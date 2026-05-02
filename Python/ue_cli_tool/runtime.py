# coding: utf-8
"""MCP-free runtime handlers for UE CLI commands and queries."""

from __future__ import annotations

import json
import logging
import time
from collections import deque
from pathlib import Path
from typing import Any, Callable

from .cli_parser import CliParser
from .config import ProjectConfig, load_config
from .connection import CommandResult, ConnectionConfig, get_connection
from .context import ContextStore
from .registry import get_registry
from .skills import get_skill_list, load_skill

logger = logging.getLogger(__name__)

_LOG_BUFFER_SIZE = 200
_MAX_BATCH = 50
_RESOURCES_DIR = Path(__file__).parent / "resources"

_context_store: ContextStore | None = None
_project_config: ProjectConfig | None = None
_command_log: deque[dict[str, Any]] = deque(maxlen=_LOG_BUFFER_SIZE)
_cli_parser: CliParser | None = None

SendCommand = Callable[[str, dict[str, Any] | None], dict[str, Any]]


def init_runtime(*, connect: bool = False, context_dir: Path | None = None) -> None:
	global _context_store, _project_config
	if _project_config is None:
		_project_config = load_config()
	if _context_store is None:
		_context_store = ContextStore(context_dir or Path(__file__).parent.parent.parent / ".context")
		if _project_config.engine_root or _project_config.project_root:
			_context_store.set_project_paths(
				engine_root=_project_config.engine_root,
				project_root=_project_config.project_root,
			)
	if connect:
		conn = get_connection(ConnectionConfig(port=_project_config.tcp_port))
		conn.on_state_change = _context_store._on_ue_state_change
		conn.connect()


def shutdown_runtime() -> None:
	if _context_store is not None:
		_context_store.shutdown()


def get_project_config() -> ProjectConfig:
	global _project_config
	if _project_config is None:
		_project_config = load_config()
	return _project_config


def get_context_store() -> ContextStore:
	if _context_store is None:
		init_runtime()
	assert _context_store is not None
	return _context_store


def _get_parser() -> CliParser:
	global _cli_parser
	if _cli_parser is None:
		_cli_parser = CliParser(get_registry())
	return _cli_parser


def _to_serializable(obj: Any) -> Any:
	if isinstance(obj, CommandResult):
		return _to_serializable(obj.to_dict())
	if isinstance(obj, dict):
		return {k: _to_serializable(v) for k, v in obj.items()}
	if isinstance(obj, (list, tuple)):
		return [_to_serializable(item) for item in obj]
	return obj


def _send_command(command_type: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
	config = get_project_config()
	conn = get_connection(ConnectionConfig(port=config.tcp_port))
	if _context_store is not None:
		conn.on_state_change = _context_store._on_ue_state_change
	if not conn.is_connected:
		conn.connect()
	return conn.send_command(command_type, params).to_dict()


def _log_command(action_id: str, params: dict | None, result: dict, elapsed_ms: float) -> None:
	_command_log.append(
		{
			"ts": time.strftime("%H:%M:%S"),
			"action": action_id,
			"ok": result.get("success", False),
			"ms": round(elapsed_ms, 1),
			"error": result.get("error"),
		}
	)


def handle_cli(
	args: dict[str, Any],
	*,
	send_command_func: SendCommand | None = None,
	log_command_func: Callable[[str, dict | None, dict, float], None] | None = None,
) -> dict[str, Any]:
	command_text = args.get("command", "")
	if not command_text.strip():
		return {"success": False, "error": "command is required", "error_type": "parse_error"}

	parser = _get_parser()
	parsed = parser.parse(command_text)
	if parsed.errors:
		return {
			"success": False,
			"error": f"Parse errors: {'; '.join(parsed.errors)}",
			"error_type": "parse_error",
			"parse_errors": parsed.errors,
		}
	if not parsed.commands:
		return {"success": True, "total": 0, "executed": 0, "results": []}

	send = send_command_func or _send_command
	log = log_command_func or _log_command

	if len(parsed.commands) == 1:
		cmd = parsed.commands[0]
		t0 = time.perf_counter()
		result = send(cmd.command, cmd.params or None)
		elapsed = (time.perf_counter() - t0) * 1000
		log(cmd.command, cmd.params, result, elapsed)
		result["_cli_line"] = cmd.raw_line
		return result

	if len(parsed.commands) > _MAX_BATCH:
		return {
			"success": False,
			"error": f"Max {_MAX_BATCH} commands per batch, got {len(parsed.commands)}",
			"error_type": "batch_too_large",
		}

	results: list[dict[str, Any]] = []
	all_ok = True
	for cmd in parsed.commands:
		t0 = time.perf_counter()
		try:
			result = send(cmd.command, cmd.params or None)
		except Exception as exc:
			result = {"success": False, "error": str(exc), "error_type": "transport_error"}
		elapsed = (time.perf_counter() - t0) * 1000
		log(cmd.command, cmd.params, result, elapsed)
		result["_cli_line"] = cmd.raw_line
		if result.get("success") is False:
			all_ok = False
		results.append(result)

	return {
		"success": all_ok,
		"total": len(parsed.commands),
		"executed": len(results),
		"results": results,
	}


def handle_query(
	args: dict[str, Any],
	*,
	send_command_func: SendCommand | None = None,
	connection_health_func: Callable[[], dict[str, Any]] | None = None,
	ping_func: Callable[[], bool] | None = None,
) -> Any:
	query = args.get("query", "").strip()
	if not query:
		return {"success": False, "error": "query is required", "error_type": "parse_error"}

	send = send_command_func or _send_command
	parts = query.split(None, 1)
	sub = parts[0].lower()
	rest = parts[1].strip() if len(parts) > 1 else ""

	if sub == "help":
		return _help_command(rest) if rest else _help_list()
	if sub == "search":
		if not rest:
			return {"success": False, "error": "Usage: search <keyword>", "error_type": "parse_error"}
		return {"success": True, "results": get_registry().search(rest, top_k=20)}
	if sub == "context":
		if _context_store is not None:
			payload = _context_store.get_resume_payload()
			config = get_project_config()
			payload["engine_root"] = config.engine_root
			payload["project_root"] = config.project_root
			return {"success": True, **payload}
		return send("get_context", None)
	if sub == "logs":
		return _handle_logs(rest, send)
	if sub == "metrics":
		from .metrics import get_metrics

		metrics = get_metrics()
		return {
			"success": True,
			"metrics": metrics.get_summary(),
			"recent_requests": metrics.get_recent(last_n=20),
		}
	if sub == "health":
		if connection_health_func is not None:
			return {"success": True, "health": connection_health_func()}
		conn = get_connection()
		return {"success": True, "health": conn.get_health()}
	if sub == "resources":
		if not rest:
			available = [f.name for f in _RESOURCES_DIR.iterdir()] if _RESOURCES_DIR.exists() else []
			return {"success": True, "available": available}
		return _read_resource(rest)
	if sub == "skills":
		if rest:
			skill_data = load_skill(rest)
			if skill_data is None:
				available = [s["skill_id"] for s in get_skill_list()]
				return {"success": False, "error": f"Unknown skill: '{rest}'", "available": available}
			return {"success": True, **skill_data}
		return {"success": True, "skills": get_skill_list()}
	if sub == "ping":
		ok = ping_func() if ping_func is not None else get_connection().ping()
		return {"success": ok, "pong": ok}

	return {
		"success": False,
		"error": f"Unknown query: '{sub}'. Try: help, search, context, logs, metrics, health, resources, skills, ping",
		"error_type": "parse_error",
	}


def _read_resource(name: str) -> str:
	path = _RESOURCES_DIR / name
	if not path.exists():
		available = [f.name for f in _RESOURCES_DIR.iterdir()] if _RESOURCES_DIR.exists() else []
		return json.dumps({"error": f"Resource '{name}' not found. Available: {available}"})
	return path.read_text(encoding="utf-8")


def _handle_logs(rest: str, send: SendCommand) -> dict[str, Any]:
	n = 20
	source = "python"
	parts = rest.split()
	i = 0
	while i < len(parts):
		if parts[i] == "--n" and i + 1 < len(parts):
			try:
				n = int(parts[i + 1])
			except ValueError:
				pass
			i += 2
		elif parts[i] == "--source" and i + 1 < len(parts):
			source = parts[i + 1]
			i += 2
		else:
			i += 1

	n = min(n, _LOG_BUFFER_SIZE)
	result: dict[str, Any] = {"success": True}
	if source == "metrics":
		from .metrics import get_metrics

		result["metrics"] = get_metrics().get_summary(last_n=n)
		return result
	if source == "health":
		result["health"] = get_connection().get_health()
		return result
	if source in ("python", "both"):
		result["python_log"] = {"total_logged": len(_command_log), "entries": list(_command_log)[-n:]}
	if source in ("editor", "both"):
		try:
			result["editor_log"] = _to_serializable(send("get_editor_logs", {"count": n}))
		except Exception as exc:
			result["editor_log"] = {"error": str(exc)}
	if source == "python":
		result["total_logged"] = result["python_log"]["total_logged"]
		result["entries"] = result["python_log"]["entries"]
		del result["python_log"]
	return result


def _help_list() -> dict[str, Any]:
	registry = get_registry()
	grouped: dict[str, list[dict[str, Any]]] = {}
	for action_id in registry.all_ids:
		action = registry.get(action_id)
		if action is None:
			continue
		domain = action_id.split(".", 1)[0] if "." in action_id else "other"
		grouped.setdefault(domain, []).append(
			{"command": action.command, "id": action.id, "description": action.description}
		)
	return {"success": True, "total": registry.count, "domains": grouped}


def _help_command(command_name: str) -> dict[str, Any]:
	registry = get_registry()
	action = registry.get_by_command(command_name) or registry.get(command_name)
	if action is None:
		return {
			"success": False,
			"error": f"Unknown command: '{command_name}'",
			"error_type": "unknown_command",
			"suggestions": registry.search(command_name, top_k=5),
		}
	required = action.input_schema.get("required", [])
	properties = action.input_schema.get("properties", {})
	description = action.description
	if action.command == "exec_python":
		description = (
			"Execute Python code in Unreal's embedded Python environment. "
			"For shell use, prefer `ue python --file` or stdin; "
			"`exec_python` remains the run-DSL compatibility command."
		)
	lines = [f"Command: {action.command}", f"ID: {action.id}", f"Description: {description}", ""]
	pos_parts = " ".join(f"<{p}>" for p in required)
	opt_parts = " ".join(
		f"[--{k} <{properties[k].get('type', 'string')}>]" for k in properties if k not in required
	)
	lines.append(f"Usage: {action.command} {pos_parts} {opt_parts}".rstrip())
	lines.append("")
	if action.command == "exec_python":
		lines.extend(_python_exec_cli_guidance())
		return {"success": True, "help": "\n".join(lines)}
	if required:
		lines.append("POSITIONAL (required):")
		for param in required:
			lines.append(f"  <{param}>  {properties.get(param, {}).get('description', '')}")
		lines.append("")
	optional = [k for k in properties if k not in required]
	if optional:
		lines.append("FLAGS (optional):")
		for param in optional:
			ptype = properties.get(param, {}).get("type", "string")
			desc = properties.get(param, {}).get("description", "")
			lines.append(f"  --{param} <{ptype}>  {desc}")
		lines.append("")
	if action.examples:
		lines.append("EXAMPLES:")
		for ex in action.examples:
			lines.append(f"  {_format_example_as_cli(action.command, ex, required)}")
	return {"success": True, "help": "\n".join(lines)}


def _python_exec_cli_guidance() -> list[str]:
	return [
		"RECOMMENDED CLI:",
		"  ue python --json --file script.py",
		"  @'",
		"  import unreal",
		"  _result = unreal.SystemLibrary.get_engine_version()",
		"  '@ | ue python --json",
		"  ue python --json \"print('hello')\"",
		"",
		"NOTES:",
		"  `ue python` and `ue py` send code directly to the daemon and do not use the run DSL parser.",
		"  Use `_result = ...` for structured return data; print output is captured as stdout.",
		"  `ue run \"exec_python <code>\"` remains available for simple compatibility cases.",
	]


def _format_example_as_cli(command: str, example: dict, required: list[str]) -> str:
	parts = [command]
	remaining = dict(example)
	for param in required:
		if param in remaining:
			val = remaining.pop(param)
			parts.append(f'"{val}"' if isinstance(val, str) and " " in val else str(val))
	for k, v in remaining.items():
		if isinstance(v, bool):
			parts.append(f"--{k}" if v else f"--{k} false")
		elif isinstance(v, (list, dict)):
			parts.append(f"--{k} {json.dumps(v)}")
		elif isinstance(v, str) and " " in v:
			parts.append(f'--{k} "{v}"')
		else:
			parts.append(f"--{k} {v}")
	return " ".join(parts)

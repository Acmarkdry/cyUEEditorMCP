#!/usr/bin/env python3
"""
UE MCP Bridge CLI —?send commands to Unreal MCPBridge via TCP.

Usage:
	python ue_mcp_cli.py <command> [json_params]
	python ue_mcp_cli.py --health
	python ue_mcp_cli.py --stats
	python ue_mcp_cli.py --batch <file.json>

Examples:
	python ue_mcp_cli.py ping
	python ue_mcp_cli.py save_all
	python ue_mcp_cli.py set_node_pin_default '{"blueprint_name":"BP_X","node_id":"GUID","pin_name":"InString","default_value":"Hello"}'

	# pipe params (recommended, avoids PowerShell escaping)
	'{"blueprint_name":"BP_X"}' | python ue_mcp_cli.py compile_blueprint

	# batch execution from file
	python ue_mcp_cli.py --batch commands.json

	# connection health check
	python ue_mcp_cli.py --health

	# performance metrics
	python ue_mcp_cli.py --stats
"""

import json
import os
import sys
from typing import Optional

# Ensure the ue_cli_tool package is importable
_parent = os.path.dirname(os.path.abspath(__file__))
if _parent not in sys.path:
	sys.path.insert(0, _parent)

from ue_cli_tool.connection import (
	PersistentUnrealConnection,
	ConnectionConfig,
	_wire_metrics,
)
from ue_cli_tool.metrics import get_metrics


def _get_connection(timeout: Optional[float] = None) -> PersistentUnrealConnection:
	"""Create a connection with optional timeout override."""
	config = ConnectionConfig()
	if timeout is not None:
		config.timeout = timeout
	conn = PersistentUnrealConnection(config)
	_wire_metrics(conn)
	return conn


def send_command(
	command_type: str, params: Optional[dict] = None, *, timeout: Optional[float] = None
) -> dict:
	"""Send a single command to UE MCPBridge and return the response."""
	conn = _get_connection(timeout)
	try:
		if not conn.connect():
			return {
				"success": False,
				"error": "Cannot connect to UE MCPBridge (port 55558). Is Unreal Editor running?",
			}
		return conn.send_raw_dict(command_type, params)
	finally:
		conn.disconnect()


def _handle_health() -> None:
	"""Show connection health status."""
	conn = _get_connection()
	try:
		connected = conn.connect()
		health = conn.get_health()
		if connected:
			conn.ping()
		health["ping_ok"] = connected
		print(json.dumps(health, indent=2, ensure_ascii=False))
	finally:
		conn.disconnect()


def _handle_stats() -> None:
	"""Show performance metrics."""
	conn = _get_connection()
	try:
		conn.connect()
		conn.ping()  # at least one metric
		summary = get_metrics().get_summary()
		print(json.dumps(summary, indent=2, ensure_ascii=False))
	finally:
		conn.disconnect()


def _handle_batch(file_path: str) -> None:
	"""Execute commands from a JSON batch file.

	Expected format:
		[
			{"type": "create_blueprint", "params": {"name": "BP_Test", "parent_class": "Actor"}},
			{"type": "compile_blueprint", "params": {"blueprint_name": "BP_Test"}}
		]
	"""
	if not os.path.isfile(file_path):
		print(f"ERROR: Batch file not found: {file_path}", file=sys.stderr)
		sys.exit(1)

	with open(file_path, "r", encoding="utf-8") as f:
		commands = json.load(f)

	if not isinstance(commands, list):
		print(
			"ERROR: Batch file must contain a JSON array of commands", file=sys.stderr
		)
		sys.exit(1)

	conn = _get_connection()
	try:
		if not conn.connect():
			print("ERROR: Cannot connect to UE MCPBridge", file=sys.stderr)
			sys.exit(1)

		batch_params = {"commands": commands, "continue_on_error": True}
		result = conn.send_raw_dict("batch_execute", batch_params)
		print(json.dumps(result, indent=2, ensure_ascii=False))
	finally:
		conn.disconnect()


def _handle_script() -> None:
	"""Execute a compact script from file or stdin.

	Usage:
		python ue_mcp_cli.py --script script.ues
		echo "@BP_Test\ncreate Actor\ncompile" | python ue_mcp_cli.py --script
	"""
	from ue_cli_tool.cli_parser import CliParser
	from ue_cli_tool.registry import get_registry
	from ue_cli_tool.connection import TimeoutTier

	# Read script from file argument or stdin
	script_text = ""
	if len(sys.argv) >= 3 and not sys.argv[2].startswith("-"):
		file_path = sys.argv[2]
		if not os.path.isfile(file_path):
			print(f"ERROR: Script file not found: {file_path}", file=sys.stderr)
			sys.exit(1)
		with open(file_path, "r", encoding="utf-8") as f:
			script_text = f.read()
	elif not sys.stdin.isatty():
		script_text = sys.stdin.read()
	else:
		print("ERROR: --script requires a file path or piped input", file=sys.stderr)
		sys.exit(1)

	if not script_text.strip():
		print("ERROR: Empty script", file=sys.stderr)
		sys.exit(1)

	parser = CliParser(get_registry())
	parsed = parser.parse(script_text)

	if parsed.errors:
		print(f"ERROR: Parse errors: {'; '.join(parsed.errors)}", file=sys.stderr)
		sys.exit(1)

	conn = _get_connection()
	try:
		if not conn.connect():
			print("ERROR: Cannot connect to UE MCPBridge", file=sys.stderr)
			sys.exit(1)

		batch = parser.to_batch_commands(parsed)
		result = conn.send_raw_dict(
			"batch_execute",
			{"commands": batch, "continue_on_error": True},
			timeout_tier=TimeoutTier.EXTRA_SLOW,
		)

		print(json.dumps(result, indent=2, ensure_ascii=False))
	finally:
		conn.disconnect()


def main():
	if len(sys.argv) < 2:
		print(__doc__)
		sys.exit(1)

	arg1 = sys.argv[1]

	# Special subcommands
	if arg1 in ("--help", "-h"):
		print(__doc__)
		sys.exit(0)

	if arg1 == "--health":
		_handle_health()
		return

	if arg1 == "--stats":
		_handle_stats()
		return

	if arg1 == "--batch":
		if len(sys.argv) < 3:
			print("ERROR: --batch requires a JSON file path", file=sys.stderr)
			sys.exit(1)
		_handle_batch(sys.argv[2])
		return

	if arg1 == "--script":
		_handle_script()
		return

	# Parse optional --timeout
	timeout = None
	command_type = arg1
	extra_args = sys.argv[2:]

	if "--timeout" in sys.argv:
		idx = sys.argv.index("--timeout")
		if idx + 1 < len(sys.argv):
			try:
				timeout = float(sys.argv[idx + 1])
			except ValueError:
				print(
					f"ERROR: Invalid timeout value: {sys.argv[idx + 1]}",
					file=sys.stderr,
				)
				sys.exit(1)
			# Remove --timeout and its value from extra_args
			extra_args = [
				a
				for i, a in enumerate(sys.argv[2:])
				if i + 2 != idx and i + 2 != idx + 1
			]

	params = None

	# Try joining remaining args as JSON
	if extra_args:
		raw = " ".join(extra_args)
		try:
			params = json.loads(raw)
		except json.JSONDecodeError:
			pass

	# Fallback: read from stdin
	if params is None and not sys.stdin.isatty():
		try:
			raw_stdin = sys.stdin.read().strip()
			if raw_stdin:
				params = json.loads(raw_stdin)
		except json.JSONDecodeError as e:
			print(f"ERROR: Invalid JSON params: {e}", file=sys.stderr)
			sys.exit(1)

	try:
		result = send_command(command_type, params, timeout=timeout)
		print(json.dumps(result, indent=2, ensure_ascii=False))
	except ConnectionRefusedError:
		print(
			"ERROR: Cannot connect to UE MCPBridge (port 55558). Is Unreal Editor running?",
			file=sys.stderr,
		)
		sys.exit(1)
	except Exception as e:
		print(f"ERROR: {e}", file=sys.stderr)
		sys.exit(1)


if __name__ == "__main__":
	main()

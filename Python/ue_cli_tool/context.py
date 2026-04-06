# coding: utf-8
"""
Context Layer —?persistent cross-session state for the MCP server.

Maintains session metadata, operation history and a working-set of
recently-touched assets.  All state is persisted to a `.context/`
directory so that it survives process restarts.
"""

from __future__ import annotations

import json
import logging
import os
import tempfile
import threading
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

logger = logging.getLogger(__name__)

# ──── constants ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

ASSET_PARAM_KEYS: list[str] = [
	"blueprint_name",
	"material_name",
	"asset_path",
	"widget_name",
	"anim_blueprint",
	"mapping_context",
]

_MAX_HISTORY = 500
_MAX_HISTORY_QUERY = 100
_STALE_WORKSET_DAYS = 7
_CODE_TRUNCATE_LEN = 80


# ──── helpers ──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────


def _now_iso() -> str:
	"""Return current UTC time as ISO-8601 string."""
	return datetime.now(timezone.utc).isoformat()


# ──── ContextStore ────────────────────────────────────────────────────────────────────────────────────────────────────────────────


class ContextStore:
	"""
	Persistent context store embedded in the MCP server process.

	Files managed under ``context_dir`` (default ``.context/``):
	- ``session.json``  —?current session metadata + UE connection state
	- ``history.jsonl`` —?append-only operation log (one JSON object per line)
	- ``workset.json``  —?asset path →?last-operation mapping
	"""

	def __init__(self, context_dir: str | Path | None = None):
		if context_dir is None:
			context_dir = Path.cwd() / ".context"
		self._context_dir = Path(context_dir)
		self._context_dir.mkdir(parents=True, exist_ok=True)

		self._session_path = self._context_dir / "session.json"
		self._history_path = self._context_dir / "history.jsonl"
		self._workset_path = self._context_dir / "workset.json"

		self._lock = threading.Lock()

		# In-memory caches
		self._session: dict[str, Any] = {}
		self._workset: dict[str, Any] = {}
		self._history: list[dict[str, Any]] = []

		# Boot sequence
		self._boot()

	# ──── file I/O helpers ────────────────────────────────────────────────────────────────────────────────────────

	def _atomic_write_json(self, path: Path, data: Any) -> None:
		"""Write *data* as JSON via a temp file + rename for crash safety."""
		dir_ = path.parent
		try:
			fd, tmp = tempfile.mkstemp(dir=str(dir_), suffix=".tmp")
			try:
				with os.fdopen(fd, "w", encoding="utf-8") as f:
					json.dump(data, f, ensure_ascii=False, indent=2)
			except BaseException:
				os.unlink(tmp)
				raise
			# On Windows, target must not exist for os.rename; use replace.
			os.replace(tmp, str(path))
		except Exception:
			logger.warning("Failed to write %s", path, exc_info=True)

	def _safe_read_json(self, path: Path, default: Any = None) -> Any:
		"""Read a JSON file, returning *default* on any error."""
		try:
			text = path.read_text(encoding="utf-8")
			return json.loads(text)
		except FileNotFoundError:
			return default
		except Exception:
			logger.warning(
				"Corrupted or unreadable file %s —?using default", path, exc_info=True
			)
			return default

	# ──── session lifecycle ──────────────────────────────────────────────────────────────────────────────────────

	def _boot(self) -> None:
		"""Run on construction: detect previous session, create a new one."""
		previous = self._safe_read_json(self._session_path)

		previous_session: dict[str, Any] | None = None
		if previous and isinstance(previous, dict):
			old_status = previous.get("status", "")
			if old_status != "ended":
				previous["status"] = "abnormal"
			previous_session = {
				"session_id": previous.get("session_id"),
				"status": previous.get("status"),
				"started_at": previous.get("started_at"),
				"ended_at": previous.get("ended_at"),
				"op_count": previous.get("op_count", 0),
				"ue_connection": previous.get("ue_connection"),
				"crash_context": previous.get("crash_context"),
			}

		self._session = {
			"session_id": str(uuid.uuid4()),
			"status": "active",
			"started_at": _now_iso(),
			"ended_at": None,
			"ue_connection": "unknown",
			"op_count": 0,
			"previous_session": previous_session,
		}
		self._save_session()

		# Load history & workset
		self._load_history()
		self._truncate_history()
		self._load_workset()

	def _load_session(self) -> None:
		data = self._safe_read_json(self._session_path, default={})
		if data:
			self._session = data

	def _save_session(self) -> None:
		self._atomic_write_json(self._session_path, self._session)

	def shutdown(self) -> None:
		"""Mark the session as ended (call on graceful exit)."""
		with self._lock:
			self._session["status"] = "ended"
			self._session["ended_at"] = _now_iso()
			self._save_session()
			logger.info(
				"ContextStore session %s ended", self._session.get("session_id")
			)

	# ──── operation history ──────────────────────────────────────────────────────────────────────────────────────

	def _load_history(self) -> None:
		self._history = []
		if not self._history_path.exists():
			return
		try:
			with open(self._history_path, "r", encoding="utf-8") as f:
				for line in f:
					line = line.strip()
					if line:
						try:
							self._history.append(json.loads(line))
						except json.JSONDecodeError:
							continue
		except Exception:
			logger.warning("Failed to load history", exc_info=True)

	def _truncate_history(self, max_entries: int = _MAX_HISTORY) -> None:
		"""Keep only the most recent *max_entries* history entries."""
		if len(self._history) > max_entries:
			self._history = self._history[-max_entries:]
			# Re-write the file with truncated data
			self._rewrite_history()

	def _rewrite_history(self) -> None:
		"""Re-write the full history file from memory."""
		try:
			with open(self._history_path, "w", encoding="utf-8") as f:
				for entry in self._history:
					f.write(json.dumps(entry, ensure_ascii=False) + "\n")
		except Exception:
			logger.warning("Failed to rewrite history", exc_info=True)

	def _append_history(self, entry: dict[str, Any]) -> None:
		self._history.append(entry)
		try:
			with open(self._history_path, "a", encoding="utf-8") as f:
				f.write(json.dumps(entry, ensure_ascii=False) + "\n")
		except Exception:
			logger.warning("Failed to append history entry", exc_info=True)

	@staticmethod
	def _summarize_params(params: dict | None) -> dict[str, Any]:
		"""Extract key fields from *params*, truncating long values."""
		if not params:
			return {}
		summary: dict[str, Any] = {}
		for key, value in params.items():
			if (
				key == "code"
				and isinstance(value, str)
				and len(value) > _CODE_TRUNCATE_LEN
			):
				summary[key] = value[:_CODE_TRUNCATE_LEN] + "..."
			elif key in ASSET_PARAM_KEYS:
				summary[key] = value
			elif key in (
				"action_id",
				"action",
				"query",
				"name",
				"skill_id",
				"task_id",
				"component_type",
				"component_name",
				"variable_name",
				"function_name",
				"event_name",
				"node_id",
				"source_pin",
				"target_pin",
			):
				summary[key] = value
		return summary

	@staticmethod
	def _summarize_result(result: Any, success: bool) -> str:
		"""Return a short textual summary of a tool result."""
		if not isinstance(result, dict):
			return str(result)[:200] if result else ""
		if success:
			# Pull out the most informative fields
			for key in (
				"message",
				"return_value",
				"description",
				"name",
				"status",
				"total",
				"results_count",
				"stdout",
			):
				if key in result:
					val = result[key]
					s = str(val)
					return s[:200] if len(s) > 200 else s
			return "ok"
		# failure
		return str(result.get("error", "unknown error"))[:300]

	def record_operation(
		self,
		tool: str,
		action_id: str | None,
		params: dict | None,
		success: bool,
		result: Any,
		duration_ms: float,
	) -> None:
		"""Record a completed tool invocation (called automatically after each _handle_tool)."""
		entry = {
			"timestamp": _now_iso(),
			"tool": tool,
			"action_id": action_id or "",
			"params_summary": self._summarize_params(params),
			"success": success,
			"result_summary": self._summarize_result(result, success),
			"duration_ms": round(duration_ms, 1),
		}
		with self._lock:
			self._append_history(entry)
			self._session["op_count"] = self._session.get("op_count", 0) + 1
			self._save_session()

	def get_history(self, limit: int = 20) -> list[dict[str, Any]]:
		"""Return the most recent *limit* history entries (max 100)."""
		limit = max(1, min(limit, _MAX_HISTORY_QUERY))
		with self._lock:
			return list(self._history[-limit:])

	# ──── working set ──────────────────────────────────────────────────────────────────────────────────────────────────

	def _load_workset(self) -> None:
		data = self._safe_read_json(self._workset_path, default={})
		self._workset = data if isinstance(data, dict) else {}

	def _save_workset(self) -> None:
		self._atomic_write_json(self._workset_path, self._workset)

	def track_assets(self, params: dict | None, action_id: str | None = None) -> None:
		"""Extract asset paths from *params* and update the working set."""
		if not params:
			return
		now = _now_iso()
		with self._lock:
			changed = False
			for key in ASSET_PARAM_KEYS:
				value = params.get(key)
				if value and isinstance(value, str):
					if value in self._workset:
						entry = self._workset[value]
						entry["last_op"] = action_id or ""
						entry["last_op_time"] = now
						entry["op_count"] = entry.get("op_count", 0) + 1
					else:
						self._workset[value] = {
							"path": value,
							"first_seen": now,
							"last_op": action_id or "",
							"last_op_time": now,
							"op_count": 1,
						}
					changed = True
			if changed:
				self._save_workset()

	def _cleanup_stale_workset(self, days: int = _STALE_WORKSET_DAYS) -> None:
		"""Remove workset entries that have not been operated on for *days* days."""
		cutoff = time.time() - days * 86400
		to_remove: list[str] = []
		for key, entry in self._workset.items():
			last_op_time = entry.get("last_op_time", "")
			if last_op_time:
				try:
					ts = datetime.fromisoformat(last_op_time).timestamp()
					if ts < cutoff:
						to_remove.append(key)
				except (ValueError, TypeError):
					pass
		if to_remove:
			for key in to_remove:
				del self._workset[key]
			self._save_workset()
			logger.info("Cleaned up %d stale workset entries", len(to_remove))

	def get_workset(self) -> dict[str, Any]:
		"""Return the full working set."""
		with self._lock:
			return dict(self._workset)

	def clear(self) -> None:
		"""Clear working set and history, reset op_count (session ID preserved)."""
		with self._lock:
			self._workset = {}
			self._history = []
			self._session["op_count"] = 0
			self._save_workset()
			self._rewrite_history()
			self._save_session()
			logger.info("ContextStore cleared")

	# ──── UE connection state callback ────────────────────────────────────────────────────────────────

	def _on_ue_state_change(
		self, new_state: str, old_state: str, ts: str | None = None
	) -> None:
		"""Callback invoked by PersistentUnrealConnection on state transitions."""
		ts = ts or _now_iso()
		with self._lock:
			self._session["ue_connection"] = new_state
			if new_state == "crashed":
				# Persist crash context immediately
				self._session["crash_context"] = {
					"crash_time": ts,
					"last_op": self._history[-1] if self._history else None,
					"workset": dict(self._workset),
				}
			elif new_state == "alive" and old_state == "crashed":
				self._session["recovered_from_crash"] = True
			self._save_session()
			logger.info("UE connection state: %s →?%s", old_state, new_state)

	# ──── resume payload ────────────────────────────────────────────────────────────────────────────────────────────

	def get_resume_payload(self) -> dict[str, Any]:
		"""Build the payload returned by ``ue_context(action="resume")``."""
		with self._lock:
			self._cleanup_stale_workset()

			prev = self._session.get("previous_session")
			if prev:
				raw_status = prev.get("status", "")
				if raw_status == "ended":
					prev_status = "ended_normally"
				elif raw_status == "abnormal":
					prev_status = "previous_session_abnormal"
				else:
					prev_status = raw_status

				prev_ue = prev.get("ue_connection", "unknown")
				if prev_ue == "crashed":
					prev_status = "ue_crashed"

				previous_session = {
					"session_id": prev.get("session_id"),
					"status": prev_status,
					"started_at": prev.get("started_at"),
					"ended_at": prev.get("ended_at"),
					"op_count": prev.get("op_count", 0),
					"last_known_ue_state": prev_ue,
					"crash_context": prev.get("crash_context"),
				}
				if prev_ue == "crashed":
					previous_session["recovery_hint"] = (
						"UE may have auto-saved. Reconnect and verify working_set assets."
					)
			else:
				previous_session = None

			return {
				"previous_session": previous_session,
				"ue_connection": self._session.get("ue_connection", "unknown"),
				"workset": list(self._workset.values()),
				"recent_ops": list(self._history[-10:]),
			}

	def get_status(self) -> dict[str, Any]:
		"""Return real-time session status for ``ue_context(action="status")``."""
		with self._lock:
			last_op_at = self._history[-1]["timestamp"] if self._history else None
			return {
				"session_id": self._session.get("session_id"),
				"started_at": self._session.get("started_at"),
				"ue_connection": self._session.get("ue_connection", "unknown"),
				"op_count": self._session.get("op_count", 0),
				"workset_size": len(self._workset),
				"last_op_at": last_op_at,
			}

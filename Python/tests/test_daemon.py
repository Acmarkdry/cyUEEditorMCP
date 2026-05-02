# coding: utf-8
from __future__ import annotations

import time
from concurrent.futures import ThreadPoolExecutor

from ue_cli_tool.config import ProjectConfig
from ue_cli_tool.daemon import UEDaemon


class _FakeConnection:
	def __init__(self):
		self.is_connected = True
		self.on_state_change = None

	def connect(self):
		self.is_connected = True
		return True

	def disconnect(self):
		self.is_connected = False

	def send_command(self, command_type, params=None):
		class _Result:
			def to_dict(self_inner):
				return {"success": True, "command": command_type, "params": params or {}}

		return _Result()

	def get_health(self):
		return {"connection_state": "connected", "is_connected": True}

	def ping(self):
		return True


class _FakeContext:
	def _on_ue_state_change(self, *args, **kwargs):
		pass

	def shutdown(self):
		pass

	def record_operation(self, *args, **kwargs):
		pass

	def track_assets(self, *args, **kwargs):
		pass

	def get_status(self):
		return {"ue_connection": "alive"}


def _daemon(monkeypatch):
	monkeypatch.setattr("ue_cli_tool.daemon.ContextStore", lambda *a, **k: _FakeContext())
	monkeypatch.setattr("ue_cli_tool.daemon.PersistentUnrealConnection", lambda *a, **k: _FakeConnection())
	monkeypatch.setattr("ue_cli_tool.daemon._wire_metrics", lambda *a, **k: None)
	return UEDaemon(ProjectConfig(tcp_port=55558, daemon_port=55559))


def test_daemon_run_returns_envelope(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "run", "command": "get_context"})

	assert response["success"] is True
	assert response["action"] == "get_context"
	assert response["data"]["command"] == "get_context"


def test_daemon_query_health_uses_owned_connection(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "query", "query": "health"})

	assert response["success"] is True
	assert response["data"]["health"]["is_connected"] is True


def test_daemon_exec_python_bypasses_cli_parser(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "exec_python", "code": "import unreal; _result = 1"})

	assert response["success"] is True
	assert response["action"] == "exec_python"
	assert response["data"]["command"] == "exec_python"
	assert response["data"]["params"] == {"code": "import unreal; _result = 1"}


def test_daemon_status_includes_source_identity(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "status"})

	assert response["success"] is True
	source = response["data"]["source"]
	assert source["python_executable"]
	assert source["daemon_module"].endswith("daemon.py")
	assert source["cli_parser_module"].endswith("cli_parser.py")


def test_daemon_unknown_request_is_error(monkeypatch):
	daemon = _daemon(monkeypatch)

	response = daemon.handle_request({"type": "wat"})

	assert response["success"] is False
	assert response["error"]["code"] == "UNKNOWN_DAEMON_REQUEST"


def test_daemon_serializes_shared_ue_connection(monkeypatch):
	class _SlowConnection(_FakeConnection):
		def __init__(self):
			super().__init__()
			self.active = 0
			self.max_active = 0

		def send_command(self, command_type, params=None):
			self.active += 1
			self.max_active = max(self.max_active, self.active)
			try:
				time.sleep(0.02)
				return super().send_command(command_type, params)
			finally:
				self.active -= 1

	connection = _SlowConnection()
	monkeypatch.setattr("ue_cli_tool.daemon.ContextStore", lambda *a, **k: _FakeContext())
	monkeypatch.setattr("ue_cli_tool.daemon.PersistentUnrealConnection", lambda *a, **k: connection)
	monkeypatch.setattr("ue_cli_tool.daemon._wire_metrics", lambda *a, **k: None)
	daemon = UEDaemon(ProjectConfig(tcp_port=55558, daemon_port=55559))

	with ThreadPoolExecutor(max_workers=4) as pool:
		results = list(pool.map(lambda i: daemon._send_command("ping", {"i": i}), range(8)))

	assert all(result["success"] for result in results)
	assert connection.max_active == 1

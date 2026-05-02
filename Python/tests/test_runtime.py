# coding: utf-8
from __future__ import annotations

from ue_cli_tool import runtime


def setup_function():
	runtime._cli_parser = None


def test_handle_cli_multiline_executes_commands_sequentially():
	calls = []
	logged = []

	def fake_send(command, params):
		calls.append((command, params))
		return {"success": True, "command": command, "params": params}

	def fake_log(command, params, result, elapsed_ms):
		logged.append((command, params, result, elapsed_ms))

	result = runtime.handle_cli(
		{"command": "exec_python _result = 1\nexec_python _result = 2"},
		send_command_func=fake_send,
		log_command_func=fake_log,
	)

	assert result["success"] is True
	assert result["total"] == 2
	assert result["executed"] == 2
	assert calls == [
		("exec_python", {"code": "_result = 1"}),
		("exec_python", {"code": "_result = 2"}),
	]
	assert [item["_cli_line"] for item in result["results"]] == [
		"exec_python _result = 1",
		"exec_python _result = 2",
	]
	assert len(logged) == 2


def test_handle_cli_multiline_continues_after_child_failure():
	calls = []

	def fake_send(command, params):
		calls.append((command, params))
		if len(calls) == 1:
			return {"success": False, "error": "first failed"}
		return {"success": True, "return_value": 2}

	result = runtime.handle_cli(
		{"command": "exec_python _result = 1\nexec_python _result = 2"},
		send_command_func=fake_send,
		log_command_func=lambda *args: None,
	)

	assert result["success"] is False
	assert result["executed"] == 2
	assert result["results"][0]["error"] == "first failed"
	assert result["results"][1]["return_value"] == 2

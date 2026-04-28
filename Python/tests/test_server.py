# coding: utf-8
"""
Integration tests for the Server module (_handle_cli and _handle_query).

Uses Mock to replace actual TCP connections so tests run without a UE editor.

**Validates: Requirements 18.1, 18.2, 18.3**
"""

from __future__ import annotations

import json
from unittest.mock import patch, MagicMock

import pytest

from ue_cli_tool import server as server_mod
from ue_cli_tool.server import _handle_cli, _handle_query


# ---------------------------------------------------------------------------
# Fixtures / helpers
# ---------------------------------------------------------------------------


@pytest.fixture(autouse=True)
def _reset_parser():
    """Reset the cached CliParser before each test so registry changes
    don't leak between tests."""
    server_mod._cli_parser = None
    yield
    server_mod._cli_parser = None


def _mock_send_command(return_value=None):
    """Return a patcher for ``server._send_command`` that returns *return_value*.

    Default return value is a simple success dict.
    """
    if return_value is None:
        return_value = {"success": True, "result": "ok"}
    return patch.object(server_mod, "_send_command", return_value=return_value)


# ===========================================================================
# _handle_cli tests  (Requirement 18.1)
# ===========================================================================


class TestHandleCliSingleCommand:
    """_handle_cli: single command execution."""

    def test_single_command_returns_result(self):
        """A single valid command is sent to _send_command and the result
        is returned with the original CLI line attached."""
        expected = {"success": True, "name": "BP_Test"}
        with _mock_send_command(expected) as mock_send:
            result = _handle_cli({"command": "create_blueprint BP_Test"})

        assert result["success"] is True
        assert result["name"] == "BP_Test"
        # The original CLI line should be attached
        assert "_cli_line" in result
        mock_send.assert_called_once()
        # First positional arg should be the command name
        call_args = mock_send.call_args
        assert call_args[0][0] == "create_blueprint"

    def test_single_command_with_flags(self):
        """Flags are forwarded as params to _send_command."""
        expected = {"success": True}
        with _mock_send_command(expected) as mock_send:
            result = _handle_cli(
                {"command": "create_blueprint BP_Player --parent_class Character"}
            )

        assert result["success"] is True
        call_args = mock_send.call_args
        params = call_args[0][1]  # second positional arg = params dict
        assert params is not None
        assert "parent_class" in params


class TestHandleCliBatch:
    """_handle_cli: multi-command batch execution."""

    def test_multi_command_batch(self):
        """Multiple lines are sent as a single batch_execute call."""
        batch_result = {
            "success": True,
            "results": [
                {"success": True, "result": "created"},
                {"success": True, "result": "compiled"},
            ],
        }
        with _mock_send_command(batch_result) as mock_send:
            result = _handle_cli(
                {
                    "command": (
                        "create_blueprint BP_Test\n"
                        "compile_blueprint BP_Test"
                    )
                }
            )

        assert result["success"] is True
        # batch_execute should be the command sent
        mock_send.assert_called_once()
        call_args = mock_send.call_args
        assert call_args[0][0] == "batch_execute"
        # The batch payload should contain 2 commands
        batch_params = call_args[0][1]
        assert len(batch_params["commands"]) == 2

    def test_batch_results_have_cli_lines(self):
        """Each sub-result in a batch should have the original _cli_line."""
        batch_result = {
            "success": True,
            "results": [
                {"success": True},
                {"success": True},
            ],
        }
        with _mock_send_command(batch_result):
            result = _handle_cli(
                {
                    "command": (
                        "create_blueprint BP_A\n"
                        "create_blueprint BP_B"
                    )
                }
            )

        for sub in result["results"]:
            assert "_cli_line" in sub


class TestHandleCliEmptyCommand:
    """_handle_cli: empty command returns error."""

    def test_empty_string_returns_error(self):
        """An empty command string returns an error without calling _send_command."""
        with _mock_send_command() as mock_send:
            result = _handle_cli({"command": ""})

        assert result["success"] is False
        assert "required" in result["error"].lower()
        mock_send.assert_not_called()

    def test_whitespace_only_returns_error(self):
        """A whitespace-only command string returns an error."""
        with _mock_send_command() as mock_send:
            result = _handle_cli({"command": "   \n  \n  "})

        assert result["success"] is False
        mock_send.assert_not_called()

    def test_missing_command_key_returns_error(self):
        """Missing 'command' key in args returns an error."""
        with _mock_send_command() as mock_send:
            result = _handle_cli({})

        assert result["success"] is False
        mock_send.assert_not_called()


class TestHandleCliParseError:
    """_handle_cli: parse error returns error info."""

    def test_empty_target_returns_parse_error(self):
        """An empty @target line produces a parse error."""
        with _mock_send_command() as mock_send:
            result = _handle_cli({"command": "@\ncreate_blueprint BP_Test"})

        # The parser should report an error for the empty @target
        # but still parse the second command — check that the error is surfaced
        # The behavior depends on whether there are also valid commands.
        # With one error and one valid command, the parser returns errors list
        # and _handle_cli returns the error.
        assert result["success"] is False
        assert "parse_errors" in result or "error" in result


# ===========================================================================
# _handle_query tests  (Requirement 18.2)
# ===========================================================================


class TestHandleQueryHelp:
    """_handle_query: help returns command list."""

    def test_help_returns_domains(self):
        """'help' query returns a grouped command list with total count."""
        result = _handle_query({"query": "help"})

        assert result["success"] is True
        assert "total" in result
        assert "domains" in result
        assert isinstance(result["domains"], dict)
        # There should be at least some registered actions
        assert result["total"] > 0

    def test_help_command_returns_details(self):
        """'help <command>' returns detailed help for a known command."""
        result = _handle_query({"query": "help create_blueprint"})

        assert result["success"] is True
        assert "help" in result
        help_text = result["help"]
        assert "create_blueprint" in help_text
        # Should contain usage, description sections
        assert "Usage:" in help_text or "Command:" in help_text

    def test_help_unknown_command_returns_suggestions(self):
        """'help <unknown>' returns an error with suggestions."""
        result = _handle_query({"query": "help totally_nonexistent_command_xyz"})

        assert result["success"] is False
        assert "suggestions" in result


class TestHandleQuerySearch:
    """_handle_query: search returns results."""

    def test_search_returns_results(self):
        """'search blueprint' returns matching actions."""
        result = _handle_query({"query": "search blueprint"})

        assert result["success"] is True
        assert "results" in result
        assert isinstance(result["results"], list)
        # 'blueprint' is a common keyword — should find matches
        assert len(result["results"]) > 0

    def test_search_empty_keyword_returns_error(self):
        """'search' without a keyword returns an error."""
        result = _handle_query({"query": "search"})

        assert result["success"] is False
        assert "usage" in result["error"].lower() or "keyword" in result["error"].lower()


class TestHandleQuerySkills:
    """_handle_query: skills returns skill list."""

    def test_skills_returns_list(self):
        """'skills' query returns a list of available skills."""
        result = _handle_query({"query": "skills"})

        assert result["success"] is True
        assert "skills" in result
        assert isinstance(result["skills"], list)
        # There should be at least some skills defined
        assert len(result["skills"]) > 0

    def test_skills_specific_returns_content(self):
        """'skills <skill_id>' returns the skill content for a known skill."""
        # First get the list to find a valid skill_id
        list_result = _handle_query({"query": "skills"})
        assert list_result["success"] is True
        skills = list_result["skills"]
        if not skills:
            pytest.skip("No skills registered")

        skill_id = skills[0]["skill_id"]
        result = _handle_query({"query": f"skills {skill_id}"})

        assert result["success"] is True

    def test_skills_unknown_returns_error(self):
        """'skills <unknown>' returns an error with available skill list."""
        result = _handle_query({"query": "skills nonexistent_skill_xyz"})

        assert result["success"] is False
        assert "available" in result


class TestHandleQueryUnknown:
    """_handle_query: unknown query returns error."""

    def test_unknown_query_returns_error(self):
        """An unrecognized query keyword returns an error with suggestions."""
        result = _handle_query({"query": "foobar"})

        assert result["success"] is False
        assert "Unknown query" in result["error"]
        # Should suggest valid query types
        assert "help" in result["error"]

    def test_empty_query_returns_error(self):
        """An empty query string returns an error."""
        result = _handle_query({"query": ""})

        assert result["success"] is False
        assert "required" in result["error"].lower()

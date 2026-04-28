# coding: utf-8
"""
Property-based tests for BatchContext pending_count invariant.

Feature: v0.4.0-platform-extensions, Property 6: BatchContext pending_count invariant

Uses Hypothesis to generate positive integers N (1 ≤ N ≤ 50) and command name lists.
Verifies that after adding N commands, pending_count == N, and after execution,
pending_count == 0 and is_executed == True.

**Validates: Requirements 16.3**
"""

from __future__ import annotations

from unittest.mock import MagicMock

from hypothesis import given, settings
from hypothesis import strategies as st

from ue_cli_tool.pipeline import BatchContext

# ---------------------------------------------------------------------------
# Strategies
# ---------------------------------------------------------------------------

# Command names: simple alphanumeric identifiers resembling real UE commands.
_command_name = st.from_regex(r"[a-z][a-z0-9_]{2,30}", fullmatch=True)

# Generate N (1 ≤ N ≤ 50) and a list of exactly N command names.
_n_and_commands = st.integers(min_value=1, max_value=50).flatmap(
    lambda n: st.tuples(
        st.just(n),
        st.lists(_command_name, min_size=n, max_size=n),
    )
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _make_mock_connection() -> MagicMock:
    """Create a mock PersistentUnrealConnection that returns a success result
    from send_raw_dict (used by BatchContext.execute)."""
    conn = MagicMock()
    conn.send_raw_dict.return_value = {
        "success": True,
        "results": [],
    }
    return conn


# ---------------------------------------------------------------------------
# Property 6: BatchContext pending_count invariant
# ---------------------------------------------------------------------------


@given(data=_n_and_commands)
@settings(max_examples=100)
def test_batch_context_pending_count_invariant(data: tuple[int, list[str]]):
    """For any positive integer N (1 ≤ N ≤ 50) and any command name list,
    after adding N commands pending_count == N; after execution,
    pending_count == 0 and is_executed == True.

    Feature: v0.4.0-platform-extensions, Property 6: BatchContext pending_count invariant

    **Validates: Requirements 16.3**
    """
    n, command_names = data

    conn = _make_mock_connection()
    batch = BatchContext(conn)

    # --- Pre-execution: pending_count tracks additions ---
    assert batch.pending_count == 0, "Empty batch should have pending_count == 0"
    assert batch.is_executed is False, "New batch should not be executed"

    for i, cmd in enumerate(command_names):
        batch.add(cmd)
        assert batch.pending_count == i + 1, (
            f"After adding {i + 1} commands, pending_count should be {i + 1}, "
            f"got {batch.pending_count}"
        )

    assert batch.pending_count == n, (
        f"After adding {n} commands, pending_count should be {n}, "
        f"got {batch.pending_count}"
    )

    # --- Post-execution: pending_count resets to 0 ---
    batch.execute()

    assert batch.pending_count == 0, (
        f"After execution, pending_count should be 0, got {batch.pending_count}"
    )
    assert batch.is_executed is True, (
        "After execution, is_executed should be True"
    )


# ---------------------------------------------------------------------------
# Unit Tests for BatchContext and AsyncSubmitter
# ---------------------------------------------------------------------------
# Requirements: 16.1, 16.2, 16.3

import time
from unittest.mock import patch

import pytest

from ue_cli_tool.pipeline import AsyncSubmitter, BatchContext


# ═══════════════════════════════════════════════════════════════════════════
# BatchContext Unit Tests
# ═══════════════════════════════════════════════════════════════════════════


class TestBatchContextAdd:
    """Tests for BatchContext.add() method."""

    def test_add_single_command(self):
        """add() appends a command to the internal queue."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        idx = batch.add("create_blueprint", {"name": "BP_Test"})

        assert idx == 0
        assert batch.pending_count == 1

    def test_add_multiple_commands(self):
        """add() appends multiple commands, each returning incremental index."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        idx0 = batch.add("create_blueprint", {"name": "BP_A"})
        idx1 = batch.add("compile_blueprint", {"blueprint_name": "BP_A"})
        idx2 = batch.add("delete_blueprint", {"blueprint_name": "BP_A"})

        assert idx0 == 0
        assert idx1 == 1
        assert idx2 == 2
        assert batch.pending_count == 3

    def test_add_command_without_params(self):
        """add() works with command name only (no params)."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        idx = batch.add("list_blueprints")

        assert idx == 0
        assert batch.pending_count == 1


class TestBatchContextGetattr:
    """Tests for BatchContext.__getattr__ dynamic method dispatch."""

    def test_dynamic_method_adds_command(self):
        """Dynamic method call (e.g. batch.create_blueprint(...)) queues a command."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        idx = batch.create_blueprint(name="BP_Test", parent_class="Actor")

        assert idx == 0
        assert batch.pending_count == 1

    def test_dynamic_method_multiple_calls(self):
        """Multiple dynamic method calls queue multiple commands."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        batch.create_blueprint(name="BP_Test")
        batch.add_component_to_blueprint(
            blueprint_name="BP_Test",
            component_type="StaticMeshComponent",
            component_name="Mesh",
        )

        assert batch.pending_count == 2

    def test_dynamic_method_no_kwargs(self):
        """Dynamic method with no keyword args queues command with no params."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        idx = batch.list_blueprints()

        assert idx == 0
        assert batch.pending_count == 1

    def test_private_attr_raises_attribute_error(self):
        """Accessing a private attribute (starts with _) raises AttributeError."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        with pytest.raises(AttributeError):
            _ = batch._internal_thing


class TestBatchContextContextManager:
    """Tests for BatchContext as a context manager."""

    def test_context_manager_auto_executes(self):
        """Exiting the context manager auto-executes the batch."""
        conn = _make_mock_connection()

        with BatchContext(conn) as batch:
            batch.add("create_blueprint", {"name": "BP_Test"})
            assert batch.is_executed is False

        assert batch.is_executed is True
        conn.send_raw_dict.assert_called_once()

    def test_context_manager_results_available_after_exit(self):
        """Results are accessible after the context manager exits."""
        conn = _make_mock_connection()
        conn.send_raw_dict.return_value = {
            "success": True,
            "results": [{"success": True, "result": "ok"}],
        }

        with BatchContext(conn) as batch:
            batch.add("create_blueprint", {"name": "BP_Test"})

        assert len(batch.results) == 1
        assert batch.results[0]["success"] is True

    def test_context_manager_no_execute_on_exception(self):
        """If an exception occurs inside the context, batch is NOT executed."""
        conn = _make_mock_connection()

        with pytest.raises(ValueError):
            with BatchContext(conn) as batch:
                batch.add("create_blueprint", {"name": "BP_Test"})
                raise ValueError("something went wrong")

        assert batch.is_executed is False
        conn.send_raw_dict.assert_not_called()


class TestBatchContextAlreadyExecuted:
    """Tests for RuntimeError when adding to an already-executed batch."""

    def test_add_after_execute_raises_runtime_error(self):
        """Calling add() after execute() raises RuntimeError."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)
        batch.add("create_blueprint", {"name": "BP_Test"})
        batch.execute()

        with pytest.raises(RuntimeError, match="already executed"):
            batch.add("compile_blueprint", {"blueprint_name": "BP_Test"})

    def test_dynamic_method_after_execute_raises_runtime_error(self):
        """Calling a dynamic method after execute() raises RuntimeError."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)
        batch.add("create_blueprint", {"name": "BP_Test"})
        batch.execute()

        with pytest.raises(RuntimeError, match="already executed"):
            batch.compile_blueprint(blueprint_name="BP_Test")


class TestBatchContextMaxSize:
    """Tests for ValueError when exceeding max batch size (50)."""

    def test_exceeding_max_batch_size_raises_value_error(self):
        """Adding more than 50 commands raises ValueError."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        # Fill to capacity
        for i in range(50):
            batch.add(f"cmd_{i}")

        assert batch.pending_count == 50

        with pytest.raises(ValueError, match="max"):
            batch.add("one_too_many")

    def test_exactly_max_batch_size_is_allowed(self):
        """Adding exactly 50 commands does not raise."""
        conn = _make_mock_connection()
        batch = BatchContext(conn)

        for i in range(50):
            batch.add(f"cmd_{i}")

        assert batch.pending_count == 50


# ═══════════════════════════════════════════════════════════════════════════
# AsyncSubmitter Unit Tests
# ═══════════════════════════════════════════════════════════════════════════


def _make_async_mock_connection(
    submit_result=None, poll_result=None
) -> MagicMock:
    """Create a mock connection for AsyncSubmitter tests."""
    conn = MagicMock()

    if submit_result is None:
        submit_result = {
            "success": True,
            "result": {"task_id": "task-abc-123"},
        }
    if poll_result is None:
        poll_result = {
            "success": True,
            "result": {"status": "completed", "data": "done"},
        }

    def side_effect(cmd, params, **kwargs):
        if cmd == "async_execute":
            return submit_result
        elif cmd == "get_task_result":
            return poll_result
        return {"success": True}

    conn.send_raw_dict.side_effect = side_effect
    return conn


class TestAsyncSubmitterSubmit:
    """Tests for AsyncSubmitter.submit()."""

    def test_submit_returns_task_id(self):
        """submit() returns the task_id from the server response."""
        conn = _make_async_mock_connection()
        sub = AsyncSubmitter(conn)

        task_id = sub.submit("compile_blueprint", {"blueprint_name": "BP_Test"})

        assert task_id == "task-abc-123"
        conn.send_raw_dict.assert_called_once()

    def test_submit_returns_empty_string_on_missing_task_id(self):
        """submit() returns empty string when server response has no task_id."""
        conn = _make_async_mock_connection(
            submit_result={"success": True, "result": {}}
        )
        sub = AsyncSubmitter(conn)

        task_id = sub.submit("compile_blueprint", {"blueprint_name": "BP_Test"})

        assert task_id == ""


class TestAsyncSubmitterPoll:
    """Tests for AsyncSubmitter.poll()."""

    def test_poll_returns_task_status(self):
        """poll() returns the full result dict including status."""
        poll_result = {
            "success": True,
            "result": {"status": "running", "progress": 50},
        }
        conn = _make_async_mock_connection(poll_result=poll_result)
        sub = AsyncSubmitter(conn)

        result = sub.poll("task-abc-123")

        assert result["result"]["status"] == "running"
        assert result["result"]["progress"] == 50

    def test_poll_completed_task(self):
        """poll() returns completed status when task is done."""
        poll_result = {
            "success": True,
            "result": {"status": "completed", "data": {"compiled": True}},
        }
        conn = _make_async_mock_connection(poll_result=poll_result)
        sub = AsyncSubmitter(conn)

        result = sub.poll("task-abc-123")

        assert result["result"]["status"] == "completed"


class TestAsyncSubmitterWait:
    """Tests for AsyncSubmitter.wait() — polls until completion or timeout."""

    @patch("ue_cli_tool.pipeline.time")
    def test_wait_polls_until_completion(self, mock_time):
        """wait() polls repeatedly until status is 'completed'."""
        # Simulate time progression: start=0, then 1, 2, 3 (within timeout)
        mock_time.time.side_effect = [0, 1, 2, 3]
        mock_time.sleep = MagicMock()

        conn = MagicMock()
        call_count = 0

        def send_raw_dict_side_effect(cmd, params, **kwargs):
            nonlocal call_count
            if cmd == "get_task_result":
                call_count += 1
                if call_count < 3:
                    return {"success": True, "result": {"status": "running"}}
                return {
                    "success": True,
                    "result": {"status": "completed", "data": "done"},
                }
            return {"success": True}

        conn.send_raw_dict.side_effect = send_raw_dict_side_effect
        sub = AsyncSubmitter(conn)

        result = sub.wait("task-abc-123", timeout=10.0, interval=1.0)

        assert result["result"]["status"] == "completed"
        assert mock_time.sleep.call_count >= 2

    @patch("ue_cli_tool.pipeline.time")
    def test_wait_returns_error_on_timeout(self, mock_time):
        """wait() returns an error dict when the task times out."""
        # time.time() returns values that exceed the deadline
        # deadline = start + timeout = 0 + 5 = 5
        # Loop: time() returns 0 (deadline calc), then 1, 3, 6 (exceeds deadline)
        mock_time.time.side_effect = [0, 1, 3, 6]
        mock_time.sleep = MagicMock()

        conn = MagicMock()
        conn.send_raw_dict.return_value = {
            "success": True,
            "result": {"status": "running"},
        }
        sub = AsyncSubmitter(conn)

        result = sub.wait("task-abc-123", timeout=5.0, interval=1.0)

        assert result["success"] is False
        assert "timed out" in result["error"]

    @patch("ue_cli_tool.pipeline.time")
    def test_wait_returns_on_failed_status(self, mock_time):
        """wait() returns immediately when task status is 'failed'."""
        mock_time.time.side_effect = [0, 1]
        mock_time.sleep = MagicMock()

        conn = MagicMock()
        conn.send_raw_dict.return_value = {
            "success": True,
            "result": {"status": "failed", "error": "compilation error"},
        }
        sub = AsyncSubmitter(conn)

        result = sub.wait("task-abc-123", timeout=60.0)

        assert result["result"]["status"] == "failed"
        # Should return after first poll, no sleep needed
        mock_time.sleep.assert_not_called()

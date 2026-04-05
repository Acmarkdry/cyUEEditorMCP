"""
Request pipeline for UE MCP Bridge.

Provides:
- BatchContext: collect multiple commands, execute in a single TCP round-trip
- AsyncSubmitter: submit long-running commands for async execution + polling

Usage (batch):
    from ue_editor_mcp.pipeline import BatchContext

    with BatchContext(connection) as batch:
        batch.create_blueprint(name="BP_Test", parent_class="Actor")
        batch.add_component_to_blueprint(
            blueprint_name="BP_Test",
            component_type="StaticMeshComponent",
            component_name="Mesh",
        )
    print(batch.results)

Usage (async):
    from ue_editor_mcp.pipeline import AsyncSubmitter

    async_sub = AsyncSubmitter(connection)
    task_id = async_sub.submit("compile_blueprint", {"blueprint_name": "BP_Test"})
    result = async_sub.wait(task_id, timeout=60)
"""

from __future__ import annotations

import logging
import time
from typing import Any, Callable, Optional

from .connection import PersistentUnrealConnection, TimeoutTier

logger = logging.getLogger(__name__)

_MAX_BATCH_SIZE = 50


# ═══════════════════════════════════════════════════════════════════
# Batch Context
# ═══════════════════════════════════════════════════════════════════


class BatchContext:
    """Collect commands and execute them in a single ``batch_execute`` call.

    Supports two usage patterns:

    1. Context manager (auto-execute on exit)::

        with BatchContext(conn) as b:
            b.create_blueprint(name="BP_Test", parent_class="Actor")
            b.compile_blueprint(blueprint_name="BP_Test")
        print(b.results)

    2. Manual execution::

        b = BatchContext(conn)
        b.add("create_blueprint", {"name": "BP_Test", "parent_class": "Actor"})
        b.add("compile_blueprint", {"blueprint_name": "BP_Test"})
        results = b.execute()
    """

    def __init__(
        self,
        connection: PersistentUnrealConnection,
        *,
        continue_on_error: bool = False,
        transactional: bool = False,
    ):
        self._conn = connection
        self._commands: list[dict[str, Any]] = []
        self._continue_on_error = continue_on_error
        self._transactional = transactional
        self._results: list[dict[str, Any]] = []
        self._executed = False

    # ── Adding commands ─────────────────────────────────────────────

    def add(self, command: str, params: Optional[dict[str, Any]] = None) -> int:
        """Add a command to the batch queue.

        Args:
            command: C++ command type (e.g. "create_blueprint")
            params: Command parameters

        Returns:
            Index of the added command in the queue.

        Raises:
            RuntimeError: If batch has already been executed.
            ValueError: If queue exceeds max batch size.
        """
        if self._executed:
            raise RuntimeError("Batch already executed. Create a new BatchContext.")
        if len(self._commands) >= _MAX_BATCH_SIZE:
            raise ValueError(
                f"Batch queue full (max {_MAX_BATCH_SIZE}). "
                f"Execute current batch first or split into multiple batches."
            )

        entry = {"type": command}
        if params:
            entry["params"] = {k: v for k, v in params.items() if v is not None}
        self._commands.append(entry)
        return len(self._commands) - 1

    def __getattr__(self, name: str) -> Callable[..., int]:
        """Dynamic method: ``batch.create_blueprint(...)`` → add to queue."""
        if name.startswith("_"):
            raise AttributeError(name)

        def queued_call(**kwargs: Any) -> int:
            params = {k: v for k, v in kwargs.items() if v is not None}
            return self.add(name, params or None)

        queued_call.__name__ = name
        queued_call.__doc__ = f"Queue '{name}' command for batch execution."
        return queued_call

    # ── Execution ───────────────────────────────────────────────────

    def execute(self) -> list[dict[str, Any]]:
        """Execute all queued commands in a single ``batch_execute`` call.

        Returns:
            List of result dicts, one per command.
        """
        if self._executed:
            return self._results
        if not self._commands:
            self._executed = True
            return []

        batch_params: dict[str, Any] = {
            "commands": self._commands,
            "continue_on_error": self._continue_on_error,
        }
        if self._transactional:
            batch_params["transactional"] = True

        logger.info(
            f"Executing batch: {len(self._commands)} commands "
            f"(continue_on_error={self._continue_on_error}, "
            f"transactional={self._transactional})"
        )

        result = self._conn.send_raw_dict(
            "batch_execute", batch_params, timeout_tier=TimeoutTier.EXTRA_SLOW
        )

        self._results = result.get("results", [result])
        self._executed = True
        return self._results

    # ── Properties ──────────────────────────────────────────────────

    @property
    def results(self) -> list[dict[str, Any]]:
        """Results from the last execute() call."""
        return self._results

    @property
    def pending_count(self) -> int:
        """Number of commands waiting to be executed."""
        return len(self._commands) if not self._executed else 0

    @property
    def is_executed(self) -> bool:
        """Whether this batch has been executed."""
        return self._executed

    # ── Context manager ─────────────────────────────────────────────

    def __enter__(self) -> BatchContext:
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> bool:
        if exc_type is None and not self._executed:
            self.execute()
        return False

    def __len__(self) -> int:
        return len(self._commands)

    def __repr__(self) -> str:
        status = "executed" if self._executed else f"{len(self._commands)} pending"
        return f"<BatchContext {status}>"


# ═══════════════════════════════════════════════════════════════════
# Async Submitter
# ═══════════════════════════════════════════════════════════════════


class AsyncSubmitter:
    """Submit commands for async execution and poll for results.

    Use for long-running commands (compile, large batches) where you
    don't want to block the calling thread.

    Usage::

        sub = AsyncSubmitter(connection)
        task_id = sub.submit("compile_blueprint", {"blueprint_name": "BP_Heavy"})

        # ... do other work ...

        result = sub.wait(task_id, timeout=120)
    """

    def __init__(self, connection: PersistentUnrealConnection):
        self._conn = connection

    def submit(self, command: str, params: Optional[dict[str, Any]] = None) -> str:
        """Submit a command for async execution.

        Args:
            command: C++ command type
            params: Command parameters

        Returns:
            task_id string for polling.
        """
        result = self._conn.send_raw_dict(
            "async_execute",
            {"command": command, "params": params or {}},
            timeout_tier=TimeoutTier.FAST,
        )
        task_id = result.get("result", {}).get("task_id", "")
        if not task_id:
            logger.error(f"async_execute returned no task_id: {result}")
        return task_id

    def poll(self, task_id: str) -> dict[str, Any]:
        """Poll for a task result.

        Returns:
            Result dict with "status" field: "submitted", "running", "completed", "failed".
        """
        return self._conn.send_raw_dict(
            "get_task_result",
            {"task_id": task_id},
            timeout_tier=TimeoutTier.FAST,
        )

    def wait(
        self,
        task_id: str,
        *,
        timeout: float = 120.0,
        interval: float = 0.5,
    ) -> dict[str, Any]:
        """Block until a task completes or times out.

        Args:
            task_id: Task ID from submit()
            timeout: Max seconds to wait
            interval: Seconds between polls

        Returns:
            Final result dict.
        """
        deadline = time.time() + timeout
        while time.time() < deadline:
            result = self.poll(task_id)
            status = result.get("result", {}).get("status", "")
            if status in ("completed", "failed"):
                return result
            time.sleep(interval)

        return {
            "success": False,
            "error": f"Task {task_id} timed out after {timeout}s",
        }

    def submit_and_wait(
        self,
        command: str,
        params: Optional[dict[str, Any]] = None,
        *,
        timeout: float = 120.0,
    ) -> dict[str, Any]:
        """Convenience: submit + wait in one call."""
        task_id = self.submit(command, params)
        if not task_id:
            return {"success": False, "error": "Failed to submit async task"}
        return self.wait(task_id, timeout=timeout)

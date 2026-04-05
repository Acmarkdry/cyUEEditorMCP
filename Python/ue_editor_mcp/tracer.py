"""
Request tracing for UE MCP Bridge.

Provides a context-manager based tracer that automatically records
request timing and outcome to the MetricsCollector.

Usage:
    tracer = RequestTracer(metrics_collector)
    with tracer.trace("compile_blueprint") as ctx:
        result = connection.send_command("compile_blueprint", params)
        ctx["success"] = result.success
        ctx["error"] = result.error
"""

from __future__ import annotations

import time
import logging
from contextlib import contextmanager
from typing import Any, Generator, Optional
from uuid import uuid4

from .metrics import MetricsCollector, RequestMetrics

logger = logging.getLogger(__name__)


class RequestTracer:
    """Traces individual requests, recording metrics automatically."""

    def __init__(self, metrics: MetricsCollector):
        self._metrics = metrics

    @contextmanager
    def trace(
        self, command: str, *, request_id: Optional[str] = None
    ) -> Generator[dict[str, Any], None, None]:
        """Context manager that records request metrics on exit.

        Yields a mutable dict — set ``ctx["success"]`` and optionally
        ``ctx["error"]`` before the block exits.

        Example::

            with tracer.trace("compile_blueprint") as ctx:
                result = do_something()
                ctx["success"] = result.ok
                ctx["error"] = result.error_msg
        """
        rid = request_id or f"{command}_{uuid4().hex[:8]}"
        ctx: dict[str, Any] = {"success": False, "error": None, "request_id": rid}
        start = time.perf_counter()

        try:
            yield ctx
        except Exception as exc:
            ctx["success"] = False
            ctx["error"] = str(exc)
            raise
        finally:
            end = time.perf_counter()
            self._metrics.record(
                RequestMetrics(
                    command=command,
                    start_time=start,
                    end_time=end,
                    success=ctx["success"],
                    error=ctx.get("error"),
                    request_id=rid,
                )
            )
            duration_ms = (end - start) * 1000
            if duration_ms > 5000:
                logger.warning(f"Slow request: {command} took {duration_ms:.0f}ms (id={rid})")

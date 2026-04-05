"""
Metrics collection for UE MCP Bridge requests.

Provides:
- RequestMetrics: per-request timing/success data
- MetricsCollector: thread-safe aggregation with summary statistics
- Global singleton access via get_metrics() / reset_metrics()

Zero external dependencies — uses only stdlib (collections, threading, dataclasses).
"""

from __future__ import annotations

import threading
import time
from collections import defaultdict, deque
from dataclasses import dataclass, field
from typing import Any, Optional
from uuid import uuid4

# ═══════════════════════════════════════════════════════════════════
# Data Model
# ═══════════════════════════════════════════════════════════════════


@dataclass
class RequestMetrics:
    """Metrics for a single request."""

    command: str
    start_time: float
    end_time: float
    success: bool
    error: Optional[str] = None
    request_id: str = field(default_factory=lambda: uuid4().hex[:8])

    @property
    def duration_ms(self) -> float:
        return (self.end_time - self.start_time) * 1000


# ═══════════════════════════════════════════════════════════════════
# Collector
# ═══════════════════════════════════════════════════════════════════


class MetricsCollector:
    """Thread-safe metrics aggregation.

    Records individual request metrics and provides summary statistics
    including success rate, latency percentiles, and per-command breakdowns.
    """

    def __init__(self, max_history: int = 1000):
        self._history: deque[RequestMetrics] = deque(maxlen=max_history)
        self._total_count: int = 0
        self._total_errors: int = 0
        self._counters: dict[str, int] = defaultdict(int)
        self._error_counters: dict[str, int] = defaultdict(int)
        self._lock = threading.Lock()

    # ── Recording ───────────────────────────────────────────────────

    def record(self, metrics: RequestMetrics) -> None:
        """Record a completed request."""
        with self._lock:
            self._history.append(metrics)
            self._total_count += 1
            self._counters[metrics.command] += 1
            if not metrics.success:
                self._total_errors += 1
                self._error_counters[metrics.command] += 1

    def record_simple(
        self,
        command: str,
        duration_ms: float,
        success: bool,
        error: Optional[str] = None,
    ) -> None:
        """Record from raw values (used by connection callback hook)."""
        now = time.perf_counter()
        self.record(
            RequestMetrics(
                command=command,
                start_time=now - duration_ms / 1000,
                end_time=now,
                success=success,
                error=error,
            )
        )

    # ── Queries ─────────────────────────────────────────────────────

    def get_summary(self, last_n: int = 100) -> dict[str, Any]:
        """Return summary statistics over the most recent *last_n* requests."""
        with self._lock:
            recent = list(self._history)[-last_n:]

        if not recent:
            return {
                "total": 0,
                "total_all_time": self._total_count,
                "errors_all_time": self._total_errors,
            }

        durations = sorted(m.duration_ms for m in recent)
        successes = sum(1 for m in recent if m.success)
        n = len(durations)

        return {
            "total": n,
            "total_all_time": self._total_count,
            "errors_all_time": self._total_errors,
            "success_rate": round(successes / n * 100, 1),
            "avg_ms": round(sum(durations) / n, 1),
            "min_ms": round(durations[0], 1),
            "p50_ms": round(durations[n // 2], 1),
            "p95_ms": round(durations[min(int(n * 0.95), n - 1)], 1),
            "p99_ms": round(durations[min(int(n * 0.99), n - 1)], 1),
            "max_ms": round(durations[-1], 1),
            "by_command": dict(self._counters),
            "errors_by_command": {k: v for k, v in self._error_counters.items() if v > 0},
        }

    def get_recent(self, last_n: int = 20) -> list[dict[str, Any]]:
        """Return the most recent *last_n* request records."""
        with self._lock:
            recent = list(self._history)[-last_n:]
        return [
            {
                "command": m.command,
                "request_id": m.request_id,
                "duration_ms": round(m.duration_ms, 1),
                "success": m.success,
                "error": m.error,
                "timestamp": m.start_time,
            }
            for m in recent
        ]

    def get_slow_requests(
        self, threshold_ms: float = 1000, last_n: int = 20
    ) -> list[dict[str, Any]]:
        """Return recent requests slower than *threshold_ms*."""
        with self._lock:
            slow = [m for m in self._history if m.duration_ms >= threshold_ms]
        return [
            {
                "command": m.command,
                "request_id": m.request_id,
                "duration_ms": round(m.duration_ms, 1),
                "success": m.success,
                "error": m.error,
            }
            for m in slow[-last_n:]
        ]

    # ── Management ──────────────────────────────────────────────────

    def reset(self) -> None:
        """Clear all metrics."""
        with self._lock:
            self._history.clear()
            self._total_count = 0
            self._total_errors = 0
            self._counters.clear()
            self._error_counters.clear()

    @property
    def total_count(self) -> int:
        return self._total_count

    @property
    def error_count(self) -> int:
        return self._total_errors


# ═══════════════════════════════════════════════════════════════════
# Global singleton
# ═══════════════════════════════════════════════════════════════════

_metrics: MetricsCollector | None = None


def get_metrics() -> MetricsCollector:
    """Get or create the global MetricsCollector instance."""
    global _metrics
    if _metrics is None:
        _metrics = MetricsCollector()
    return _metrics


def reset_metrics() -> None:
    """Reset the global MetricsCollector."""
    global _metrics
    if _metrics:
        _metrics.reset()
    _metrics = None

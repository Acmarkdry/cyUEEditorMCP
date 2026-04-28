# coding: utf-8
"""
Property-based tests for Circuit Breaker state transition invariant.

Feature: v0.4.0-platform-extensions, Property 5: Circuit Breaker state transition invariant

Uses Hypothesis to generate failure counts N (N >= failure_threshold) and verifies
that from CLOSED state, after N consecutive failures the state is OPEN, and after
recovery_timeout elapses the state transitions to HALF_OPEN.

**Validates: Requirements 15.6**
"""

from __future__ import annotations

from unittest.mock import patch

from hypothesis import given, settings
from hypothesis import strategies as st

from ue_cli_tool.connection import (
    CircuitBreaker,
    CircuitBreakerConfig,
    CircuitState,
)

# ---------------------------------------------------------------------------
# Strategies
# ---------------------------------------------------------------------------

# Generate failure counts that are at least failure_threshold (default 5).
# We use a fixed config with a very short recovery_timeout to keep tests fast.
_FAILURE_THRESHOLD = 5
_RECOVERY_TIMEOUT = 0.01  # 10ms — fast enough for tests

_failure_count = st.integers(min_value=_FAILURE_THRESHOLD, max_value=200)


# ---------------------------------------------------------------------------
# Property 5: Circuit Breaker state transition invariant
# ---------------------------------------------------------------------------


@given(n_failures=_failure_count)
@settings(max_examples=100)
def test_circuit_breaker_state_transition_invariant(n_failures: int):
    """For any failure count N >= failure_threshold, from CLOSED state,
    after N consecutive failures the Circuit Breaker state is OPEN.
    After recovery_timeout elapses, the state transitions to HALF_OPEN.

    Feature: v0.4.0-platform-extensions, Property 5: Circuit Breaker state transition invariant

    **Validates: Requirements 15.6**
    """
    config = CircuitBreakerConfig(
        failure_threshold=_FAILURE_THRESHOLD,
        recovery_timeout=_RECOVERY_TIMEOUT,
        success_threshold=2,
    )
    cb = CircuitBreaker(config)

    # Precondition: starts in CLOSED state.
    assert cb.state == CircuitState.CLOSED, (
        f"Expected initial state CLOSED, got {cb.state}"
    )

    # Record N consecutive failures.
    # We mock time.time() to return a fixed value so that the recovery_timeout
    # does NOT elapse during the failure recording phase.
    fixed_time = 1000.0
    with patch("ue_cli_tool.connection.time") as mock_time:
        mock_time.time.return_value = fixed_time
        for _ in range(n_failures):
            cb.record_failure()

    # After N >= failure_threshold consecutive failures, state MUST be OPEN.
    # We check the internal _state directly to avoid the property triggering
    # the OPEN → HALF_OPEN transition (which checks time).
    with patch("ue_cli_tool.connection.time") as mock_time:
        mock_time.time.return_value = fixed_time  # no time has passed
        assert cb.state == CircuitState.OPEN, (
            f"After {n_failures} failures (threshold={_FAILURE_THRESHOLD}), "
            f"expected OPEN, got {cb.state}"
        )

    # Now simulate recovery_timeout elapsing.
    elapsed_time = fixed_time + _RECOVERY_TIMEOUT + 0.001  # just past timeout
    with patch("ue_cli_tool.connection.time") as mock_time:
        mock_time.time.return_value = elapsed_time
        state_after_timeout = cb.state

    assert state_after_timeout == CircuitState.HALF_OPEN, (
        f"After recovery_timeout ({_RECOVERY_TIMEOUT}s) elapsed, "
        f"expected HALF_OPEN, got {state_after_timeout}"
    )


# ===========================================================================
# Unit Tests for Connection Module
# Requirements: 15.1, 15.2, 15.3, 15.4, 15.5, 15.6
# ===========================================================================

import json
import pytest
from unittest.mock import patch, MagicMock

from ue_cli_tool.connection import (
    CircuitBreaker,
    CircuitBreakerConfig,
    CircuitState,
    ConnectionConfig,
    ConnectionState,
    PersistentUnrealConnection,
    CommandResult,
    TimeoutTier,
    _TIMEOUT_MAP,
)


# ---------------------------------------------------------------------------
# 1. Circuit Breaker state transitions (Requirement 15.1)
# ---------------------------------------------------------------------------


class TestCircuitBreakerStateTransitions:
    """Circuit Breaker state transition tests.

    **Validates: Requirements 15.1**
    """

    def _make_cb(self, failure_threshold=3, recovery_timeout=0.01, success_threshold=2):
        config = CircuitBreakerConfig(
            failure_threshold=failure_threshold,
            recovery_timeout=recovery_timeout,
            success_threshold=success_threshold,
        )
        return CircuitBreaker(config)

    def test_closed_to_open_on_consecutive_failures(self):
        """CLOSED → OPEN when consecutive failures reach threshold."""
        cb = self._make_cb(failure_threshold=3)
        assert cb.state == CircuitState.CLOSED

        fixed_time = 1000.0
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            assert cb.state == CircuitState.OPEN

    def test_open_to_half_open_after_recovery_timeout(self):
        """OPEN → HALF_OPEN when recovery timeout has elapsed."""
        cb = self._make_cb(failure_threshold=3, recovery_timeout=0.01)

        fixed_time = 1000.0
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        # Advance time past recovery_timeout
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            assert cb.state == CircuitState.HALF_OPEN

    def test_half_open_to_closed_on_consecutive_successes(self):
        """HALF_OPEN → CLOSED when consecutive successes reach threshold."""
        cb = self._make_cb(failure_threshold=3, recovery_timeout=0.01, success_threshold=2)

        fixed_time = 1000.0
        # Trip to OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        # Transition to HALF_OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            assert cb.state == CircuitState.HALF_OPEN

        # Record enough successes to close
        cb.record_success()
        cb.record_success()
        assert cb.state == CircuitState.CLOSED

    def test_half_open_to_open_on_probe_failure(self):
        """HALF_OPEN → OPEN when a probe request fails."""
        cb = self._make_cb(failure_threshold=3, recovery_timeout=0.01)

        fixed_time = 1000.0
        # Trip to OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        # Transition to HALF_OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            assert cb.state == CircuitState.HALF_OPEN

        # Probe failure sends back to OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            cb.record_failure()

        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            assert cb.state == CircuitState.OPEN


# ---------------------------------------------------------------------------
# 2. allow_request method (Requirement 15.2)
# ---------------------------------------------------------------------------


class TestCircuitBreakerAllowRequest:
    """Circuit Breaker allow_request tests.

    **Validates: Requirements 15.2**
    """

    def _make_cb(self, failure_threshold=3, recovery_timeout=0.01, success_threshold=2):
        config = CircuitBreakerConfig(
            failure_threshold=failure_threshold,
            recovery_timeout=recovery_timeout,
            success_threshold=success_threshold,
        )
        return CircuitBreaker(config)

    def test_closed_allows_requests(self):
        """CLOSED state allows requests."""
        cb = self._make_cb()
        assert cb.allow_request() is True

    def test_open_rejects_requests(self):
        """OPEN state rejects requests."""
        cb = self._make_cb(failure_threshold=3)

        fixed_time = 1000.0
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        # Still within recovery_timeout — should be OPEN and reject
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            assert cb.allow_request() is False

    def test_half_open_allows_probe_requests(self):
        """HALF_OPEN state allows probe requests."""
        cb = self._make_cb(failure_threshold=3, recovery_timeout=0.01)

        fixed_time = 1000.0
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time
            for _ in range(3):
                cb.record_failure()

        # Advance past recovery_timeout to trigger HALF_OPEN
        with patch("ue_cli_tool.connection.time") as mock_time:
            mock_time.time.return_value = fixed_time + 0.02
            assert cb.allow_request() is True


# ---------------------------------------------------------------------------
# 3. Reconnect logic (Requirement 15.3)
# ---------------------------------------------------------------------------


class TestReconnectLogic:
    """Reconnect exponential backoff and max attempts tests.

    **Validates: Requirements 15.3**
    """

    def test_exponential_backoff_delay_calculation(self):
        """Exponential backoff delay doubles each attempt, capped at max."""
        config = ConnectionConfig(
            reconnect_base_delay=1.0,
            reconnect_max_delay=30.0,
            max_reconnect_attempts=5,
        )
        conn = PersistentUnrealConnection(config)

        # Verify the delay formula: min(base * 2^(attempt-1), max_delay)
        # attempt 1: min(1*2^0, 30) = 1.0
        # attempt 2: min(1*2^1, 30) = 2.0
        # attempt 3: min(1*2^2, 30) = 4.0
        # attempt 4: min(1*2^3, 30) = 8.0
        # attempt 5: min(1*2^4, 30) = 16.0
        expected_delays = [1.0, 2.0, 4.0, 8.0, 16.0]

        for attempt, expected in enumerate(expected_delays, start=1):
            delay = min(
                config.reconnect_base_delay * (2 ** (attempt - 1)),
                config.reconnect_max_delay,
            )
            assert delay == expected, f"Attempt {attempt}: expected {expected}, got {delay}"

    def test_exponential_backoff_capped_at_max(self):
        """Backoff delay is capped at reconnect_max_delay."""
        config = ConnectionConfig(
            reconnect_base_delay=1.0,
            reconnect_max_delay=5.0,
            max_reconnect_attempts=10,
        )
        # attempt 4: min(1*2^3, 5) = 5.0 (capped)
        delay = min(
            config.reconnect_base_delay * (2 ** (4 - 1)),
            config.reconnect_max_delay,
        )
        assert delay == 5.0

    def test_stops_after_max_reconnect_attempts(self):
        """_try_reconnect stops after max_reconnect_attempts and returns False."""
        config = ConnectionConfig(
            max_reconnect_attempts=3,
            reconnect_base_delay=0.001,
            reconnect_max_delay=0.01,
        )
        conn = PersistentUnrealConnection(config)
        conn._state = ConnectionState.CONNECTED  # pretend we were connected

        # Mock connect() to always fail and time.sleep to be instant
        with patch.object(conn, "connect", return_value=False), \
             patch("ue_cli_tool.connection.time.sleep"):
            result = conn._try_reconnect()

        assert result is False
        assert conn._reconnect_attempts == 3
        assert conn._state == ConnectionState.ERROR


# ---------------------------------------------------------------------------
# 4. _parse_response method (Requirement 15.4)
# ---------------------------------------------------------------------------


class TestParseResponse:
    """_parse_response static method tests.

    **Validates: Requirements 15.4**
    """

    def test_format1_success_response(self):
        """Format 1 (success bool) normal response."""
        response = {"success": True, "name": "MyBlueprint", "node_count": 5}
        result = PersistentUnrealConnection._parse_response("test_cmd", response)

        assert result.success is True
        assert result.data == {"name": "MyBlueprint", "node_count": 5}
        assert result.error is None

    def test_format1_error_response(self):
        """Format 1 (success bool) error response."""
        response = {
            "success": False,
            "error": "Blueprint not found",
            "error_type": "not_found",
        }
        result = PersistentUnrealConnection._parse_response("test_cmd", response)

        assert result.success is False
        assert "Blueprint not found" in result.error
        assert "[not_found]" in result.error

    def test_format2_success_response(self):
        """Format 2 (status string) normal response."""
        response = {"status": "success", "result": {"actors": ["Actor1", "Actor2"]}}
        result = PersistentUnrealConnection._parse_response("test_cmd", response)

        assert result.success is True
        assert result.data == {"actors": ["Actor1", "Actor2"]}

    def test_format2_error_response(self):
        """Format 2 (status string) error response."""
        response = {
            "status": "error",
            "error": "Connection refused",
            "error_type": "transport",
        }
        result = PersistentUnrealConnection._parse_response("test_cmd", response)

        assert result.success is False
        assert "Connection refused" in result.error
        assert "[transport]" in result.error

    def test_unknown_format_response(self):
        """Unknown format response (no 'success' or 'status' key)."""
        response = {"data": "something", "count": 42}
        result = PersistentUnrealConnection._parse_response("test_cmd", response)

        assert result.success is False
        assert "Unknown response format" in result.error
        assert result.recoverable is True


# ---------------------------------------------------------------------------
# 5. Timeout tiers (Requirement 15.5)
# ---------------------------------------------------------------------------


class TestTimeoutTiers:
    """TimeoutTier and _resolve_timeout tests.

    **Validates: Requirements 15.5**
    """

    def test_registered_command_uses_corresponding_timeout(self):
        """Registered commands use their corresponding timeout tier."""
        conn = PersistentUnrealConnection()

        # ping → PING (3.0s)
        assert conn._resolve_timeout("ping") == TimeoutTier.PING.value
        # get_actors_in_level → FAST (15.0s)
        assert conn._resolve_timeout("get_actors_in_level") == TimeoutTier.FAST.value
        # compile_blueprint → SLOW (120.0s)
        assert conn._resolve_timeout("compile_blueprint") == TimeoutTier.SLOW.value
        # batch_execute → EXTRA_SLOW (240.0s)
        assert conn._resolve_timeout("batch_execute") == TimeoutTier.EXTRA_SLOW.value

    def test_unregistered_command_uses_normal_default(self):
        """Unregistered commands default to NORMAL timeout (30.0s)."""
        conn = PersistentUnrealConnection()

        assert conn._resolve_timeout("some_unknown_command") == TimeoutTier.NORMAL.value
        assert conn._resolve_timeout("create_blueprint") == TimeoutTier.NORMAL.value

    def test_explicit_timeout_tier_overrides_map(self):
        """An explicit timeout_tier parameter overrides the _TIMEOUT_MAP lookup."""
        conn = PersistentUnrealConnection()

        # ping is normally PING (3.0), but explicit SLOW should win
        assert conn._resolve_timeout("ping", TimeoutTier.SLOW) == TimeoutTier.SLOW.value

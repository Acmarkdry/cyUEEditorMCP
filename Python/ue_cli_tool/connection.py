# coding: utf-8
"""
Persistent connection to Unreal Engine MCP Bridge.

Unlike the original implementation that reconnected for each command,
this maintains a persistent socket with heartbeat and auto-reconnect.

Enhanced with:
- Circuit Breaker pattern for resilient failure handling
- Tiered timeouts per command category
- Metrics hook for observability
- send_raw_dict() convenience for SDK consumers
"""

import json
import socket
import threading
import time
import logging
from typing import Any, Callable, Optional
from dataclasses import dataclass, field
from enum import Enum

logger = logging.getLogger(__name__)


# ══════════════════════════════════?
# Enums & Config
# ══════════════════════════════════?


class ConnectionState(Enum):
	"""Connection lifecycle states."""

	DISCONNECTED = "disconnected"
	CONNECTING = "connecting"
	CONNECTED = "connected"
	RECONNECTING = "reconnecting"
	ERROR = "error"


class CircuitState(Enum):
	"""Circuit Breaker states."""

	CLOSED = "closed"  # Normal —?all requests pass through
	OPEN = "open"  # Tripped —?all requests fail-fast
	HALF_OPEN = "half_open"  # Probing —?allow one request to test recovery


class TimeoutTier(Enum):
	"""Tiered timeouts for different command categories."""

	PING = 3.0
	FAST = 15.0  # Simple queries (get_actors_in_level, list_assets)
	NORMAL = 30.0  # Standard mutations (create_blueprint, add_node)
	SLOW = 120.0  # Compile, python exec
	EXTRA_SLOW = 240.0  # Large batch operations


# Command →?timeout tier mapping.  Unlisted commands default to NORMAL.
_TIMEOUT_MAP: dict[str, TimeoutTier] = {
	"ping": TimeoutTier.PING,
	"get_context": TimeoutTier.FAST,
	"get_actors_in_level": TimeoutTier.FAST,
	"find_actors_by_name": TimeoutTier.FAST,
	"list_assets": TimeoutTier.FAST,
	"get_blueprint_summary": TimeoutTier.FAST,
	"get_viewport_transform": TimeoutTier.FAST,
	"find_blueprint_nodes": TimeoutTier.FAST,
	"get_node_pins": TimeoutTier.FAST,
	"compile_blueprint": TimeoutTier.SLOW,
	"compile_material": TimeoutTier.SLOW,
	"exec_python": TimeoutTier.SLOW,
	"batch_execute": TimeoutTier.EXTRA_SLOW,
	"async_execute": TimeoutTier.FAST,  # submission is fast; execution is async
	"get_task_result": TimeoutTier.FAST,
}


@dataclass
class CircuitBreakerConfig:
	"""Configuration for the Circuit Breaker."""

	failure_threshold: int = 5  # Consecutive failures to trip OPEN
	recovery_timeout: float = 10.0  # Seconds in OPEN before probing HALF_OPEN
	success_threshold: int = 2  # Consecutive successes in HALF_OPEN to close


@dataclass
class ConnectionConfig:
	"""Configuration for the Unreal connection."""

	host: str = "127.0.0.1"
	port: int = 55558  # New UEEditorMCP plugin port
	timeout: float = 120.0
	heartbeat_interval: float = 5.0
	max_reconnect_attempts: int = 5
	reconnect_base_delay: float = 1.0
	reconnect_max_delay: float = 30.0
	circuit_breaker: CircuitBreakerConfig = field(default_factory=CircuitBreakerConfig)


@dataclass
class CommandResult:
	"""Result of a command execution."""

	success: bool
	data: dict = field(default_factory=dict)
	error: Optional[str] = None
	recoverable: bool = True

	def to_dict(self) -> dict:
		result = {"success": self.success}
		if self.success:
			result.update(self.data)
		else:
			result["error"] = self.error
			result["recoverable"] = self.recoverable
			# Preserve structured data on failure too (e.g. batch partial
			# failures carry results/total/executed/failed fields).
			if self.data:
				result.update(self.data)
		return result


# ══════════════════════════════════?
# Circuit Breaker
# ══════════════════════════════════?


class CircuitBreaker:
	"""Lightweight circuit breaker to prevent cascading failures.

	State transitions:
		CLOSED ────(failures >= threshold)────→?OPEN
		OPEN ────(recovery_timeout elapsed)────→?HALF_OPEN
		HALF_OPEN ────(success >= threshold)────→?CLOSED
		HALF_OPEN ────(any failure)────→?OPEN
	"""

	def __init__(self, config: Optional[CircuitBreakerConfig] = None):
		self._config = config or CircuitBreakerConfig()
		self._state = CircuitState.CLOSED
		self._consecutive_failures = 0
		self._consecutive_successes = 0
		self._last_failure_time: float = 0.0
		self._lock = threading.Lock()

	@property
	def state(self) -> CircuitState:
		with self._lock:
			if (
				self._state == CircuitState.OPEN
				and time.time() - self._last_failure_time
				>= self._config.recovery_timeout
			):
				self._state = CircuitState.HALF_OPEN
				self._consecutive_successes = 0
				logger.info("Circuit breaker →?HALF_OPEN (probing)")
			return self._state

	def allow_request(self) -> bool:
		"""Check if a request is allowed through the breaker."""
		state = self.state  # triggers OPEN→→ALF_OPEN transition if timeout elapsed
		if state == CircuitState.CLOSED:
			return True
		if state == CircuitState.HALF_OPEN:
			return True  # allow probe request
		# OPEN —?fail fast
		return False

	def record_success(self) -> None:
		"""Record a successful request."""
		with self._lock:
			self._consecutive_failures = 0
			if self._state == CircuitState.HALF_OPEN:
				self._consecutive_successes += 1
				if self._consecutive_successes >= self._config.success_threshold:
					self._state = CircuitState.CLOSED
					logger.info("Circuit breaker →?CLOSED (recovered)")
			else:
				self._consecutive_successes += 1

	def record_failure(self) -> None:
		"""Record a failed request."""
		with self._lock:
			self._consecutive_successes = 0
			self._consecutive_failures += 1
			self._last_failure_time = time.time()
			if self._state == CircuitState.HALF_OPEN:
				self._state = CircuitState.OPEN
				logger.warning("Circuit breaker →?OPEN (probe failed)")
			elif (
				self._state == CircuitState.CLOSED
				and self._consecutive_failures >= self._config.failure_threshold
			):
				self._state = CircuitState.OPEN
				logger.warning(
					f"Circuit breaker →?OPEN (threshold {self._config.failure_threshold} reached)"
				)

	def reset(self) -> None:
		"""Force reset to CLOSED state."""
		with self._lock:
			self._state = CircuitState.CLOSED
			self._consecutive_failures = 0
			self._consecutive_successes = 0
			self._last_failure_time = 0.0

	def get_status(self) -> dict[str, Any]:
		"""Return current status for diagnostics."""
		with self._lock:
			return {
				"state": self._state.value,
				"consecutive_failures": self._consecutive_failures,
				"consecutive_successes": self._consecutive_successes,
				"last_failure_time": self._last_failure_time,
				"config": {
					"failure_threshold": self._config.failure_threshold,
					"recovery_timeout": self._config.recovery_timeout,
					"success_threshold": self._config.success_threshold,
				},
			}


# ══════════════════════════════════?
# Persistent Connection
# ══════════════════════════════════?


class PersistentUnrealConnection:
	"""
	Maintains a persistent connection to the Unreal MCP Bridge.

	Features:
	- Keeps socket alive between commands (no reconnect per command)
	- Heartbeat ping every N seconds to detect stale connections
	- Auto-reconnect with exponential backoff on failure
	- Circuit Breaker to prevent cascading failures
	- Tiered timeouts per command category
	- Metrics callback hook for observability
	- Thread-safe command execution
	"""

	def __init__(self, config: Optional[ConnectionConfig] = None):
		self.config = config or ConnectionConfig()
		self._socket: Optional[socket.socket] = None
		self._state = ConnectionState.DISCONNECTED
		self._lock = threading.RLock()
		self._heartbeat_thread: Optional[threading.Thread] = None
		self._stop_heartbeat = threading.Event()
		self._last_activity = time.time()
		self._reconnect_attempts = 0
		# Circuit Breaker
		self._circuit = CircuitBreaker(self.config.circuit_breaker)
		# Diagnostics: consecutive command failure tracking
		self._consecutive_cmd_failures: int = 0
		self._last_heartbeat_success: bool = True
		self._last_error: Optional[str] = None
		self._last_error_time: float = 0.0
		# Optional callback: (new_state: str, old_state: str, timestamp: str) -> None
		self.on_state_change: Optional[Callable[[str, str, str], None]] = None
		# Optional metrics callback: (command, duration_ms, success, error) -> None
		self.on_request_complete: Optional[
			Callable[[str, float, bool, Optional[str]], None]
		] = None

	@property
	def state(self) -> ConnectionState:
		"""Current connection state."""
		return self._state

	@property
	def is_connected(self) -> bool:
		"""Whether actively connected to Unreal."""
		return self._state == ConnectionState.CONNECTED and self._socket is not None

	def connect(self) -> bool:
		"""
		Establish connection to Unreal Engine.

		Returns:
			True if connection successful, False otherwise.
		"""
		with self._lock:
			if self._state == ConnectionState.CONNECTED:
				return True

			self._state = ConnectionState.CONNECTING

			try:
				self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
				self._socket.settimeout(self.config.timeout)
				self._socket.connect((self.config.host, self.config.port))

				old_state = self._state
				self._state = ConnectionState.CONNECTED
				self._reconnect_attempts = 0
				self._last_activity = time.time()

				# Start heartbeat thread
				self._start_heartbeat()

				logger.info(
					f"Connected to Unreal at {self.config.host}:{self.config.port}"
				)
				self._fire_state_change("alive", old_state.value)
				return True

			except (socket.error, socket.timeout, ConnectionRefusedError) as e:
				self._state = ConnectionState.ERROR
				logger.error(f"Failed to connect to Unreal: {e}")
				self._cleanup_socket()
				return False

	def disconnect(self):
		"""Gracefully disconnect from Unreal."""
		with self._lock:
			self._stop_heartbeat.set()
			if self._heartbeat_thread:
				self._heartbeat_thread.join(timeout=2.0)

			old_state = self._state

			# Send close command if connected
			if self._socket and self._state == ConnectionState.CONNECTED:
				try:
					self._send_raw({"type": "close"})
				except Exception:
					pass

			self._cleanup_socket()
			self._state = ConnectionState.DISCONNECTED
			logger.info("Disconnected from Unreal")
			self._fire_state_change("disconnected", old_state.value)

	# ──── Timeout helpers ──────────────────────────────────────────────────────────────────────────────────────────

	def _resolve_timeout(
		self, command_type: str, timeout_tier: Optional[TimeoutTier] = None
	) -> float:
		"""Resolve the effective timeout for a command."""
		if timeout_tier is not None:
			return timeout_tier.value
		tier = _TIMEOUT_MAP.get(command_type, TimeoutTier.NORMAL)
		return tier.value

	def _apply_timeout(self, timeout_seconds: float) -> float:
		"""Temporarily set socket timeout, returning the previous value."""
		prev = self.config.timeout
		if self._socket:
			self._socket.settimeout(timeout_seconds)
		return prev

	def _restore_timeout(self, prev: float) -> None:
		"""Restore previous socket timeout."""
		if self._socket:
			self._socket.settimeout(prev)

	# ──── Metrics hook ────────────────────────────────────────────────────────────────────────────────────────────────

	def _fire_request_complete(
		self, command: str, duration_ms: float, success: bool, error: Optional[str]
	) -> None:
		cb = self.on_request_complete
		if cb is not None:
			try:
				cb(command, duration_ms, success, error)
			except Exception:
				logger.warning("on_request_complete callback failed", exc_info=True)

	# ──── Circuit Breaker access ────────────────────────────────────────────────────────────────────────────

	@property
	def circuit_breaker(self) -> CircuitBreaker:
		"""Access the circuit breaker for diagnostics."""
		return self._circuit

	# ──── Core send ──────────────────────────────────────────────────────────────────────────────────────────────────────

	def send_command(
		self,
		command_type: str,
		params: Optional[dict] = None,
		*,
		timeout_tier: Optional[TimeoutTier] = None,
	) -> CommandResult:
		"""
		Send a command to Unreal and wait for response.

		Args:
			command_type: The command type (e.g., "create_blueprint", "ping")
			params: Optional parameters for the command
			timeout_tier: Optional explicit timeout tier (auto-resolved if omitted)

		Returns:
			CommandResult with success/failure and data/error
		"""
		# Circuit Breaker gate
		if not self._circuit.allow_request():
			cb_status = self._circuit.get_status()
			logger.warning(
				f"Circuit breaker OPEN —?rejecting '{command_type}' (failures={cb_status['consecutive_failures']})"
			)
			return CommandResult(
				success=False,
				error=f"Circuit breaker is OPEN. Too many consecutive failures ({cb_status['consecutive_failures']}). "
				f"Will probe again in {cb_status['config']['recovery_timeout']}s.",
				recoverable=True,
			)

		t0 = time.perf_counter()
		result = self._send_impl(command_type, params, timeout_tier)
		elapsed_ms = (time.perf_counter() - t0) * 1000

		# Record to circuit breaker (only for transport-level success/failure)
		if result.success:
			self._circuit.record_success()
			self._consecutive_cmd_failures = 0
		elif result.error and any(
			kw in (result.error or "")
			for kw in ("timed out", "Connection lost", "Socket", "not connected")
		):
			# Only transport-level errors trip the breaker, not business-logic errors
			self._circuit.record_failure()
			self._consecutive_cmd_failures += 1
			self._last_error = result.error
			self._last_error_time = time.time()
		else:
			# Business-logic failure (e.g., "blueprint not found") —?still a success
			# for the transport layer, so record success for the breaker.
			self._circuit.record_success()
			self._consecutive_cmd_failures += 1
			self._last_error = result.error
			self._last_error_time = time.time()

		# Attach health warning when consecutive failures reach 3
		if self._consecutive_cmd_failures >= 3:
			result.data["_health_warning"] = (
				f"Warning: {self._consecutive_cmd_failures} consecutive command failures. "
				f"Connection may be unstable. Last error: {self._last_error}"
			)

		# Fire metrics hook
		self._fire_request_complete(
			command_type, elapsed_ms, result.success, result.error
		)

		return result

	def _send_impl(
		self,
		command_type: str,
		params: Optional[dict] = None,
		timeout_tier: Optional[TimeoutTier] = None,
	) -> CommandResult:
		"""Internal: actual send/receive with timeout management."""
		with self._lock:
			# Ensure connected
			if not self.is_connected:
				if not self._try_reconnect():
					return CommandResult(
						success=False,
						error="Not connected to Unreal and reconnect failed",
						recoverable=True,
					)

			# Apply tiered timeout
			effective_timeout = self._resolve_timeout(command_type, timeout_tier)
			prev_timeout = self._apply_timeout(effective_timeout)

			command = {"type": command_type}
			if params:
				command["params"] = params

			try:
				# Log outgoing command (truncate large params)
				params_preview = json.dumps(params)[:200] if params else "none"
				logger.debug(
					f">>> Sending command '{command_type}' with params: {params_preview}"
				)

				# Send command
				self._send_raw(command)

				# Receive response
				response = self._receive_raw()
				self._last_activity = time.time()

				# Log incoming response (truncate large responses)
				if response:
					response_preview = json.dumps(response)[:500]
					logger.warning(f"<<< [{command_type}] Response: {response_preview}")

				if response is None:
					# Connection died, try reconnect
					old_state = self._state
					self._state = ConnectionState.ERROR
					self._fire_state_change("crashed", old_state.value)
					if self._try_reconnect():
						# Retry the command once
						self._restore_timeout(prev_timeout)
						return self._send_impl(command_type, params, timeout_tier)
					return CommandResult(
						success=False,
						error="Connection lost and reconnect failed",
						recoverable=True,
					)

				return self._parse_response(command_type, response)

			except socket.timeout:
				logger.warning(
					f"Command '{command_type}' timed out after {effective_timeout}s"
				)
				return CommandResult(
					success=False,
					error=f"Command '{command_type}' timed out after {effective_timeout}s",
					recoverable=True,
				)
			except (socket.error, BrokenPipeError, ConnectionResetError) as e:
				logger.error(f"Socket error during command '{command_type}': {e}")
				old_state = self._state
				self._state = ConnectionState.ERROR
				self._cleanup_socket()
				self._fire_state_change("crashed", old_state.value)
				return CommandResult(success=False, error=str(e), recoverable=True)
			finally:
				self._restore_timeout(prev_timeout)

	# ──── Response parsing (extracted from send_command) ────────────────────────────

	@staticmethod
	def _parse_response(command_type: str, response: dict) -> CommandResult:
		"""Parse a raw JSON response into a CommandResult.

		Handles two response formats:
		- Format 1 (EditorAction): {"success": true, ...data...}
		- Format 2 (MCPBridge legacy): {"status": "success", "result": {...}}

		IMPORTANT: Check "success" field FIRST because some responses
		have both "success" and "status" where "status" is a data field
		(e.g., compilation status "UpToDate"), not a success indicator.
		"""
		_ERROR_DETAIL_KEYS = (
			"errors",
			"warnings",
			"error_count",
			"warning_count",
			"name",
			"status",
			"results",
			"total",
			"executed",
			"succeeded",
			"failed",
			"compiled",
			"compile_error",
		)

		# ──── Format 1: success bool field ────
		if "success" in response:
			if response.get("success") is True:
				data = {k: v for k, v in response.items() if k != "success"}
				return CommandResult(success=True, data=data)
			else:
				error_msg = response.get(
					"error", "Unknown error (no error message in response)"
				)
				error_type = response.get("error_type", "unknown")
				error_details = {
					k: response[k] for k in _ERROR_DETAIL_KEYS if k in response
				}
				detail_str = ""
				if error_details:
					detail_str = f" | DETAILS: {json.dumps(error_details)}"
				full_error = f"[{error_type}] {error_msg}{detail_str}"
				logger.error(f"Command '{command_type}' failed: {full_error}")
				data = {
					k: v
					for k, v in response.items()
					if k not in ("success", "error", "error_type")
				}
				return CommandResult(
					success=False,
					data=data,
					error=full_error,
					recoverable=response.get("recoverable", True),
				)

		# ──── Format 2: legacy status field ────
		if "status" in response:
			if response.get("status") == "success":
				return CommandResult(success=True, data=response.get("result", {}))
			else:
				error_msg = response.get(
					"error", "Unknown error (no error message in response)"
				)
				error_type = response.get("error_type", "unknown")
				error_details = {
					k: response[k] for k in _ERROR_DETAIL_KEYS if k in response
				}
				detail_str = ""
				if error_details:
					detail_str = f" | DETAILS: {json.dumps(error_details)}"
				full_error = f"[{error_type}] {error_msg}{detail_str}"
				logger.error(f"Command '{command_type}' failed: {full_error}")
				data = {
					k: v
					for k, v in response.items()
					if k not in ("status", "error", "error_type")
				}
				return CommandResult(
					success=False,
					data=data,
					error=full_error,
					recoverable=response.get("recoverable", True),
				)

		# ──── Unknown format ────
		logger.error(
			f"Command '{command_type}' returned unknown response format: "
			f"{json.dumps(response)[:500]}"
		)
		return CommandResult(
			success=False,
			error=f"Unknown response format from Unreal. Raw: {json.dumps(response)[:500]}",
			recoverable=True,
		)

	# ──── Convenience: raw dict response (for SDK / UEBridge) ────────────────

	def send_raw_dict(
		self,
		command_type: str,
		params: Optional[dict] = None,
		*,
		timeout_tier: Optional[TimeoutTier] = None,
	) -> dict:
		"""Send command and return a plain dict (not CommandResult).

		Convenience wrapper for SDK consumers (UEBridge, CLI) that prefer
		working with raw dicts instead of CommandResult objects.
		"""
		return self.send_command(
			command_type, params, timeout_tier=timeout_tier
		).to_dict()

	# ──── Health diagnostics ────────────────────────────────────────────────────────────────────────────────────

	def get_health(self) -> dict[str, Any]:
		"""Return full health status for diagnostics."""
		return {
			"connection_state": self._state.value,
			"is_connected": self.is_connected,
			"circuit_breaker": self._circuit.get_status(),
			"last_activity": self._last_activity,
			"reconnect_attempts": self._reconnect_attempts,
			"last_heartbeat_success": self._last_heartbeat_success,
			"last_error": self._last_error,
			"last_error_time": self._last_error_time,
			"consecutive_failures": self._consecutive_cmd_failures,
			"config": {
				"host": self.config.host,
				"port": self.config.port,
				"timeout": self.config.timeout,
				"heartbeat_interval": self.config.heartbeat_interval,
			},
		}

	def ping(self) -> bool:
		"""
		Send a ping to check connection health.

		Returns:
			True if Unreal responded, False otherwise.
		"""
		result = self.send_command("ping")
		return result.success and result.data.get("pong", False)

	def get_context(self) -> CommandResult:
		"""
		Get the current editor context from Unreal.

		Returns:
			CommandResult with context data (current blueprint, graph, etc.)
		"""
		return self.send_command("get_context")

	def _send_raw(self, data: dict):
		"""Send raw JSON data over socket."""
		if not self._socket:
			raise ConnectionError("Socket not connected")

		json_str = json.dumps(data)
		message = json_str.encode("utf-8")

		# Send length prefix (4 bytes, big endian)
		length = len(message)
		self._socket.sendall(length.to_bytes(4, byteorder="big"))
		self._socket.sendall(message)

	def _receive_raw(self) -> Optional[dict]:
		"""Receive raw JSON data from socket."""
		if not self._socket:
			return None

		try:
			# Receive length prefix
			length_bytes = self._recv_exact(4)
			if not length_bytes:
				return None

			length = int.from_bytes(length_bytes, byteorder="big")

			# Sanity check length
			if length <= 0 or length > 100 * 1024 * 1024:  # Max 100MB
				logger.error(f"Invalid message length: {length}")
				return None

			# Receive message
			message_bytes = self._recv_exact(length)
			if not message_bytes:
				return None

			return json.loads(message_bytes.decode("utf-8"))

		except (json.JSONDecodeError, UnicodeDecodeError) as e:
			logger.error(f"Failed to parse response: {e}")
			return None

	def _recv_exact(self, num_bytes: int) -> Optional[bytes]:
		"""Receive exact number of bytes from socket."""
		if not self._socket:
			return None

		data = bytearray()
		while len(data) < num_bytes:
			try:
				chunk = self._socket.recv(num_bytes - len(data))
				if not chunk:
					return None  # Connection closed
				data.extend(chunk)
			except socket.timeout:
				return None
			except socket.error:
				return None

		return bytes(data)

	def _cleanup_socket(self):
		"""Clean up socket resources."""
		if self._socket:
			try:
				self._socket.close()
			except Exception:
				pass
			self._socket = None

	def _try_reconnect(self) -> bool:
		"""
		Attempt to reconnect with exponential backoff.

		Returns:
			True if reconnection successful, False otherwise.
		"""
		self._state = ConnectionState.RECONNECTING
		self._cleanup_socket()

		while self._reconnect_attempts < self.config.max_reconnect_attempts:
			self._reconnect_attempts += 1

			# Calculate delay with exponential backoff
			delay = min(
				self.config.reconnect_base_delay
				* (2 ** (self._reconnect_attempts - 1)),
				self.config.reconnect_max_delay,
			)

			logger.info(
				f"Reconnect attempt {self._reconnect_attempts}/{self.config.max_reconnect_attempts} in {delay:.1f}s"
			)
			time.sleep(delay)

			if self.connect():
				return True

		self._state = ConnectionState.ERROR
		logger.error(
			f"Failed to reconnect after {self.config.max_reconnect_attempts} attempts"
		)
		return False

	def _start_heartbeat(self):
		"""Start the heartbeat thread."""
		self._stop_heartbeat.clear()
		self._heartbeat_thread = threading.Thread(
			target=self._heartbeat_loop, daemon=True
		)
		self._heartbeat_thread.start()

	def _heartbeat_loop(self):
		"""Background thread that sends periodic pings."""
		while not self._stop_heartbeat.is_set():
			self._stop_heartbeat.wait(self.config.heartbeat_interval)

			if self._stop_heartbeat.is_set():
				break

			# Check if we need to ping (no recent activity)
			elapsed = time.time() - self._last_activity
			if elapsed >= self.config.heartbeat_interval:
				try:
					ping_ok = self.ping()
					self._last_heartbeat_success = ping_ok
					if not ping_ok:
						logger.warning(
							"Heartbeat ping returned false, marking connection stale"
						)
						self._last_error = "Heartbeat ping failed"
						self._last_error_time = time.time()
						# Don't reconnect from heartbeat thread - let the next
						# send_command handle reconnection to avoid blocking
						with self._lock:
							if self._state == ConnectionState.CONNECTED:
								old_state = self._state
								self._state = ConnectionState.ERROR
								self._cleanup_socket()
								self._fire_state_change("crashed", old_state.value)
				except Exception as e:
					logger.error(f"Heartbeat error: {e}")
					self._last_heartbeat_success = False
					self._last_error = f"Heartbeat error: {e}"
					self._last_error_time = time.time()
					with self._lock:
						if self._state == ConnectionState.CONNECTED:
							old_state = self._state
							self._state = ConnectionState.ERROR
							self._cleanup_socket()
							self._fire_state_change("crashed", old_state.value)

	def _fire_state_change(self, new_state: str, old_state: str) -> None:
		"""Invoke the on_state_change callback if registered."""
		cb = self.on_state_change
		if cb is not None:
			try:
				import datetime as _dt

				ts = _dt.datetime.now(_dt.timezone.utc).isoformat()
				cb(new_state, old_state, ts)
			except Exception:
				logger.warning("on_state_change callback failed", exc_info=True)

	def __enter__(self):
		"""Context manager entry - connect."""
		self.connect()
		return self

	def __exit__(self, exc_type, exc_val, exc_tb):
		"""Context manager exit - disconnect."""
		self.disconnect()
		return False


# ══════════════════════════════════?
# Global connection singleton (with auto-metrics wiring)
# ══════════════════════════════════?

_global_connection: Optional[PersistentUnrealConnection] = None


def _wire_metrics(conn: PersistentUnrealConnection) -> None:
	"""Attach the global MetricsCollector to a connection via callback."""
	from .metrics import get_metrics

	collector = get_metrics()
	conn.on_request_complete = collector.record_simple


def get_connection(
	config: Optional[ConnectionConfig] = None,
) -> PersistentUnrealConnection:
	"""Get or create the global connection instance.

	If *config* is provided **and** no connection exists yet, the new
	connection is created with that config.  If a connection already
	exists, *config* is ignored (call :func:`reset_connection` first to
	reconfigure).

	Automatically wires up the global MetricsCollector so every command
	sent through this connection records timing/success metrics.
	"""
	global _global_connection
	if _global_connection is None:
		_global_connection = PersistentUnrealConnection(config)
		_wire_metrics(_global_connection)
	return _global_connection


def reset_connection():
	"""Reset the global connection (for testing or reconfiguration)."""
	global _global_connection
	if _global_connection:
		_global_connection.disconnect()
	_global_connection = None

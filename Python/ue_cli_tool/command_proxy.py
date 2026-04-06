"""
Dynamic command proxy for UE MCP Bridge.

Routes Python method calls to C++ commands via the ActionRegistry,
eliminating the need for hundreds of hand-written wrapper methods.

Usage:
	from ue_cli_tool.connection import PersistentUnrealConnection
	from ue_cli_tool.command_proxy import CommandProxy

	conn = PersistentUnrealConnection()
	proxy = CommandProxy(conn)

	# Call by C++ command name
	proxy.create_blueprint(name="BP_Test", parent_class="Actor")

	# Call by action id (dot →?underscore)
	proxy.blueprint_create(name="BP_Test", parent_class="Actor")

	# Generic call
	proxy.call("create_blueprint", name="BP_Test", parent_class="Actor")

	# Search
	proxy.search("blueprint create")
"""

from __future__ import annotations

import logging
from typing import Any, Callable, Optional

from .connection import PersistentUnrealConnection, TimeoutTier, _TIMEOUT_MAP
from .registry import get_registry, ActionDef

logger = logging.getLogger(__name__)


class CommandProxy:
	"""Dynamic proxy that routes method calls to C++ commands via ActionRegistry.

	Resolution order for ``proxy.some_method(**kwargs)``:

	1. Direct match on C++ command name (e.g. ``create_blueprint``)
	2. Match on action_id with first ``_`` →?``.`` (e.g. ``blueprint_create`` →?``blueprint.create``)
	3. Fallback: try replacing all ``_`` with ``.`` (e.g. ``node_add_event`` →?``node.add_event``)

	Resolved methods are cached for O(1) subsequent lookups.
	"""

	def __init__(self, connection: PersistentUnrealConnection):
		self._conn = connection
		self._registry = get_registry()
		self._method_cache: dict[str, Callable[..., dict]] = {}

	def _resolve_action(self, name: str) -> Optional[ActionDef]:
		"""Try multiple strategies to resolve a method name to an ActionDef."""
		registry = self._registry

		# Strategy 1: exact C++ command name match
		action = registry.get_by_command(name)
		if action is not None:
			return action

		# Strategy 2: first underscore →?dot  (blueprint_create →?blueprint.create)
		if "_" in name:
			idx = name.index("_")
			dotted = name[:idx] + "." + name[idx + 1 :]
			action = registry.get(dotted)
			if action is not None:
				return action

		# Strategy 3: progressive dot replacement
		# node_add_event →?node.add_event, node_add.event, etc.
		parts = name.split("_")
		if len(parts) >= 2:
			for i in range(1, len(parts)):
				candidate = ".".join(["_".join(parts[:i]), "_".join(parts[i:])])
				action = registry.get(candidate)
				if action is not None:
					return action

		return None

	def _make_bound_method(self, action: ActionDef) -> Callable[..., dict]:
		"""Create a callable bound to this proxy's connection."""
		command = action.command
		tier = _TIMEOUT_MAP.get(command)
		conn = self._conn

		def method(**kwargs: Any) -> dict:
			params = {k: v for k, v in kwargs.items() if v is not None}
			return conn.send_raw_dict(command, params or None, timeout_tier=tier)

		method.__name__ = action.command
		method.__qualname__ = f"CommandProxy.{action.command}"
		method.__doc__ = (
			f"{action.description}\n\n"
			f"Action: {action.id}\n"
			f"Command: {action.command}\n"
			f"Tags: {', '.join(action.tags)}"
		)
		return method

	# ──── Dynamic dispatch ────────────────────────────────────────────────────────────────────────────────────────

	def __getattr__(self, name: str) -> Callable[..., dict]:
		if name.startswith("_"):
			raise AttributeError(name)

		cached = self._method_cache.get(name)
		if cached is not None:
			return cached

		action = self._resolve_action(name)
		if action is None:
			raise AttributeError(
				f"No action found for '{name}'. "
				f"Use proxy.search('{name}') or proxy.list_commands() to find actions."
			)

		method = self._make_bound_method(action)
		self._method_cache[name] = method
		return method

	# ──── Explicit call (bypasses registry lookup) ────────────────────────────────────────

	def call(self, command: str, **params: Any) -> dict:
		"""Send a command directly by C++ command type name.

		Does NOT go through the registry —?use this for commands that
		may not be registered (e.g., internal/undocumented commands).
		"""
		clean = {k: v for k, v in params.items() if v is not None}
		tier = _TIMEOUT_MAP.get(command)
		return self._conn.send_raw_dict(command, clean or None, timeout_tier=tier)

	# ──── Discovery ──────────────────────────────────────────────────────────────────────────────────────────────────────

	def search(self, query: str, **kwargs: Any) -> list[dict[str, Any]]:
		"""Search available actions by keyword or tag."""
		return self._registry.search(query, **kwargs)

	def list_commands(self) -> list[str]:
		"""Return all registered action IDs."""
		return self._registry.all_ids

	def schema(self, action_id: str) -> Optional[dict[str, Any]]:
		"""Return the full schema for an action."""
		return self._registry.schema(action_id)

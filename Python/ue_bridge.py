#!/usr/bin/env python3
"""
UE Bridge —?Complete Python interface to Unreal Engine MCPBridge.

All registered commands are accessible as typed Python methods via dynamic
proxy (CommandProxy).  High-frequency commands have explicit signatures for
IDE autocompletion; the rest are resolved automatically from the ActionRegistry.

============================================================
Usage 1: CLI mode (PowerShell)
============================================================
  python ue_bridge.py ping
  python ue_bridge.py save_all
  python ue_bridge.py get_actors_in_level

  # pipe params (recommended, avoids PowerShell escaping)
  '{"blueprint_name":"BP_X","node_id":"GUID","pin_name":"Value","default_value":"42"}' | python ue_bridge.py set_node_pin_default

  # interactive REPL
  python ue_bridge.py --repl

  # health check
  python ue_bridge.py --health

  # performance stats
  python ue_bridge.py --stats

============================================================
Usage 2: Python module import
============================================================
  from ue_bridge import UEBridge
  ue = UEBridge()

  ue.ping()
  ue.create_blueprint(name="BP_Test", parent_class="Actor")
  ue.add_component_to_blueprint(
	  blueprint_name="BP_Test",
	  component_type="StaticMeshComponent",
	  component_name="Mesh",
  )
  ue.compile_blueprint(blueprint_name="BP_Test")
  ue.save_all()

============================================================
Usage 3: Batch mode (single round-trip)
============================================================
  from ue_bridge import UEBridge
  ue = UEBridge()

  with ue.batch() as b:
	  b.create_blueprint(name="BP_Enemy", parent_class="Actor")
	  b.add_component_to_blueprint(
		  blueprint_name="BP_Enemy",
		  component_type="StaticMeshComponent",
		  component_name="Mesh",
	  )
	  b.compile_blueprint(blueprint_name="BP_Enemy")
  print(b.results)

============================================================
Usage 4: Async execution
============================================================
  ue = UEBridge()
  task_id = ue.async_submit("compile_blueprint", blueprint_name="BP_Heavy")
  result = ue.async_wait(task_id, timeout=120)
"""

import json
import sys
from typing import Any, Dict, List, Optional

try:
	import readline  # noqa: F401 —?enables arrow-key editing in REPL
except ImportError:
	pass

# ══════════════════════════════?
# Imports from the ue_cli_tool package
# ══════════════════════════════?

# Ensure the package is importable when running ue_bridge.py directly
# (it lives in Python/ alongside ue_cli_tool/)
import os as _os

_pkg_dir = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "ue_cli_tool")
if _os.path.isdir(_pkg_dir):
	_parent = _os.path.dirname(_os.path.abspath(__file__))
	if _parent not in sys.path:
		sys.path.insert(0, _parent)

from ue_cli_tool.connection import (
	PersistentUnrealConnection,
	ConnectionConfig,
	ConnectionState,
)
from ue_cli_tool.command_proxy import CommandProxy
from ue_cli_tool.pipeline import BatchContext, AsyncSubmitter
from ue_cli_tool.metrics import get_metrics

HOST = "127.0.0.1"
PORT = 55558


# ══════════════════════════════?
# UEBridge —?Public SDK
# ══════════════════════════════?


class UEBridge:
	"""
	Complete Python interface to UE MCPBridge.

	High-frequency commands have explicit method signatures for IDE
	autocompletion.  All other registered commands are available via
	dynamic proxy (``__getattr__``), resolved from the ActionRegistry.

	Examples::

		ue = UEBridge()
		ue.ping()
		ue.create_blueprint(name="BP_Test", parent_class="Actor")
		ue.compile_blueprint(blueprint_name="BP_Test")

		# Any registered command works dynamically:
		ue.add_blueprint_event_node(blueprint_name="BP_Test", event_name="ReceiveBeginPlay")

		# Batch mode:
		with ue.batch() as b:
			b.create_blueprint(name="BP_A", parent_class="Actor")
			b.create_blueprint(name="BP_B", parent_class="Pawn")
	"""

	def __init__(self, host: str = HOST, port: int = PORT):
		config = ConnectionConfig(host=host, port=port)
		self._conn = PersistentUnrealConnection(config)
		# Wire metrics
		from ue_cli_tool.connection import _wire_metrics

		_wire_metrics(self._conn)
		self._proxy = CommandProxy(self._conn)
		self._async = AsyncSubmitter(self._conn)

	# ──── Generic call ────────────────────────────────────────────────────────────────────────────────────────────────

	def call(self, command: str, **params: Any) -> dict:
		"""Send any command by C++ command name. Use for unregistered commands."""
		return self._proxy.call(command, **params)

	def close(self) -> None:
		"""Disconnect from Unreal."""
		self._conn.disconnect()

	# ──── Dynamic proxy fallback ────────────────────────────────────────────────────────────────────────────

	def __getattr__(self, name: str) -> Any:
		"""Delegate unknown methods to CommandProxy for dynamic resolution."""
		if name.startswith("_"):
			raise AttributeError(name)
		return getattr(self._proxy, name)

	# ──── Pipeline ────────────────────────────────────────────────────────────────────────────────────────────────────────

	def batch(
		self, *, continue_on_error: bool = False, transactional: bool = False
	) -> BatchContext:
		"""Create a batch context for executing multiple commands in one round-trip.

		Usage::

			with ue.batch() as b:
				b.create_blueprint(name="BP_Test", parent_class="Actor")
				b.compile_blueprint(blueprint_name="BP_Test")
			print(b.results)
		"""
		return BatchContext(
			self._conn,
			continue_on_error=continue_on_error,
			transactional=transactional,
		)

	def async_submit(self, command: str, **params: Any) -> str:
		"""Submit a command for async execution. Returns task_id."""
		return self._async.submit(command, params or None)

	def async_wait(self, task_id: str, *, timeout: float = 120.0) -> dict:
		"""Wait for an async task to complete."""
		return self._async.wait(task_id, timeout=timeout)

	# ──── Diagnostics ──────────────────────────────────────────────────────────────────────────────────────────────────

	def health(self) -> dict:
		"""Get connection health status."""
		return self._conn.get_health()

	def stats(self, last_n: int = 100) -> dict:
		"""Get performance metrics summary."""
		return get_metrics().get_summary(last_n)

	def search(self, query: str, **kwargs: Any) -> list:
		"""Search available commands by keyword."""
		return self._proxy.search(query, **kwargs)

	# ════════════════════════════════?
	# HIGH-FREQUENCY EXPLICIT METHODS (IDE autocompletion friendly)
	# ════════════════════════════════?

	# ──── Meta ────────────────────────────────────────────────────────────────────────────────────────────────────────────────

	def ping(self) -> dict:
		return self.call("ping")

	def get_context(self) -> dict:
		return self.call("get_context")

	def save_all(self) -> dict:
		return self.call("save_all")

	# ──── Blueprint CRUD ────────────────────────────────────────────────────────────────────────────────────────────

	def create_blueprint(
		self, name: str, parent_class: str, *, path: str = None
	) -> dict:
		return self.call(
			"create_blueprint", name=name, parent_class=parent_class, path=path
		)

	def compile_blueprint(self, blueprint_name: str) -> dict:
		return self.call("compile_blueprint", blueprint_name=blueprint_name)

	def get_blueprint_summary(
		self, *, blueprint_name: str = None, asset_path: str = None
	) -> dict:
		return self.call(
			"get_blueprint_summary",
			blueprint_name=blueprint_name,
			asset_path=asset_path,
		)

	# ──── Components ────────────────────────────────────────────────────────────────────────────────────────────────────

	def add_component_to_blueprint(
		self,
		blueprint_name: str,
		component_type: str,
		component_name: str,
		*,
		location: List[float] = None,
		rotation: List[float] = None,
		scale: List[float] = None,
		component_properties: dict = None,
	) -> dict:
		return self.call(
			"add_component_to_blueprint",
			blueprint_name=blueprint_name,
			component_type=component_type,
			component_name=component_name,
			location=location,
			rotation=rotation,
			scale=scale,
			component_properties=component_properties,
		)

	# ──── Nodes ──────────────────────────────────────────────────────────────────────────────────────────────────────────────

	def add_blueprint_event_node(
		self, blueprint_name: str, event_name: str, *, node_position: str = None
	) -> dict:
		return self.call(
			"add_blueprint_event_node",
			blueprint_name=blueprint_name,
			event_name=event_name,
			node_position=node_position,
		)

	def add_blueprint_function_node(
		self,
		blueprint_name: str,
		target: str,
		function_name: str,
		*,
		params: str = None,
		node_position: str = None,
		graph_name: str = None,
	) -> dict:
		return self.call(
			"add_blueprint_function_node",
			blueprint_name=blueprint_name,
			target=target,
			function_name=function_name,
			params=params,
			node_position=node_position,
			graph_name=graph_name,
		)

	# ──── Variables ──────────────────────────────────────────────────────────────────────────────────────────────────────

	def add_blueprint_variable(
		self,
		blueprint_name: str,
		variable_name: str,
		variable_type: str,
		*,
		is_exposed: bool = None,
	) -> dict:
		return self.call(
			"add_blueprint_variable",
			blueprint_name=blueprint_name,
			variable_name=variable_name,
			variable_type=variable_type,
			is_exposed=is_exposed,
		)

	def set_blueprint_variable_default(
		self, blueprint_name: str, variable_name: str, default_value: str
	) -> dict:
		return self.call(
			"set_blueprint_variable_default",
			blueprint_name=blueprint_name,
			variable_name=variable_name,
			default_value=default_value,
		)

	# ──── Graph wiring ────────────────────────────────────────────────────────────────────────────────────────────────

	def connect_blueprint_nodes(
		self,
		blueprint_name: str,
		source_node_id: str,
		source_pin: str,
		target_node_id: str,
		target_pin: str,
		*,
		graph_name: str = None,
	) -> dict:
		return self.call(
			"connect_blueprint_nodes",
			blueprint_name=blueprint_name,
			source_node_id=source_node_id,
			source_pin=source_pin,
			target_node_id=target_node_id,
			target_pin=target_pin,
			graph_name=graph_name,
		)

	def find_blueprint_nodes(
		self,
		blueprint_name: str,
		*,
		graph_name: str = None,
		node_type: str = None,
		event_type: str = None,
	) -> dict:
		return self.call(
			"find_blueprint_nodes",
			blueprint_name=blueprint_name,
			graph_name=graph_name,
			node_type=node_type,
			event_type=event_type,
		)

	def get_node_pins(
		self, blueprint_name: str, node_id: str, *, graph_name: str = None
	) -> dict:
		return self.call(
			"get_node_pins",
			blueprint_name=blueprint_name,
			node_id=node_id,
			graph_name=graph_name,
		)

	def set_node_pin_default(
		self,
		blueprint_name: str,
		node_id: str,
		pin_name: str,
		default_value: str,
		*,
		graph_name: str = None,
	) -> dict:
		return self.call(
			"set_node_pin_default",
			blueprint_name=blueprint_name,
			node_id=node_id,
			pin_name=pin_name,
			default_value=default_value,
			graph_name=graph_name,
		)

	def move_node(
		self,
		blueprint_name: str,
		node_id: str,
		node_position: List[float],
		*,
		graph_name: str = None,
	) -> dict:
		return self.call(
			"move_node",
			blueprint_name=blueprint_name,
			node_id=node_id,
			node_position=node_position,
			graph_name=graph_name,
		)

	# ──── Actors ────────────────────────────────────────────────────────────────────────────────────────────────────────────

	def get_actors_in_level(self) -> dict:
		return self.call("get_actors_in_level")

	def spawn_actor(
		self,
		name: str,
		type: str,
		*,
		location: List[float] = None,
		rotation: List[float] = None,
	) -> dict:
		return self.call(
			"spawn_actor", name=name, type=type, location=location, rotation=rotation
		)

	# ──── Materials ──────────────────────────────────────────────────────────────────────────────────────────────────────

	def create_material(
		self,
		material_name: str,
		*,
		path: str = None,
		domain: str = None,
		blend_mode: str = None,
	) -> dict:
		return self.call(
			"create_material",
			material_name=material_name,
			path=path,
			domain=domain,
			blend_mode=blend_mode,
		)

	def add_material_expression(
		self,
		material_name: str,
		expression_class: str,
		node_name: str,
		*,
		position: List[float] = None,
		properties: dict = None,
	) -> dict:
		return self.call(
			"add_material_expression",
			material_name=material_name,
			expression_class=expression_class,
			node_name=node_name,
			position=position,
			properties=properties,
		)

	def connect_material_expressions(
		self,
		material_name: str,
		source_node: str,
		target_node: str,
		target_input: str,
		*,
		source_output_index: int = None,
	) -> dict:
		return self.call(
			"connect_material_expressions",
			material_name=material_name,
			source_node=source_node,
			target_node=target_node,
			target_input=target_input,
			source_output_index=source_output_index,
		)

	# ──── UMG Widgets ──────────────────────────────────────────────────────────────────────────────────────────────────

	def create_umg_widget_blueprint(
		self, widget_name: str, *, parent_class: str = None, path: str = None
	) -> dict:
		return self.call(
			"create_umg_widget_blueprint",
			widget_name=widget_name,
			parent_class=parent_class,
			path=path,
		)

	def add_widget_component(
		self, widget_name: str, component_type: str, component_name: str, **kwargs
	) -> dict:
		return self.call(
			"add_widget_component",
			widget_name=widget_name,
			component_type=component_type,
			component_name=component_name,
			**kwargs,
		)

	def set_widget_properties(self, widget_name: str, target: str, **kwargs) -> dict:
		return self.call(
			"set_widget_properties", widget_name=widget_name, target=target, **kwargs
		)

	# ──── Assets ────────────────────────────────────────────────────────────────────────────────────────────────────────────

	def list_assets(
		self,
		path: str,
		*,
		recursive: bool = None,
		class_filter: str = None,
		name_contains: str = None,
		max_results: int = None,
	) -> dict:
		return self.call(
			"list_assets",
			path=path,
			recursive=recursive,
			class_filter=class_filter,
			name_contains=name_contains,
			max_results=max_results,
		)


# ══════════════════════════════?
# CLI Entry Point
# ══════════════════════════════?


def _cli_main():
	"""CLI: python ue_bridge.py <command> or pipe JSON for params."""
	if len(sys.argv) < 2 or sys.argv[1] in ("--help", "-h"):
		print(__doc__)
		sys.exit(0)

	if sys.argv[1] == "--repl":
		_repl()
		return

	if sys.argv[1] == "--health":
		ue = UEBridge()
		try:
			print(json.dumps(ue.health(), indent=2, ensure_ascii=False))
		finally:
			ue.close()
		return

	if sys.argv[1] == "--stats":
		ue = UEBridge()
		try:
			# Do a ping first to establish connection and get at least one metric
			ue.ping()
			print(json.dumps(ue.stats(), indent=2, ensure_ascii=False))
		finally:
			ue.close()
		return

	command = sys.argv[1]
	params = None

	# Try joining remaining args as JSON
	if len(sys.argv) >= 3:
		raw = " ".join(sys.argv[2:])
		try:
			params = json.loads(raw)
		except json.JSONDecodeError:
			pass

	# Fallback: read from stdin
	if params is None and not sys.stdin.isatty():
		try:
			raw = sys.stdin.read().strip()
			if raw:
				params = json.loads(raw)
		except json.JSONDecodeError as e:
			print(f"ERROR: Invalid JSON: {e}", file=sys.stderr)
			sys.exit(1)

	ue = UEBridge()
	try:
		result = ue.call(command, **(params or {}))
		print(json.dumps(result, indent=2, ensure_ascii=False))
	except ConnectionRefusedError:
		print(
			"ERROR: Cannot connect to UE MCPBridge (port 55558). Is Unreal Editor running?",
			file=sys.stderr,
		)
		sys.exit(1)
	except Exception as e:
		print(f"ERROR: {e}", file=sys.stderr)
		sys.exit(1)
	finally:
		ue.close()


def _repl():
	"""Interactive REPL for testing commands."""
	print("UE Bridge REPL —?type 'help', 'health', 'stats', or 'quit'")
	ue = UEBridge()
	try:
		while True:
			try:
				line = input("ue> ").strip()
			except (EOFError, KeyboardInterrupt):
				break
			if not line:
				continue
			if line in ("quit", "exit"):
				break
			if line == "help":
				print("Usage: <command> [json_params]")
				print("Special: health, stats, search <query>")
				print("Examples:")
				print("  ping")
				print("  save_all")
				print("  get_actors_in_level")
				print('  compile_blueprint {"blueprint_name":"BP_Test"}')
				print("  search blueprint")
				continue
			if line == "health":
				print(json.dumps(ue.health(), indent=2, ensure_ascii=False))
				continue
			if line == "stats":
				print(json.dumps(ue.stats(), indent=2, ensure_ascii=False))
				continue
			if line.startswith("search "):
				query = line[7:].strip()
				results = ue.search(query)
				print(json.dumps(results, indent=2, ensure_ascii=False))
				continue

			parts = line.split(None, 1)
			cmd = parts[0]
			params = None
			if len(parts) > 1:
				try:
					params = json.loads(parts[1])
				except json.JSONDecodeError as e:
					print(f"JSON error: {e}")
					continue
			try:
				result = ue.call(cmd, **(params or {}))
				print(json.dumps(result, indent=2, ensure_ascii=False))
			except Exception as e:
				print(f"Error: {e}")
	finally:
		ue.close()
		print("\nDisconnected.")


if __name__ == "__main__":
	_cli_main()

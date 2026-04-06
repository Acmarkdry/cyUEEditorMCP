"""
CLI Parser —?parse CLI-style text into executable command dicts.

Core principle: **zero hand-maintained tables.**
Everything is derived from ActionRegistry at runtime.

Syntax
------
  <command> [positional_args...] [--flag value ...]
  @<target>     Set context (auto-fills blueprint_name / material_name / widget_name)
  # comment     Ignored
  Multiple lines = batch execution

Examples
--------
  @BP_Enemy
  add_component_to_blueprint StaticMeshComponent Mesh --location [0,0,100]
  compile_blueprint
"""

from __future__ import annotations

import json
import shlex
from dataclasses import dataclass, field
from typing import Any, TYPE_CHECKING

if TYPE_CHECKING:
	from .registry import ActionRegistry

# Context parameter names that can be auto-injected via @target
_CONTEXT_PARAMS = ("blueprint_name", "material_name", "widget_name")


@dataclass
class CommandDict:
	"""Parsed representation of a single CLI command."""

	command: str
	params: dict[str, Any] = field(default_factory=dict)
	raw_line: str = ""


@dataclass
class ParseResult:
	"""Result of parsing one or more CLI lines."""

	commands: list[CommandDict] = field(default_factory=list)
	errors: list[str] = field(default_factory=list)


class CliParser:
	"""Parse CLI-style text into executable command dicts.

	Automatically derives positional parameter mapping from
	ActionRegistry ``input_schema.required`` fields.
	"""

	def __init__(self, registry: ActionRegistry) -> None:
		self._registry = registry
		# Cache: command_name →?[ordered required param names]
		self._positional_cache: dict[str, list[str]] = {}
		# Cache: command_name →?context param name (or None)
		self._context_param_cache: dict[str, str | None] = {}

	# ──── public API ────────────────────────────────────────────────────────────────────────────────────────────────────

	def parse(self, text: str) -> ParseResult:
		"""Parse one or more CLI lines into command dicts.

		Handles ``@target`` context lines, ``#`` comments, and blank lines.
		"""
		result = ParseResult()
		context: dict[str, Any] = {}

		for raw_line in text.splitlines():
			stripped = raw_line.strip()

			# Skip blank lines and comments
			if not stripped or stripped.startswith("#"):
				continue

			# @target —?set context
			if stripped.startswith("@"):
				target = stripped[1:].strip()
				if not target:
					result.errors.append(f"Empty @target in line: {raw_line}")
					continue
				# Reset context for new target
				context = {"_target": target}
				continue

			# Regular command line
			try:
				cmd = self.parse_line(stripped, context)
				cmd.raw_line = raw_line.rstrip()
				result.commands.append(cmd)
			except Exception as e:
				result.errors.append(f"Error parsing '{stripped}': {e}")

		return result

	def parse_line(
		self, line: str, context: dict[str, Any] | None = None
	) -> CommandDict:
		"""Parse a single CLI line with context injection.

		Parameters
		----------
		line : str
			A single CLI line (no newlines).
		context : dict, optional
			Context dict. ``_target`` key holds the @target value.

		Returns
		-------
		CommandDict
		"""
		if context is None:
			context = {}

		tokens = self._tokenize(line)
		if not tokens:
			raise ValueError("Empty command line")

		command = tokens[0]
		remaining = tokens[1:]

		# Separate positional args and --flags
		positional_tokens: list[str] = []
		flag_params: dict[str, Any] = {}

		i = 0
		while i < len(remaining):
			tok = remaining[i]
			if tok.startswith("--"):
				flag_name = tok[2:]
				if not flag_name:
					i += 1
					continue
				# Check if next token exists and is not another flag
				if i + 1 < len(remaining) and not remaining[i + 1].startswith("--"):
					flag_params[flag_name] = self._coerce_value(remaining[i + 1])
					i += 2
				else:
					# --flag with no value →?treat as True
					flag_params[flag_name] = True
					i += 1
			else:
				positional_tokens.append(tok)
				i += 1

		# Get positional parameter order from registry
		positional_order = self._get_positional_order(command)

		# Detect context param for this command
		context_param = self._detect_context_param(command)

		# Build params dict
		params: dict[str, Any] = {}

		# Inject context target if applicable
		target = context.get("_target")
		if target and context_param:
			params[context_param] = target

		# Map positional tokens to required slots (excluding context-filled ones)
		available_slots = [p for p in positional_order if p not in params]

		for slot, value in zip(available_slots, positional_tokens):
			params[slot] = self._coerce_value(value)

		# Handle excess positional args —?store remaining as-is if any
		excess_start = len(available_slots)
		if len(positional_tokens) > excess_start:
			# Extra positional args beyond required —?ignore silently
			# (design says "unknown command passthrough" + "excess positional args")
			pass

		# Merge --flag params (override positional if conflict)
		for k, v in flag_params.items():
			params[k] = v

		return CommandDict(command=command, params=params)

	def to_batch_commands(self, parse_result: ParseResult) -> list[dict[str, Any]]:
		"""Convert ParseResult to ``batch_execute`` format.

		Returns a list of dicts: ``{"type": <command>, "params": {...}}``.
		"""
		return [
			{"type": cmd.command, "params": cmd.params} for cmd in parse_result.commands
		]

	# ──── internal helpers ────────────────────────────────────────────────────────────────────────────────────────

	def _get_positional_order(self, command: str) -> list[str]:
		"""Derive positional param order from registry's ``input_schema.required``.

		Returns an empty list for unknown commands (passthrough).
		Results are cached.
		"""
		if command in self._positional_cache:
			return self._positional_cache[command]

		action = self._registry.get_by_command(command)
		if action is None:
			self._positional_cache[command] = []
			return []

		required = action.input_schema.get("required", [])
		self._positional_cache[command] = list(required)
		return list(required)

	def _detect_context_param(self, command: str) -> str | None:
		"""Check if a command's schema contains a known context parameter.

		Looks for ``blueprint_name``, ``material_name``, or ``widget_name``
		in the schema properties. Returns the first match, or ``None``.
		Results are cached.
		"""
		if command in self._context_param_cache:
			return self._context_param_cache[command]

		action = self._registry.get_by_command(command)
		if action is None:
			self._context_param_cache[command] = None
			return None

		properties = action.input_schema.get("properties", {})
		for param in _CONTEXT_PARAMS:
			if param in properties:
				self._context_param_cache[command] = param
				return param

		self._context_param_cache[command] = None
		return None

	@staticmethod
	def _tokenize(line: str) -> list[str]:
		"""Tokenize a CLI line respecting quoted strings.

		Uses ``shlex.split`` so ``"multi word value"`` is treated as one token.
		Falls back to simple ``str.split`` if shlex fails (unbalanced quotes).
		"""
		try:
			return shlex.split(line, posix=True)
		except ValueError:
			# Unbalanced quotes —?fallback to naive split
			return line.split()

	@staticmethod
	def _coerce_value(val: str) -> Any:
		"""Auto-convert a string value to the appropriate Python type.

		Conversion order:
		  1. bool (true/false, case-insensitive)
		  2. int
		  3. float
		  4. JSON array ``[...]``
		  5. JSON object ``{...}``
		  6. Plain string (as-is)
		"""
		# Bool
		if val.lower() == "true":
			return True
		if val.lower() == "false":
			return False

		# Int
		try:
			return int(val)
		except ValueError:
			pass

		# Float
		try:
			return float(val)
		except ValueError:
			pass

		# JSON array or object
		if (val.startswith("[") and val.endswith("]")) or (
			val.startswith("{") and val.endswith("}")
		):
			try:
				return json.loads(val)
			except (json.JSONDecodeError, ValueError):
				pass

		# Plain string
		return val

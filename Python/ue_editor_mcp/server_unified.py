"""
Unified MCP Server — single server with exactly 11 fixed tools.

Replaces 6 separate servers (ue-editor, ue-blueprint, ue-bp-nodes,
ue-bp-graph, ue-materials, ue-umg) with one ``ue-editor-mcp`` server that
exposes:
     1. ue_ping           — connection health check
     2. ue_actions_search — find actions by keyword / tags
     3. ue_actions_schema — get full schema for an action
     4. ue_actions_run    — execute a single action
     5. ue_batch          — execute multiple actions atomically
     6. ue_resources_read — read embedded documentation
     7. ue_logs_tail      — tail recent command log
     8. ue_skills         — on-demand skill catalog (list / load)
     9. ue_python_exec    — execute Python code in UE embedded Python
    10. ue_async_run      — submit/poll async commands
    11. ue_context         — persistent context (resume/status/history/workset/clear)

AI workflow (skill-based, preferred):
    ue_skills(list) → ue_skills(load, skill_id) → ue_actions_run / ue_batch

AI workflow (legacy search-based):
    ue_actions_search → ue_actions_schema → ue_actions_run

AI workflow (Python exec):
    ue_python_exec(code="import unreal; _result = ...") → direct UE Python API
"""

from __future__ import annotations

import asyncio
import atexit
import json
import logging
import time
from collections import deque
from pathlib import Path
from typing import Any

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import Tool, TextContent, ImageContent

from .connection import get_connection, CommandResult
from .context import ContextStore
from .permissions import PermissionPolicy
from .registry import get_registry
from .skills import get_skill_list, load_skill


def _to_serializable(obj: Any) -> Any:
    """Recursively convert CommandResult objects to dicts so json.dumps never fails."""
    if isinstance(obj, CommandResult):
        return _to_serializable(obj.to_dict())
    if isinstance(obj, dict):
        return {k: _to_serializable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_to_serializable(item) for item in obj]
    return obj


logger = logging.getLogger(__name__)

# ── constants ───────────────────────────────────────────────────────────

_MAX_BATCH = 50
_LOG_BUFFER_SIZE = 200

# ── P9: Permission policy ───────────────────────────────────────────────

_permission_policy = PermissionPolicy()  # default: auto (allow all)

# ── context store (initialised lazily in _run) ──────────────────────────

_context_store: ContextStore | None = None

# ── command log ring buffer ─────────────────────────────────────────────

_command_log: deque[dict] = deque(maxlen=_LOG_BUFFER_SIZE)


def _log_command(
    action_id: str, params: dict | None, result: dict, elapsed_ms: float
) -> None:
    _command_log.append(
        {
            "ts": time.strftime("%H:%M:%S"),
            "action": action_id,
            "ok": result.get("success", False),
            "ms": round(elapsed_ms, 1),
            "error": result.get("error"),
        }
    )


# ── UMG component dispatch table ───────────────────────────────────────
# Translates (component_type) → (C++ command, name param key, component_class)

_UMG_COMPONENT_TYPE_MAP: dict[str, tuple[str, str, str | None]] = {
    "TextBlock": ("add_text_block_to_widget", "text_block_name", None),
    "Button": ("add_button_to_widget", "button_name", None),
    "Image": ("add_image_to_widget", "image_name", None),
    "Border": ("add_border_to_widget", "border_name", None),
    "Overlay": ("add_overlay_to_widget", "overlay_name", None),
    "HorizontalBox": ("add_horizontal_box_to_widget", "horizontal_box_name", None),
    "VerticalBox": ("add_vertical_box_to_widget", "vertical_box_name", None),
    "Slider": ("add_slider_to_widget", "slider_name", None),
    "ProgressBar": ("add_progress_bar_to_widget", "progress_bar_name", None),
    "SizeBox": ("add_size_box_to_widget", "size_box_name", None),
    "ScaleBox": ("add_scale_box_to_widget", "scale_box_name", None),
    "CanvasPanel": ("add_canvas_panel_to_widget", "canvas_panel_name", None),
    "ComboBox": ("add_combo_box_to_widget", "combo_box_name", None),
    "CheckBox": ("add_check_box_to_widget", "check_box_name", None),
    "SpinBox": ("add_spin_box_to_widget", "spin_box_name", None),
    "EditableTextBox": (
        "add_editable_text_box_to_widget",
        "editable_text_box_name",
        None,
    ),
    "ScrollBox": ("add_generic_widget_to_widget", "component_name", "ScrollBox"),
    "WidgetSwitcher": (
        "add_generic_widget_to_widget",
        "component_name",
        "WidgetSwitcher",
    ),
    "BackgroundBlur": (
        "add_generic_widget_to_widget",
        "component_name",
        "BackgroundBlur",
    ),
    "UniformGridPanel": (
        "add_generic_widget_to_widget",
        "component_name",
        "UniformGridPanel",
    ),
    "Spacer": ("add_generic_widget_to_widget", "component_name", "Spacer"),
    "RichTextBlock": (
        "add_generic_widget_to_widget",
        "component_name",
        "RichTextBlock",
    ),
    "WrapBox": ("add_generic_widget_to_widget", "component_name", "WrapBox"),
    "CircularThrobber": (
        "add_generic_widget_to_widget",
        "component_name",
        "CircularThrobber",
    ),
}


# ── embedded resources ──────────────────────────────────────────────────

_RESOURCES_DIR = Path(__file__).parent / "resources"


def _read_resource(name: str) -> str:
    """Read an embedded resource file."""
    path = _RESOURCES_DIR / name
    if not path.exists():
        available = (
            [f.name for f in _RESOURCES_DIR.iterdir()]
            if _RESOURCES_DIR.exists()
            else []
        )
        return json.dumps(
            {"error": f"Resource '{name}' not found. Available: {available}"}
        )
    return path.read_text(encoding="utf-8")


# ── core command executor ───────────────────────────────────────────────


def _send_command(command_type: str, params: dict | None = None) -> dict:
    """Send command to Unreal, return result dict."""
    conn = get_connection()
    if not conn.is_connected:
        conn.connect()
    result = conn.send_command(command_type, params)
    return result.to_dict()


def _resolve_action_to_cpp_command(
    action_id: str, params: dict | None = None
) -> tuple[str | None, dict, str | None]:
    """Resolve an action_id + params to C++ command type + final params.

    Returns (command, final_params, error).  error is non-None on failure.
    Handles UMG component_type dispatch.
    """
    registry = get_registry()
    action = registry.get(action_id)
    if action is None:
        return None, {}, f"Unknown action: {action_id}"

    command = action.command
    final_params = dict(params) if params else {}

    # Special dispatch: widget.add_component → route by component_type
    if command == "add_widget_component":
        component_type = final_params.get("component_type", "")
        if component_type not in _UMG_COMPONENT_TYPE_MAP:
            supported = list(_UMG_COMPONENT_TYPE_MAP.keys())
            return (
                None,
                {},
                f"Unknown component_type: {component_type}. Supported: {supported}",
            )
        cmd_key, name_param, component_class = _UMG_COMPONENT_TYPE_MAP[component_type]
        if "component_name" in final_params:
            final_params[name_param] = final_params.pop("component_name")
        final_params.pop("component_type", None)
        if component_class:
            final_params["component_class"] = component_class
        command = cmd_key

    return command, final_params, None


def _execute_action(action_id: str, params: dict | None = None) -> dict:
    """Execute a registered action by its ID, with special dispatch for UMG widgets."""
    # P9: Permission check
    registry = get_registry()
    action_def = registry.get(action_id)
    if action_def:
        rejection = _permission_policy.check(
            action_id=action_id,
            risk=getattr(action_def, "risk", "safe"),
            capabilities=getattr(action_def, "capabilities", ()),
            confirm=bool((params or {}).get("confirm", False)),
        )
        if rejection:
            return rejection

    command, final_params, error = _resolve_action_to_cpp_command(action_id, params)
    if error:
        return {"success": False, "error": error}

    t0 = time.perf_counter()
    result = _send_command(command, final_params or None)
    elapsed = (time.perf_counter() - t0) * 1000

    _log_command(action_id, params, result, elapsed)
    return result


# ── Tool definitions ────────────────────────────────────────────────────

TOOLS = [
    Tool(
        name="ue_ping",
        description="Test connection to Unreal Engine. Returns pong if alive.",
        inputSchema={"type": "object", "properties": {}},
    ),
    Tool(
        name="ue_actions_search",
        description=(
            "Search for available actions by keyword or tag. "
            "Returns a ranked list of matching action IDs with descriptions. "
            "Use this FIRST to discover what actions exist before calling ue_actions_schema."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Natural language search (e.g. 'create blueprint', 'add variable', 'spawn actor')",
                },
                "tags": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "Optional tag filter (e.g. ['widget', 'umg']). Action must have ALL specified tags.",
                },
                "top_k": {
                    "type": "integer",
                    "description": "Max results to return (default: 10, max: 50)",
                },
            },
        },
    ),
    Tool(
        name="ue_actions_schema",
        description=(
            "Get the full input schema, examples, and metadata for a specific action. "
            "Call this AFTER ue_actions_search to learn the exact parameters before calling ue_actions_run."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "action_id": {
                    "type": "string",
                    "description": "Action ID from search results (e.g. 'blueprint.create', 'node.add_event')",
                },
            },
            "required": ["action_id"],
        },
    ),
    Tool(
        name="ue_actions_run",
        description=(
            "Execute a single action in Unreal Engine. "
            "Use ue_actions_search + ue_actions_schema first to discover the required parameters. "
            "IMPORTANT: You may pass action parameters in TWO ways — "
            '(A) nested under \'params\' key: {"action_id": "x", "params": {"foo": 1}} '
            '(B) flat top-level keys alongside action_id: {"action_id": "x", "foo": 1} '
            "Both forms are accepted. Use (B) when the params schema is opaque."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "action_id": {
                    "type": "string",
                    "description": "Action ID (e.g. 'blueprint.create')",
                },
                "params": {
                    "type": "object",
                    "additionalProperties": True,
                    "description": (
                        "Action parameters matching the schema from ue_actions_schema. "
                        "Alternative: pass parameters as flat top-level keys alongside action_id."
                    ),
                },
            },
            "required": ["action_id"],
        },
    ),
    Tool(
        name="ue_batch",
        description=(
            "Execute multiple actions in a SINGLE TCP round-trip via C++ batch_execute. "
            "Much faster than calling ue_actions_run multiple times. "
            "Stops at first failure unless continue_on_error is true. "
            f"Max {_MAX_BATCH} actions per batch."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "actions": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "action_id": {"type": "string"},
                            "params": {
                                "type": "object",
                                "additionalProperties": True,
                                "description": "Action parameters. Can also be passed as flat top-level keys alongside action_id.",
                            },
                        },
                        "required": ["action_id"],
                    },
                    "description": "Array of {action_id, params} to execute in order",
                },
                "continue_on_error": {
                    "type": "boolean",
                    "description": "If true, continue executing after a failure (default: false)",
                },
            },
            "required": ["actions"],
        },
    ),
    Tool(
        name="ue_resources_read",
        description=(
            "Read embedded documentation and reference material. "
            "Available resources: conventions.md, error_codes.md, patch_spec.md"
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "name": {
                    "type": "string",
                    "description": "Resource filename (e.g. 'conventions.md')",
                },
            },
            "required": ["name"],
        },
    ),
    Tool(
        name="ue_logs_tail",
        description="Tail recent logs. 'source' selects: 'python' (MCP command log), 'editor' (UE editor log via C++ ring buffer), or 'both'.",
        inputSchema={
            "type": "object",
            "properties": {
                "n": {
                    "type": "integer",
                    "description": "Number of log entries to return (default: 20, max: 200)",
                },
                "source": {
                    "type": "string",
                    "enum": ["python", "editor", "both"],
                    "description": "Log source: 'python' (default) = MCP command log, 'editor' = UE editor log ring buffer, 'both' = merged",
                },
                "category": {
                    "type": "string",
                    "description": "Filter by log category (only for source=editor/both, e.g. 'LogMCP')",
                },
                "min_verbosity": {
                    "type": "string",
                    "description": "Minimum verbosity (only for source=editor/both): Fatal, Error, Warning, Display, Log, Verbose",
                },
            },
        },
    ),
    Tool(
        name="ue_skills",
        description=(
            "Load domain-specific action catalogs (skills) on demand. "
            "Use action='list' to see all available skill domains with descriptions. "
            "Use action='load' with a skill_id to get the full action schemas, examples, "
            "and workflow tips for that domain. "
            "This replaces the search→schema workflow — load a skill once, then call "
            "ue_actions_run or ue_batch directly with the action IDs and parameters from the skill."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["list", "load"],
                    "description": "'list' = show skill catalog index, 'load' = load full skill content",
                },
                "skill_id": {
                    "type": "string",
                    "description": "Skill ID to load (required when action='load'). Get IDs from action='list'.",
                },
            },
            "required": ["action"],
        },
    ),
    Tool(
        name="ue_python_exec",
        description=(
            "Execute Python code directly in Unreal Engine's embedded Python environment. "
            "Use `import unreal` to access the full UE Python API. "
            "Set `_result = <value>` in your code to return data to the caller. "
            "Captures stdout, stderr, and the _result variable. "
            "This replaces many individual actions — use unreal.EditorLevelLibrary, "
            "unreal.EditorAssetLibrary, etc. for actor/asset/material operations."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "code": {
                    "type": "string",
                    "description": (
                        "Python code to execute in Unreal's embedded Python environment. "
                        "Use `import unreal` to access UE API. "
                        "Set `_result = ...` to return data."
                    ),
                },
                "timeout_seconds": {
                    "type": "integer",
                    "description": "Max execution time in seconds (default: 30, max: 240)",
                },
            },
            "required": ["code"],
        },
    ),
    Tool(
        name="ue_context",
        description=(
            "Manage persistent cross-session context. "
            "action='resume': recover context from last session (previous session, UE connection, workset, recent ops). "
            "action='status': current session real-time status. "
            "action='history': recent operation history (limit param). "
            "action='workset': currently tracked assets. "
            "action='clear': reset history and workset."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["resume", "status", "history", "workset", "clear"],
                    "description": "Context action to perform",
                },
                "limit": {
                    "type": "integer",
                    "description": "Number of history entries to return (default 20, max 100). Only used with action='history'.",
                },
            },
            "required": ["action"],
        },
    ),
    Tool(
        name="ue_async_run",
        description=(
            "Submit a command for async execution or poll for its result. "
            "Use action='submit' to start a long-running command without blocking. "
            "Use action='poll' with a task_id to check if it's done and get the result. "
            "Useful for compile_blueprint, batch operations, and other slow commands."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "action": {
                    "type": "string",
                    "enum": ["submit", "poll"],
                    "description": "'submit' = start async command, 'poll' = check task result",
                },
                "action_id": {
                    "type": "string",
                    "description": "Action ID to execute (required for submit). Uses the same IDs as ue_actions_run.",
                },
                "params": {
                    "type": "object",
                    "additionalProperties": True,
                    "description": "Action parameters (for submit)",
                },
                "task_id": {
                    "type": "string",
                    "description": "Task ID to poll (required for poll). Returned by a previous submit call.",
                },
            },
            "required": ["action"],
        },
    ),
]


# ── Server ──────────────────────────────────────────────────────────────

server = Server("ue-editor-mcp")


@server.list_tools()
async def list_tools() -> list[Tool]:
    return TOOLS


@server.call_tool()
async def call_tool(
    name: str, arguments: dict[str, Any]
) -> list[TextContent | ImageContent]:
    """Route tool calls to the appropriate handler."""
    args = arguments or {}
    t0 = time.perf_counter()
    try:
        result = _handle_tool(name, args)
    except Exception as e:
        logger.exception("Tool %s failed", name)
        result = {"success": False, "error": str(e)}

    # ── context recording hook ──────────────────────────────────────
    if _context_store is not None and name != "ue_context":
        elapsed_ms = (time.perf_counter() - t0) * 1000
        success = isinstance(result, dict) and result.get("success", False)
        action_id = args.get("action_id") or args.get("action") or None
        try:
            _context_store.record_operation(
                name, action_id, args, success, result, elapsed_ms
            )
            _context_store.track_assets(args, action_id)
        except Exception:
            logger.warning("Context recording failed", exc_info=True)

    contents: list[TextContent | ImageContent] = []
    image_blocks_count = 0

    # Extract base64 images into ImageContent blocks if present
    if isinstance(result, dict) and result.get("success"):
        seen_images: set[tuple[str, str]] = set()

        # Helper to extract image from a dict
        def _extract_image(item: dict):
            # Support both "image_base64" (thumbnail) and "image" (viewport screenshot) fields
            raw_data = item.get("image_base64") or item.get("image")
            if raw_data:
                mime_type = item.get("mime_type", "image/png")
                base64_data = raw_data
                if base64_data.startswith("data:"):
                    # Parse "data:image/png;base64,XXXX" format
                    header, base64_data = base64_data.split(",", 1)
                    if "image/" in header:
                        mime_type = header.split(";")[0].replace("data:", "")
                image_key = (mime_type, base64_data[:64])  # Use prefix for dedup
                if image_key not in seen_images:
                    contents.append(
                        ImageContent(
                            type="image", data=base64_data, mimeType=mime_type
                        )
                    )
                    seen_images.add(image_key)
                    nonlocal image_blocks_count
                    image_blocks_count += 1
                if "image_base64" in item:
                    item["image_base64"] = "<base64_data_extracted_to_image_content>"
                if "image" in item:
                    item["image"] = "<base64_data_extracted_to_image_content>"

        # Prefer list-form thumbnails. Fallback to top-level singleton fields.
        has_thumbnail_list = (
            isinstance(result.get("thumbnails"), list)
            and len(result.get("thumbnails", [])) > 0
        )

        if has_thumbnail_list:
            for thumb in result.get("thumbnails", []):
                if isinstance(thumb, dict):
                    _extract_image(thumb)
        else:
            _extract_image(result)

        # Always redact top-level singleton payload too, even when list-form thumbnails are used.
        if "image_base64" in result:
            result["image_base64"] = "<base64_data_extracted_to_image_content>"

        # Handle batch results
        for batch_res in result.get("results", []):
            if isinstance(batch_res, dict):
                batch_has_thumbnail_list = (
                    isinstance(batch_res.get("thumbnails"), list)
                    and len(batch_res.get("thumbnails", [])) > 0
                )
                if batch_has_thumbnail_list:
                    for thumb in batch_res.get("thumbnails", []):
                        if isinstance(thumb, dict):
                            _extract_image(thumb)
                else:
                    _extract_image(batch_res)

                if "image_base64" in batch_res:
                    batch_res["image_base64"] = (
                        "<base64_data_extracted_to_image_content>"
                    )

            result["image_blocks_count"] = image_blocks_count

    safe_result = (
        _to_serializable(result)
        if isinstance(result, (dict, CommandResult))
        else result
    )
    text = (
        json.dumps(safe_result, indent=2, ensure_ascii=False)
        if isinstance(safe_result, dict)
        else str(safe_result)
    )
    contents.append(TextContent(type="text", text=text))

    return contents


def _handle_tool(name: str, args: dict) -> Any:
    """Dispatch tool call — returns serializable result."""

    # ── 1. ue_ping ──────────────────────────────────────────────────
    if name == "ue_ping":
        conn = get_connection()
        if not conn.is_connected:
            conn.connect()
        ok = conn.ping()
        return {"success": ok, "pong": ok}

    # ── 2. ue_actions_search ────────────────────────────────────────
    if name == "ue_actions_search":
        registry = get_registry()
        query = args.get("query", "")
        tags = args.get("tags")
        top_k = min(args.get("top_k", 10), 50)
        results = registry.search(query, tags=tags, top_k=top_k)
        return {
            "success": True,
            "total_actions": registry.count,
            "results": results,
        }

    # ── 3. ue_actions_schema ────────────────────────────────────────
    if name == "ue_actions_schema":
        action_id = args.get("action_id", "")
        registry = get_registry()
        schema = registry.schema(action_id)
        if schema is None:
            # Fuzzy suggestion
            suggestions = registry.search(action_id, top_k=5)
            return {
                "success": False,
                "error": f"Action '{action_id}' not found",
                "suggestions": suggestions,
            }
        return {"success": True, **schema}

    # ── 4. ue_actions_run ───────────────────────────────────────────
    if name == "ue_actions_run":
        action_id = args.get("action_id", "")
        params = args.get("params")
        logger.info(
            "ue_actions_run: action_id=%s, raw params type=%s, params=%s",
            action_id,
            type(params).__name__,
            (
                json.dumps(params, ensure_ascii=False)[:300]
                if isinstance(params, dict)
                else repr(params)[:300]
            ),
        )
        # LLMs often flatten params as top-level keys instead of nesting
        # under "params".  Auto-collect them so single-action calls work.
        # Also catches empty dict {} that some MCP clients send for
        # schema-less "type": "object" parameters.
        if not params:
            extra = {k: v for k, v in args.items() if k != "action_id"}
            if extra:
                params = extra
                logger.info(
                    "ue_actions_run: auto-collected %d top-level keys as params",
                    len(extra),
                )
        return _execute_action(action_id, params)

    # ── 5. ue_batch ─────────────────────────────────────────────────
    # Optimized: routes through C++ batch_execute for single TCP round-trip
    # instead of N individual TCP calls.
    if name == "ue_batch":
        actions = args.get("actions", [])
        continue_on_error = args.get("continue_on_error", False)

        if len(actions) > _MAX_BATCH:
            return {
                "success": False,
                "error": f"Max {_MAX_BATCH} actions per batch, got {len(actions)}",
            }

        if not actions:
            return {
                "success": True,
                "total": 0,
                "executed": 0,
                "failed": 0,
                "results": [],
            }

        # Phase 1: Resolve all action_ids to C++ commands (Python-side, no TCP)
        cpp_commands = []
        for i, item in enumerate(actions):
            aid = item.get("action_id", "")
            params = item.get("params")
            command, final_params, error = _resolve_action_to_cpp_command(aid, params)
            if error:
                return {
                    "success": False,
                    "error": f"Action at index {i} ('{aid}'): {error}",
                }
            cpp_commands.append({"type": command, "params": final_params or {}})

        # Phase 2: Single TCP round-trip via C++ batch_execute
        t0 = time.perf_counter()
        result = _send_command(
            "batch_execute",
            {
                "commands": cpp_commands,
                "stop_on_error": not continue_on_error,
            },
        )
        elapsed = (time.perf_counter() - t0) * 1000

        # Phase 3: Enrich results with action_ids and log
        batch_results = result.get("results", [])
        for i, r in enumerate(batch_results):
            if i < len(actions):
                r["action_id"] = actions[i].get("action_id", "")

        # Log each action for the Python command log
        per_action_ms = elapsed / max(len(actions), 1)
        for i, item in enumerate(actions):
            sub_result = (
                batch_results[i] if i < len(batch_results) else {"success": False}
            )
            _log_command(
                item.get("action_id", ""), item.get("params"), sub_result, per_action_ms
            )

        return {
            "success": result.get("success", False),
            "total": result.get("total", len(actions)),
            "executed": result.get("executed", len(batch_results)),
            "failed": result.get("failed", 0),
            "results": batch_results,
        }

    # ── 6. ue_resources_read ────────────────────────────────────────
    if name == "ue_resources_read":
        resource_name = args.get("name", "")
        content = _read_resource(resource_name)
        return content  # Return raw text, not JSON-wrapped

    # ── 7. ue_logs_tail ─────────────────────────────────────────────
    if name == "ue_logs_tail":
        n = min(args.get("n", 20), _LOG_BUFFER_SIZE)
        source = args.get("source", "python")

        result: dict[str, Any] = {"success": True}

        # Python-side command log
        if source in ("python", "both"):
            py_entries = list(_command_log)[-n:]
            result["python_log"] = {
                "total_logged": len(_command_log),
                "entries": py_entries,
            }

        # Editor-side C++ ring buffer (via get_editor_logs action)
        if source in ("editor", "both"):
            editor_params: dict[str, Any] = {"count": n}
            cat = args.get("category")
            mv = args.get("min_verbosity")
            if cat:
                editor_params["category"] = cat
            if mv:
                editor_params["min_verbosity"] = mv
            try:
                conn = get_connection()
                editor_result = conn.send_command("get_editor_logs", editor_params)
                editor_dict = (
                    editor_result.to_dict()
                    if isinstance(editor_result, CommandResult)
                    else editor_result
                )
                result["editor_log"] = _to_serializable(editor_dict)
            except Exception as exc:
                result["editor_log"] = {"error": str(exc)}

        # Backward compat: when source == "python", keep flat shape
        if source == "python":
            result["total_logged"] = result["python_log"]["total_logged"]
            result["entries"] = result["python_log"]["entries"]
            del result["python_log"]

        return result

    # ── 8. ue_skills ────────────────────────────────────────────────
    if name == "ue_skills":
        action = args.get("action", "")

        if action == "list":
            return {
                "success": True,
                "skills": get_skill_list(),
                "usage_hint": "Call ue_skills(action='load', skill_id='<id>') to get full action schemas and workflow tips for a domain.",
            }

        if action == "load":
            skill_id = args.get("skill_id", "")
            if not skill_id:
                return {
                    "success": False,
                    "error": "skill_id is required when action='load'",
                }
            skill_data = load_skill(skill_id)
            if skill_data is None:
                available = [s["skill_id"] for s in get_skill_list()]
                return {
                    "success": False,
                    "error": f"Unknown skill_id: '{skill_id}'",
                    "available": available,
                }
            return {"success": True, **skill_data}

        return {
            "success": False,
            "error": f"Unknown action: '{action}'. Use 'list' or 'load'.",
        }

    # ── 9. ue_python_exec ───────────────────────────────────────────
    if name == "ue_python_exec":
        code = args.get("code", "")
        if not code:
            return {"success": False, "error": "code is required"}
        timeout = min(args.get("timeout_seconds", 30), 240)
        t0 = time.perf_counter()
        result = _send_command("exec_python", {"code": code})
        elapsed = (time.perf_counter() - t0) * 1000
        _log_command("python.exec", {"code_length": len(code)}, result, elapsed)
        return result

    # ── 10. ue_async_run ────────────────────────────────────────────
    if name == "ue_async_run":
        action = args.get("action", "")

        if action == "submit":
            action_id = args.get("action_id", "")
            if not action_id:
                return {"success": False, "error": "action_id is required for submit"}
            params = args.get("params")
            # Resolve action_id to C++ command
            command, final_params, error = _resolve_action_to_cpp_command(
                action_id, params
            )
            if error:
                return {"success": False, "error": error}
            result = _send_command(
                "async_execute",
                {
                    "command": command,
                    "params": final_params or {},
                },
            )
            return result

        if action == "poll":
            task_id = args.get("task_id", "")
            if not task_id:
                return {"success": False, "error": "task_id is required for poll"}
            result = _send_command("get_task_result", {"task_id": task_id})
            return result

        return {
            "success": False,
            "error": f"Unknown action: '{action}'. Use 'submit' or 'poll'.",
        }

    # ── 11. ue_context ──────────────────────────────────────────────
    if name == "ue_context":
        if _context_store is None:
            return {"success": False, "error": "Context store not initialised"}
        action = args.get("action", "")

        if action == "resume":
            payload = _context_store.get_resume_payload()
            return {"success": True, **payload}

        if action == "status":
            status = _context_store.get_status()
            return {"success": True, **status}

        if action == "history":
            limit = args.get("limit", 20)
            entries = _context_store.get_history(limit)
            return {"success": True, "entries": entries, "count": len(entries)}

        if action == "workset":
            workset = _context_store.get_workset()
            return {
                "success": True,
                "assets": list(workset.values()),
                "count": len(workset),
            }

        if action == "clear":
            _context_store.clear()
            return {
                "success": True,
                "message": "Context cleared (workset, history, op_count reset)",
            }

        return {
            "success": False,
            "error": f"Unknown action: '{action}'. Use 'resume', 'status', 'history', 'workset', or 'clear'.",
        }

    return {"success": False, "error": f"Unknown tool: {name}"}


# ── entry point ─────────────────────────────────────────────────────────


async def _run():
    global _context_store

    logger.info(
        "Starting ue-editor-mcp unified server (%d actions registered)",
        get_registry().count,
    )

    # Initialise context store
    ctx_dir = Path(__file__).parent.parent.parent / ".context"
    _context_store = ContextStore(ctx_dir)
    atexit.register(_context_store.shutdown)

    # Connect to Unreal and register context callback
    conn = get_connection()
    conn.on_state_change = _context_store._on_ue_state_change
    conn.connect()

    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options(),
        )


def main():
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    )
    try:
        asyncio.run(_run())
    except KeyboardInterrupt:
        logger.info("ue-editor-mcp stopped by user")
    finally:
        if _context_store is not None:
            _context_store.shutdown()
        conn = get_connection()
        conn.disconnect()


if __name__ == "__main__":
    main()

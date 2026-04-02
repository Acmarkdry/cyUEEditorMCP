"""
Tests for AnimGraph MCP action definitions.

Feature: animation-graph-read
"""

import json
import re
import sys
import types
from pathlib import Path

# Ensure Python package is importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))

# Stub out mcp dependencies so we can import without installing mcp
for mod_name in ("mcp", "mcp.server", "mcp.server.stdio", "mcp.types"):
    if mod_name not in sys.modules:
        sys.modules[mod_name] = types.ModuleType(mod_name)

_noop_decorator = lambda f=None: (lambda fn: fn) if f is None else f


class _StubServer:
    def __init__(self, *a, **kw):
        pass

    def list_tools(self):
        return _noop_decorator

    def call_tool(self):
        return _noop_decorator

    def create_initialization_options(self):
        return {}

    async def run(self, *a, **kw):
        pass


sys.modules["mcp.server"].Server = _StubServer
sys.modules["mcp.server.stdio"].stdio_server = None
for attr in ("Tool", "TextContent", "ImageContent"):
    setattr(
        sys.modules["mcp.types"],
        attr,
        type(attr, (), {"__init__": lambda self, **kw: None}),
    )

from ue_editor_mcp.registry import get_registry
from ue_editor_mcp.skills import load_skill, _WORKFLOWS_DIR

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_ANIMGRAPH_ID_PATTERN = re.compile(r"^animgraph\.\w+$")

_READ_ONLY_IDS = {
    "animgraph.list_graphs",
    "animgraph.describe_topology",
    "animgraph.get_state_machine",
    "animgraph.get_state_subgraph",
    "animgraph.get_transition_rule",
}

_DESTRUCTIVE_IDS = {
    "animgraph.remove_state",
    "animgraph.remove_transition",
}

_ALL_ANIMGRAPH_IDS = {
    "animgraph.list_graphs",
    "animgraph.describe_topology",
    "animgraph.get_state_machine",
    "animgraph.get_state_subgraph",
    "animgraph.get_transition_rule",
    "animgraph.create_blueprint",
    "animgraph.add_state_machine",
    "animgraph.add_state",
    "animgraph.remove_state",
    "animgraph.rename_state",
    "animgraph.add_transition",
    "animgraph.remove_transition",
    "animgraph.set_transition_priority",
    "animgraph.add_node",
    "animgraph.set_node_property",
    "animgraph.connect_nodes",
    "animgraph.disconnect_node",
    "animgraph.compile",
}


def _get_animgraph_actions():
    registry = get_registry()
    return [
        action
        for action_id, action in registry._actions.items()
        if action_id.startswith("animgraph.")
    ]


# ---------------------------------------------------------------------------
# Property 1: ActionDef 结构正确性
# Feature: animation-graph-read, Property 1: ActionDef 结构正确性
# ---------------------------------------------------------------------------

def test_property1_actiondef_structure():
    """All animgraph.* ActionDefs must have correct structure."""
    actions = _get_animgraph_actions()
    assert len(actions) > 0, "No animgraph actions found in registry"

    for action in actions:
        aid = action.id
        # id matches animgraph.\w+ pattern
        assert _ANIMGRAPH_ID_PATTERN.match(aid), (
            f"Action id '{aid}' does not match animgraph.\\w+ pattern"
        )
        # command is non-empty string
        assert isinstance(action.command, str) and action.command, (
            f"Action '{aid}' has empty command"
        )
        # tags contains "animgraph"
        assert "animgraph" in action.tags, (
            f"Action '{aid}' tags do not contain 'animgraph': {action.tags}"
        )
        # description is non-empty
        assert isinstance(action.description, str) and action.description, (
            f"Action '{aid}' has empty description"
        )
        # input_schema contains "type": "object"
        assert isinstance(action.input_schema, dict), (
            f"Action '{aid}' input_schema is not a dict"
        )
        assert action.input_schema.get("type") == "object", (
            f"Action '{aid}' input_schema missing 'type': 'object'"
        )
        # examples is non-empty
        assert action.examples and len(action.examples) > 0, (
            f"Action '{aid}' has no examples"
        )


# ---------------------------------------------------------------------------
# Property 2: Capabilities 分类正确性
# Feature: animation-graph-read, Property 2: Capabilities 分类正确性
# ---------------------------------------------------------------------------

def test_property2_capabilities_classification():
    """Read-only, write, and destructive actions must have correct capabilities."""
    actions = _get_animgraph_actions()
    assert len(actions) > 0

    for action in actions:
        aid = action.id
        caps = action.capabilities

        if aid in _READ_ONLY_IDS:
            assert caps == ("read",), (
                f"Read-only action '{aid}' should have capabilities=('read',), got {caps}"
            )
        elif aid in _DESTRUCTIVE_IDS:
            assert "write" in caps and "destructive" in caps, (
                f"Destructive action '{aid}' should have 'write' and 'destructive' in capabilities, got {caps}"
            )
        else:
            assert "write" in caps, (
                f"Write action '{aid}' should have 'write' in capabilities, got {caps}"
            )


# ---------------------------------------------------------------------------
# Property 3: input_schema JSON 序列化 round-trip
# Feature: animation-graph-read, Property 3: input_schema JSON 序列化 round-trip
# ---------------------------------------------------------------------------

def test_property3_input_schema_json_roundtrip():
    """input_schema must survive a JSON round-trip unchanged."""
    actions = _get_animgraph_actions()
    assert len(actions) > 0

    for action in actions:
        schema = action.input_schema
        serialized = json.dumps(schema)
        deserialized = json.loads(serialized)
        assert deserialized == schema, (
            f"Action '{action.id}' input_schema round-trip failed: "
            f"original={schema}, after_roundtrip={deserialized}"
        )


# ---------------------------------------------------------------------------
# Unit test: all animgraph action_ids exist in registry
# ---------------------------------------------------------------------------

def test_all_animgraph_action_ids_in_registry():
    """Every expected animgraph action_id must be registered."""
    registry = get_registry()
    for aid in _ALL_ANIMGRAPH_IDS:
        assert registry.get(aid) is not None, (
            f"Action '{aid}' not found in registry"
        )


# ---------------------------------------------------------------------------
# Unit test: animgraph skill loads and contains all action_ids
# ---------------------------------------------------------------------------

def test_animgraph_skill_loads_with_all_actions():
    """animgraph skill must load successfully and contain all animgraph action_ids."""
    skill_data = load_skill("animgraph")
    assert skill_data is not None, "animgraph skill failed to load"

    loaded_ids = {a["id"] for a in skill_data.get("actions", [])}
    for aid in _ALL_ANIMGRAPH_IDS:
        assert aid in loaded_ids, (
            f"animgraph skill is missing action '{aid}'"
        )


# ---------------------------------------------------------------------------
# Unit test: animgraph.md workflow file exists
# ---------------------------------------------------------------------------

def test_animgraph_workflow_file_exists():
    """animgraph.md workflow file must exist in the skills directory."""
    workflow_path = _WORKFLOWS_DIR / "animgraph.md"
    assert workflow_path.exists(), (
        f"Workflow file not found: {workflow_path}"
    )
    assert workflow_path.stat().st_size > 0, "animgraph.md is empty"

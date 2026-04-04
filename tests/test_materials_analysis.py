"""
Unit tests for Material Analysis & Creation actions (Python layer).

Validates ActionRegistry search, ActionDef completeness, and skills loading
without requiring a live UE editor connection.
"""

from __future__ import annotations

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
    def __init__(self, *a, **kw): pass
    def list_tools(self): return _noop_decorator
    def call_tool(self): return _noop_decorator
    def create_initialization_options(self): return {}
    async def run(self, *a, **kw): pass


sys.modules["mcp.server"].Server = _StubServer
sys.modules["mcp.server.stdio"].stdio_server = None
for attr in ("Tool", "TextContent", "ImageContent"):
    setattr(sys.modules["mcp.types"], attr, type(attr, (), {"__init__": lambda self, **kw: None}))

from ue_editor_mcp.registry import get_registry
from ue_editor_mcp.skills import load_skill

# The 7 new action IDs introduced by this feature
NEW_ACTION_IDS = [
    "material.analyze_complexity",
    "material.analyze_dependencies",
    "material.diagnose",
    "material.diff",
    "material.extract_parameters",
    "material.batch_create_instances",
    "material.replace_node",
]


def _search(query: str) -> list[str]:
    """Helper: run registry search and return list of action IDs."""
    registry = get_registry()
    results = registry.search(query)
    return [r["id"] for r in results]


# ---------------------------------------------------------------------------
# Search discoverability tests
# ---------------------------------------------------------------------------

def test_action_search_material_analyze():
    """ue_actions_search('material analyze') must include material.analyze_complexity."""
    ids = _search("material analyze")
    assert "material.analyze_complexity" in ids, (
        f"'material.analyze_complexity' not found in search results for 'material analyze': {ids}"
    )


def test_action_search_material_analysis():
    """ue_actions_search('material analysis') must return analysis-related actions."""
    ids = _search("material analysis")
    analysis_actions = {
        "material.analyze_complexity",
        "material.analyze_dependencies",
        "material.diagnose",
        "material.diff",
    }
    found = analysis_actions & set(ids)
    assert found, (
        f"No analysis actions found in search results for 'material analysis': {ids}"
    )


def test_action_search_material_create():
    """ue_actions_search('material create') must return creation helper actions."""
    ids = _search("material create")
    creation_actions = {
        "material.batch_create_instances",
        "material.create",
        "material.create_instance",
    }
    found = creation_actions & set(ids)
    assert found, (
        f"No creation actions found in search results for 'material create': {ids}"
    )


# ---------------------------------------------------------------------------
# ActionDef completeness tests
# ---------------------------------------------------------------------------

def test_actiondef_completeness():
    """All 7 new action IDs must have non-empty id, command, tags, description,
    input_schema, and at least 1 example."""
    registry = get_registry()
    for action_id in NEW_ACTION_IDS:
        action = registry.get(action_id)
        assert action is not None, f"Action '{action_id}' not found in registry"
        assert action.id, f"'{action_id}': id is empty"
        assert action.command, f"'{action_id}': command is empty"
        assert action.tags, f"'{action_id}': tags is empty"
        assert action.description, f"'{action_id}': description is empty"
        assert action.input_schema, f"'{action_id}': input_schema is empty"
        assert action.examples, f"'{action_id}': examples is empty (must have at least 1)"


def test_action_schema_has_required_fields():
    """Each new ActionDef's input_schema must have 'required' and 'properties' fields."""
    registry = get_registry()
    for action_id in NEW_ACTION_IDS:
        action = registry.get(action_id)
        assert action is not None, f"Action '{action_id}' not found in registry"
        schema = action.input_schema
        assert "properties" in schema, (
            f"'{action_id}': input_schema missing 'properties' key"
        )
        assert "required" in schema, (
            f"'{action_id}': input_schema missing 'required' key"
        )


# ---------------------------------------------------------------------------
# Skills loading test
# ---------------------------------------------------------------------------

def test_skills_materials_contains_new_actions():
    """Loading skills/materials.md content must contain keywords for new actions."""
    skill_data = load_skill("materials")
    assert skill_data is not None, "Failed to load 'materials' skill"

    workflows: str = skill_data.get("workflows", "")
    assert workflows, "materials skill has no workflow content"

    required_keywords = ["analyze_material_complexity", "diagnose_material", "batch_create_material_instances"]
    for keyword in required_keywords:
        assert keyword in workflows, (
            f"Keyword '{keyword}' not found in materials.md workflow content"
        )


if __name__ == "__main__":
    tests = [
        test_action_search_material_analyze,
        test_action_search_material_analysis,
        test_action_search_material_create,
        test_actiondef_completeness,
        test_action_schema_has_required_fields,
        test_skills_materials_contains_new_actions,
    ]
    failed = 0
    for t in tests:
        try:
            t()
            print(f"  PASS  {t.__name__}")
        except AssertionError as e:
            print(f"  FAIL  {t.__name__}: {e}")
            failed += 1
        except Exception as e:
            print(f"  ERROR {t.__name__}: {e}")
            failed += 1
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    sys.exit(1 if failed else 0)

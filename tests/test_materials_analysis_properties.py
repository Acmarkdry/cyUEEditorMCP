"""
Property-based tests for Material Analysis & Creation actions (Python layer).

Validates ActionDef structure and registry behavior without requiring a live
UE editor connection.

**Validates: Requirements 7.1, 7.2, 7.3, 7.4**
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

from hypothesis import given, settings
from hypothesis import strategies as st

from ue_editor_mcp.registry import get_registry
from ue_editor_mcp.registry.actions import _MATERIAL_ACTIONS

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

SEARCH_KEYWORDS = [
    "material analyze",
    "material analysis",
    "material create",
    "material batch",
]


# ---------------------------------------------------------------------------
# Property 1: ActionDef registration completeness
# **Validates: Requirements 7.1, 7.4**
# ---------------------------------------------------------------------------

@given(action_id=st.sampled_from(NEW_ACTION_IDS))
@settings(max_examples=50)
def test_actiondef_registration_completeness(action_id: str):
    """For any of the 7 new action IDs, the ActionDef has all required fields
    and examples is non-empty.

    **Validates: Requirements 7.1, 7.4**
    """
    registry = get_registry()
    action = registry.get(action_id)

    assert action is not None, f"Action '{action_id}' not found in registry"
    assert action.id, f"'{action_id}': id is empty"
    assert action.command, f"'{action_id}': command is empty"
    assert action.tags, f"'{action_id}': tags is empty"
    assert action.description, f"'{action_id}': description is empty"
    assert action.input_schema, f"'{action_id}': input_schema is empty"
    assert action.examples, f"'{action_id}': examples must be non-empty"


# ---------------------------------------------------------------------------
# Property 2: Search results are a subset of all registered actions
# **Validates: Requirements 7.2, 7.3**
# ---------------------------------------------------------------------------

@given(keyword=st.sampled_from(SEARCH_KEYWORDS))
@settings(max_examples=50)
def test_action_search_returns_subset(keyword: str):
    """For any search keyword, results are a subset of all registered actions.

    **Validates: Requirements 7.2, 7.3**
    """
    registry = get_registry()
    all_ids = set(registry.all_ids)
    results = registry.search(keyword)
    result_ids = {r["id"] for r in results}

    assert result_ids.issubset(all_ids), (
        f"Search '{keyword}' returned IDs not in registry: {result_ids - all_ids}"
    )


# ---------------------------------------------------------------------------
# Property 3: All action IDs in _MATERIAL_ACTIONS are unique
# **Validates: Requirements 7.1**
# ---------------------------------------------------------------------------

def test_action_ids_are_unique():
    """All action IDs in _MATERIAL_ACTIONS are unique (no duplicates).

    **Validates: Requirements 7.1**
    """
    ids = [a.id for a in _MATERIAL_ACTIONS]
    assert len(ids) == len(set(ids)), (
        f"Duplicate action IDs found in _MATERIAL_ACTIONS: "
        f"{[x for x in ids if ids.count(x) > 1]}"
    )


# ---------------------------------------------------------------------------
# Property 4: All 7 new actions have "material" in their tags
# **Validates: Requirements 7.4**
# ---------------------------------------------------------------------------

@given(action_id=st.sampled_from(NEW_ACTION_IDS))
@settings(max_examples=50)
def test_new_action_tags_contain_material(action_id: str):
    """All 7 new actions have 'material' in their tags tuple.

    **Validates: Requirements 7.4**
    """
    registry = get_registry()
    action = registry.get(action_id)

    assert action is not None, f"Action '{action_id}' not found in registry"
    assert "material" in action.tags, (
        f"'{action_id}': 'material' not found in tags {action.tags}"
    )


if __name__ == "__main__":
    tests = [
        test_actiondef_registration_completeness,
        test_action_search_returns_subset,
        test_action_ids_are_unique,
        test_new_action_tags_contain_material,
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

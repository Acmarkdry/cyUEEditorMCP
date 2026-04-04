"""Tests for the skill system — catalog listing and loading."""

import sys
import types
from pathlib import Path

# Ensure Python package is importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))

# Stub out mcp dependencies so we can import without installing mcp
for mod_name in ("mcp", "mcp.server", "mcp.server.stdio", "mcp.types"):
    if mod_name not in sys.modules:
        sys.modules[mod_name] = types.ModuleType(mod_name)
# Provide minimal stubs for names used at import time
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
from ue_editor_mcp.skills import get_skill_list, load_skill, SKILL_DEFS, _WORKFLOWS_DIR


def test_skill_list_returns_all_skills():
    skills = get_skill_list()
    assert len(skills) == len(SKILL_DEFS)
    for s in skills:
        assert "skill_id" in s
        assert "name" in s
        assert "description" in s
        assert "action_count" in s
        assert s["action_count"] > 0


def test_skill_list_ids_are_unique():
    skills = get_skill_list()
    ids = [s["skill_id"] for s in skills]
    assert len(ids) == len(set(ids)), f"Duplicate skill IDs: {ids}"


def test_load_all_skills():
    for skill_def in SKILL_DEFS:
        data = load_skill(skill_def.id)
        assert data is not None, f"Failed to load skill: {skill_def.id}"
        assert data["skill_id"] == skill_def.id
        assert data["action_count"] > 0
        assert isinstance(data["actions"], list)
        assert len(data["actions"]) == data["action_count"]
        # Each action should have id, command, input_schema
        for action in data["actions"]:
            assert "id" in action
            assert "command" in action
            assert "input_schema" in action


def test_load_unknown_skill_returns_none():
    assert load_skill("nonexistent-skill") is None


def test_all_skill_action_ids_exist_in_registry():
    """Every action_id referenced by a skill must exist in the registry."""
    registry = get_registry()
    missing = []
    for skill_def in SKILL_DEFS:
        for aid in skill_def.action_ids:
            if registry.get(aid) is None:
                missing.append((skill_def.id, aid))
    assert not missing, f"Actions referenced by skills but missing from registry: {missing}"


# Actions whose C++ handlers were removed (replaced by ue_python_exec)
# but whose ActionDefs still exist in registry for error-message clarity.
_DEPRECATED_ACTION_IDS = {
    "blueprint.create", "blueprint.compile", "blueprint.set_property",
    "blueprint.spawn_actor", "blueprint.set_parent_class",
    "blueprint.add_interface", "blueprint.remove_interface",
    "blueprint.add_component", "blueprint.create_colored_material",
    "component.set_property", "component.set_static_mesh",
    "component.set_physics",
    "editor.get_actors", "editor.find_actors",
    "editor.spawn_actor", "editor.delete_actor",
    "editor.set_actor_transform", "editor.get_actor_properties",
    "editor.set_actor_property",
    "editor.focus_viewport", "editor.get_viewport_transform",
    "editor.set_viewport_transform",
    "editor.save_all", "editor.list_assets",
    "editor.rename_assets", "editor.get_selected_assets",
    "editor.rename_actor_label", "editor.set_actor_folder",
    "editor.select_actors", "editor.get_outliner_tree",
    "editor.open_asset_editor",
    "editor.start_pie", "editor.stop_pie", "editor.get_pie_state",
    "material.create", "material.add_expression",
    "material.connect_expressions", "material.connect_to_output",
    "material.set_expression_property", "material.compile",
    "material.create_instance",
    "material.create_post_process_volume",
    "material.apply_to_component", "material.apply_to_actor",
    "batch.execute",
}


def test_all_registry_actions_covered_by_skills():
    """Every non-deprecated action in the registry should be covered by at least one skill."""
    registry = get_registry()
    all_skill_action_ids = set()
    for skill_def in SKILL_DEFS:
        all_skill_action_ids.update(skill_def.action_ids)

    uncovered = []
    for aid in registry.all_ids:
        if aid not in all_skill_action_ids and aid not in _DEPRECATED_ACTION_IDS:
            uncovered.append(aid)

    assert not uncovered, f"Registry actions not covered by any skill: {uncovered}"


def test_workflow_files_exist():
    """Workflow markdown files referenced by skills should exist."""
    for skill_def in SKILL_DEFS:
        if skill_def.workflows_file:
            path = _WORKFLOWS_DIR / skill_def.workflows_file
            assert path.exists(), f"Workflow file missing: {path}"


if __name__ == "__main__":
    tests = [
        test_skill_list_returns_all_skills,
        test_skill_list_ids_are_unique,
        test_load_all_skills,
        test_load_unknown_skill_returns_none,
        test_all_skill_action_ids_exist_in_registry,
        test_all_registry_actions_covered_by_skills,
        test_workflow_files_exist,
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

"""
Skill system — domain-grouped action catalogs for AI on-demand loading.

Instead of search → schema → run (3 round-trips), AI uses:
  1. ue_skills(action="list")  → see skill catalog index
  2. ue_skills(action="load", skill_id="materials")  → get full schemas + workflows
  3. ue_actions_run / ue_batch  → execute directly

Skills are auto-generated from ActionDef registry data + hand-written
workflow tips stored in markdown files under skills/.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from ..registry import get_registry, ActionDef


@dataclass(frozen=True)
class SkillDef:
    """Definition of a skill (domain-grouped action catalog)."""

    id: str  # e.g. "blueprint-core"
    name: str  # human-readable name
    description: str  # one-line summary
    action_ids: tuple[str, ...]  # ordered list of action IDs in this skill
    workflows_file: str | None = None  # optional markdown filename for workflow tips


# ── Skill definitions ───────────────────────────────────────────────────

SKILL_DEFS: list[SkillDef] = [
    SkillDef(
        id="blueprint-core",
        name="Blueprint Core",
        description="Blueprint introspection and full snapshot (create/compile/modify via ue_python_exec)",
        action_ids=(
            "blueprint.get_summary",
            "blueprint.describe_full",
            "python.exec",
        ),
        workflows_file="blueprint-core.md",
    ),
    SkillDef(
        id="graph-nodes",
        name="Graph Nodes & Wiring",
        description="Event/function/variable/flow-control nodes, graph connections, comments, selection, collapse, patch system, and cross-graph transfer",
        action_ids=(
            # Events
            "node.add_event",
            "node.add_custom_event",
            "node.add_custom_event_for_delegate",
            "node.add_input_action",
            "node.add_enhanced_input_action",
            # Dispatchers
            "dispatcher.create",
            "dispatcher.call",
            "dispatcher.bind",
            "dispatcher.create_event",
            # Functions
            "node.add_function_call",
            "node.add_spawn_actor",
            "node.set_pin_default",
            "node.set_object_property",
            "node.add_get_subsystem",
            # Variables
            "variable.create",
            "variable.add_getter",
            "variable.add_setter",
            "variable.add_local",
            # References
            "node.add_self_reference",
            "node.add_component_reference",
            "node.add_cast",
            # Flow control
            "node.add_branch",
            "node.add_macro",
            "node.add_sequence",
            # Structs & switches
            "node.add_make_struct",
            "node.add_break_struct",
            "node.add_switch_string",
            "node.add_switch_int",
            # Graph operations
            "graph.connect_nodes",
            "graph.find_nodes",
            "graph.delete_node",
            "graph.get_node_pins",
            "graph.disconnect_pin",
            "graph.move_node",
            "graph.add_reroute",
            "graph.add_comment",
            "graph.auto_comment",
            # Describe & selection
            "graph.describe",
            "graph.describe_enhanced",
            "graph.get_selected_nodes",
            "graph.set_selected_nodes",
            "graph.batch_select_and_act",
            # Collapse / refactor
            "graph.collapse_selection_to_function",
            "graph.collapse_selection_to_macro",
            # Patch system
            "graph.apply_patch",
            "graph.validate_patch",
            # Cross-graph transfer
            "graph.export_nodes",
            "graph.import_nodes",
        ),
        workflows_file="graph-nodes.md",
    ),
    SkillDef(
        id="variables-functions",
        name="Variable & Function Management",
        description="Variable CRUD/metadata, function create/delete/rename, macro rename",
        action_ids=(
            "variable.set_default",
            "variable.delete",
            "variable.rename",
            "variable.set_metadata",
            "function.create",
            "function.call",
            "function.delete",
            "function.rename",
            "macro.rename",
        ),
        workflows_file="variables-functions.md",
    ),
    SkillDef(
        id="materials",
        name="Material System",
        description="Material analysis, diagnostics, layout, comments (create/compile/apply via ue_python_exec)",
        action_ids=(
            "material.set_property",
            "material.get_summary",
            "material.remove_expression",
            "material.auto_layout",
            "material.auto_comment",
            "material.refresh_editor",
            "material.get_selected_nodes",
            "material.analyze_complexity",
            "material.analyze_dependencies",
            "material.diagnose",
            "material.diff",
            "material.extract_parameters",
            "material.batch_create_instances",
            "material.replace_node",
            "python.exec",
        ),
        workflows_file="materials.md",
    ),
    SkillDef(
        id="umg-widgets",
        name="UMG Widgets & MVVM",
        description="Widget Blueprint CRUD, 24 component types, hierarchy, properties, MVVM bindings, and input system",
        action_ids=(
            "widget.create",
            "widget.delete",
            "widget.add_component",
            "widget.bind_event",
            "component.bind_event",
            "widget.add_to_viewport",
            "widget.set_text_binding",
            "widget.list_components",
            "widget.get_tree",
            "widget.set_properties",
            "widget.set_text",
            "widget.set_combo_options",
            "widget.set_slider",
            "widget.reparent",
            "widget.add_child",
            "widget.delete_component",
            "widget.rename_component",
            # MVVM
            "widget.mvvm_add_viewmodel",
            "widget.mvvm_add_binding",
            "widget.mvvm_get_bindings",
            "widget.mvvm_remove_binding",
            "widget.mvvm_remove_viewmodel",
            # Input
            "input.create_mapping",
            "input.create_action",
            "input.create_mapping_context",
            "input.add_key_mapping",
        ),
        workflows_file="umg-widgets.md",
    ),
    SkillDef(
        id="editor-level",
        name="Editor & Level Management",
        description="Editor diagnostics, logs, thumbnails, source control diff, undo/redo, screenshots, level info (actors/viewport/PIE via ue_python_exec)",
        action_ids=(
            "editor.get_selected_asset_thumbnail",
            "editor.diff_against_depot",
            "editor.get_asset_history",
            "editor.get_logs",
            "editor.is_ready",
            "editor.request_shutdown",
            "editor.clear_logs",
            "editor.assert_log",
            # P7: Undo/Redo
            "editor.undo",
            "editor.redo",
            "editor.get_undo_history",
            # P7: Screenshots
            "editor.take_screenshot",
            "editor.take_pie_screenshot",
            # P10: Level info
            "level.list_sublevels",
            "level.get_world_settings",
            "python.exec",
        ),
        workflows_file="editor-level.md",
    ),
    SkillDef(
        id="layout",
        name="Auto Layout",
        description="Blueprint graph and material graph auto-layout with Sugiyama algorithm",
        action_ids=(
            "layout.auto_selected",
            "layout.auto_subtree",
            "layout.auto_blueprint",
            "layout.layout_and_comment",
        ),
        workflows_file="layout.md",
    ),
    SkillDef(
        id="python-api",
        name="Python API (ue_python_exec)",
        description="Execute arbitrary Python in Unreal's embedded interpreter — replaces 45+ former C++ actions for actors, blueprints, materials, viewport, PIE, assets",
        action_ids=("python.exec",),
        workflows_file="python-api.md",
    ),
    SkillDef(
        id="animgraph",
        name="AnimGraph 动画图",
        description="Animation Blueprint 完整操作：读取 AnimGraph 结构、状态机、转换规则；创建动画蓝图；添加/删除状态和转换；添加动画节点；连接/断开节点；设置节点属性；编译动画蓝图。",
        action_ids=(
            # Read
            "animgraph.list_graphs",
            "animgraph.describe_topology",
            "animgraph.get_state_machine",
            "animgraph.get_state_subgraph",
            "animgraph.get_transition_rule",
            # Create
            "animgraph.create_blueprint",
            "animgraph.add_state_machine",
            # Modify — states
            "animgraph.add_state",
            "animgraph.remove_state",
            "animgraph.rename_state",
            # Modify — transitions
            "animgraph.add_transition",
            "animgraph.remove_transition",
            "animgraph.set_transition_priority",
            # Modify — nodes
            "animgraph.add_node",
            "animgraph.set_node_property",
            "animgraph.connect_nodes",
            "animgraph.disconnect_node",
            # Compile
            "animgraph.compile",
        ),
        workflows_file="animgraph.md",
    ),
    SkillDef(
        id="niagara",
        name="Niagara Particle System",
        description="Create, describe, modify, and compile Niagara particle systems — emitters, modules, parameters",
        action_ids=(
            "niagara.create_system",
            "niagara.describe_system",
            "niagara.add_emitter",
            "niagara.remove_emitter",
            "niagara.get_modules",
            "niagara.set_module_param",
            "niagara.compile",
        ),
        workflows_file="niagara.md",
    ),
    SkillDef(
        id="datatable",
        name="DataTable",
        description="Create, describe, and manipulate DataTables — add/delete rows, export to JSON",
        action_ids=(
            "datatable.create",
            "datatable.describe",
            "datatable.add_row",
            "datatable.delete_row",
            "datatable.export_json",
        ),
        workflows_file="datatable.md",
    ),
    SkillDef(
        id="sequencer",
        name="Level Sequencer",
        description="Create and edit Level Sequences — add possessable bindings, tracks, set playback range",
        action_ids=(
            "sequencer.create",
            "sequencer.describe",
            "sequencer.add_possessable",
            "sequencer.add_track",
            "sequencer.set_range",
        ),
        workflows_file="sequencer.md",
    ),
    SkillDef(
        id="profiler-test",
        name="Profiling & Automation Testing",
        description="Frame/memory stats profiling and UE automation test runner",
        action_ids=(
            "profiler.frame_stats",
            "profiler.memory_stats",
            "test.list",
            "test.run",
        ),
        workflows_file="profiler-test.md",
    ),
]

# Build lookup
_SKILL_MAP: dict[str, SkillDef] = {s.id: s for s in SKILL_DEFS}
_WORKFLOWS_DIR = Path(__file__).parent


def get_skill_list() -> list[dict[str, Any]]:
    """Return lightweight skill catalog index for AI discovery."""
    registry = get_registry()
    result = []
    for skill in SKILL_DEFS:
        # Count only actions that actually exist in registry
        valid_count = sum(
            1 for aid in skill.action_ids if registry.get(aid) is not None
        )
        result.append(
            {
                "skill_id": skill.id,
                "name": skill.name,
                "description": skill.description,
                "action_count": valid_count,
            }
        )
    return result


def load_skill(skill_id: str) -> dict[str, Any] | None:
    """Load full skill content: action schemas + workflow tips."""
    skill = _SKILL_MAP.get(skill_id)
    if skill is None:
        return None

    registry = get_registry()

    # Build action schemas
    actions = []
    for aid in skill.action_ids:
        schema = registry.schema(aid)
        if schema:
            actions.append(schema)

    # Load workflow tips if available
    workflows = ""
    if skill.workflows_file:
        wf_path = _WORKFLOWS_DIR / skill.workflows_file
        if wf_path.exists():
            workflows = wf_path.read_text(encoding="utf-8")

    return {
        "skill_id": skill.id,
        "name": skill.name,
        "description": skill.description,
        "action_count": len(actions),
        "actions": actions,
        "workflows": workflows,
    }

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
    id: str                          # e.g. "blueprint-core"
    name: str                        # human-readable name
    description: str                 # one-line summary
    action_ids: tuple[str, ...]      # ordered list of action IDs in this skill
    workflows_file: str | None = None  # optional markdown filename for workflow tips


# ── Skill definitions ───────────────────────────────────────────────────

SKILL_DEFS: list[SkillDef] = [
    SkillDef(
        id="blueprint-core",
        name="Blueprint Core",
        description="Blueprint CRUD, components, interfaces, compile, introspection, and full snapshot",
        action_ids=(
            "blueprint.create", "blueprint.compile", "blueprint.set_property",
            "blueprint.spawn_actor", "blueprint.set_parent_class",
            "blueprint.add_interface", "blueprint.remove_interface",
            "blueprint.add_component", "blueprint.get_summary",
            "blueprint.describe_full", "blueprint.create_colored_material",
            "component.set_property", "component.set_static_mesh",
            "component.set_physics", "component.bind_event",
        ),
        workflows_file="blueprint-core.md",
    ),
    SkillDef(
        id="graph-nodes",
        name="Graph Nodes & Wiring",
        description="Event/function/variable/flow-control nodes, graph connections, comments, selection, collapse, patch system, and cross-graph transfer",
        action_ids=(
            # Events
            "node.add_event", "node.add_custom_event",
            "node.add_custom_event_for_delegate",
            "node.add_input_action", "node.add_enhanced_input_action",
            # Dispatchers
            "dispatcher.create", "dispatcher.call",
            "dispatcher.bind", "dispatcher.create_event",
            # Functions
            "node.add_function_call", "node.add_spawn_actor",
            "node.set_pin_default", "node.set_object_property",
            "node.add_get_subsystem",
            # Variables
            "variable.create", "variable.add_getter",
            "variable.add_setter", "variable.add_local",
            # References
            "node.add_self_reference", "node.add_component_reference",
            "node.add_cast",
            # Flow control
            "node.add_branch", "node.add_macro", "node.add_sequence",
            # Structs & switches
            "node.add_make_struct", "node.add_break_struct",
            "node.add_switch_string", "node.add_switch_int",
            # Graph operations
            "graph.connect_nodes", "graph.find_nodes",
            "graph.delete_node", "graph.get_node_pins",
            "graph.disconnect_pin", "graph.move_node",
            "graph.add_reroute", "graph.add_comment",
            "graph.auto_comment",
            # Describe & selection
            "graph.describe", "graph.describe_enhanced",
            "graph.get_selected_nodes", "graph.set_selected_nodes",
            "graph.batch_select_and_act",
            # Collapse / refactor
            "graph.collapse_selection_to_function",
            "graph.collapse_selection_to_macro",
            # Patch system
            "graph.apply_patch", "graph.validate_patch",
            # Cross-graph transfer
            "graph.export_nodes", "graph.import_nodes",
        ),
        workflows_file="graph-nodes.md",
    ),
    SkillDef(
        id="variables-functions",
        name="Variable & Function Management",
        description="Variable CRUD/metadata, function create/delete/rename, macro rename",
        action_ids=(
            "variable.set_default", "variable.delete",
            "variable.rename", "variable.set_metadata",
            "function.create", "function.call",
            "function.delete", "function.rename",
            "macro.rename",
        ),
        workflows_file="variables-functions.md",
    ),
    SkillDef(
        id="materials",
        name="Material System",
        description="Material creation, expressions, wiring, compile diagnostics, instances, layout, comments, and level application",
        action_ids=(
            "material.create", "material.add_expression",
            "material.connect_expressions", "material.connect_to_output",
            "material.set_expression_property", "material.compile",
            "material.create_instance", "material.set_property",
            "material.create_post_process_volume",
            "material.get_summary", "material.remove_expression",
            "material.auto_layout", "material.auto_comment",
            "material.apply_to_component", "material.apply_to_actor",
            "material.refresh_editor", "material.get_selected_nodes",
        ),
        workflows_file="materials.md",
    ),
    SkillDef(
        id="umg-widgets",
        name="UMG Widgets & MVVM",
        description="Widget Blueprint CRUD, 24 component types, hierarchy, properties, MVVM bindings, and input system",
        action_ids=(
            "widget.create", "widget.delete",
            "widget.add_component", "widget.bind_event",
            "widget.add_to_viewport", "widget.set_text_binding",
            "widget.list_components", "widget.get_tree",
            "widget.set_properties", "widget.set_text",
            "widget.set_combo_options", "widget.set_slider",
            "widget.reparent", "widget.add_child",
            "widget.delete_component", "widget.rename_component",
            # MVVM
            "widget.mvvm_add_viewmodel", "widget.mvvm_add_binding",
            "widget.mvvm_get_bindings", "widget.mvvm_remove_binding",
            "widget.mvvm_remove_viewmodel",
            # Input
            "input.create_mapping", "input.create_action",
            "input.create_mapping_context", "input.add_key_mapping",
        ),
        workflows_file="umg-widgets.md",
    ),
    SkillDef(
        id="editor-level",
        name="Editor & Level Management",
        description="Actors, viewport, assets, save, PIE control, logs, outliner, source control diff, and batch execution",
        action_ids=(
            "editor.get_actors", "editor.find_actors",
            "editor.spawn_actor", "editor.delete_actor",
            "editor.set_actor_transform", "editor.get_actor_properties",
            "editor.set_actor_property",
            "editor.focus_viewport", "editor.get_viewport_transform",
            "editor.set_viewport_transform",
            "editor.save_all", "editor.list_assets",
            "editor.rename_assets", "editor.get_selected_asset_thumbnail",
            "editor.get_selected_assets",
            "editor.diff_against_depot", "editor.get_asset_history",
            "editor.get_logs",
            "editor.is_ready", "editor.request_shutdown",
            # PIE
            "editor.start_pie", "editor.stop_pie", "editor.get_pie_state",
            # Logs
            "editor.clear_logs", "editor.assert_log",
            # Outliner
            "editor.rename_actor_label", "editor.set_actor_folder",
            "editor.select_actors", "editor.get_outliner_tree",
            # Asset editor
            "editor.open_asset_editor",
            # Batch
            "batch.execute",
        ),
        workflows_file="editor-level.md",
    ),
    SkillDef(
        id="layout",
        name="Auto Layout",
        description="Blueprint graph and material graph auto-layout with Sugiyama algorithm",
        action_ids=(
            "layout.auto_selected", "layout.auto_subtree",
            "layout.auto_blueprint", "layout.layout_and_comment",
        ),
        workflows_file="layout.md",
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
        valid_count = sum(1 for aid in skill.action_ids if registry.get(aid) is not None)
        result.append({
            "skill_id": skill.id,
            "name": skill.name,
            "description": skill.description,
            "action_count": valid_count,
        })
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

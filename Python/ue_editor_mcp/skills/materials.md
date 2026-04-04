# Material System — Workflow Tips

## Full Material Creation Pipeline (via ue_python_exec)

> **Note**: Material creation, expression wiring, compilation, and application
> are now handled through `ue_python_exec`. The retained C++ actions focus on
> analysis, diagnostics, layout, and advanced editor operations.

```python
# Create a basic emissive material via Python
import unreal
factory = unreal.MaterialFactoryNew()
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = asset_tools.create_asset("M_Glow", "/Game/Materials", None, factory)

# For complex expression graphs, use the retained C++ actions:
# material.get_summary, material.auto_layout, material.auto_comment
_result = mat.get_path_name() if mat else "failed"
```

## Hybrid Workflow: Python + Retained C++ Actions

The recommended workflow combines Python for creation/application with
retained C++ actions for analysis, layout, and diagnostics:

```
# Step 1: Create material via ue_python_exec
# Step 2: Analyze with material.get_summary (C++ action)
# Step 3: Auto-organize with material.auto_layout (C++ action)
# Step 4: Diagnose issues with material.diagnose (C++ action)
# Step 5: Apply to actor via ue_python_exec
```

## Retained C++ Actions

| Action | Purpose |
|---|---|
| `material.get_summary` | Full graph structure inspection |
| `material.set_property` | Set material domain/blend mode properties |
| `material.remove_expression` | Remove expression nodes |
| `material.auto_layout` | Organize graph layout after modifications |
| `material.auto_comment` | Auto-generate comment boxes |
| `material.refresh_editor` | Update open editor UI after programmatic changes |
| `material.get_selected_nodes` | Query selected nodes in material editor |
| `material.analyze_complexity` | Node count, shader instructions, texture samples |
| `material.analyze_dependencies` | External asset dependencies |
| `material.diagnose` | Common issue detection (orphan nodes, etc.) |
| `material.diff` | Compare two materials structurally |
| `material.extract_parameters` | Discover all parameters and defaults |
| `material.batch_create_instances` | Create multiple instances in one call |
| `material.replace_node` | Swap expression node while preserving connections |

## Material Analysis Workflow

Use the analysis actions to inspect and compare materials before making changes.

```
# Check complexity and performance budget
ue_actions_run(action_id="material.analyze_complexity", params={material_name: "M_Character"})
# → node_count, node_type_distribution, connection_count, shader_instructions{vs, ps}, texture_samples[]

# Inspect external dependencies (textures, material functions, level references)
ue_actions_run(action_id="material.analyze_dependencies", params={material_name: "M_Character"})
# → external_assets[]{type, path, node_name}, level_references[]{actor_name, component_name}

# Diagnose common issues (orphan nodes, excessive samples, domain/blend mode mismatches)
ue_actions_run(action_id="material.diagnose", params={material_name: "M_Character"})
# → status("healthy"|"has_issues"), diagnostics[]{severity, code, message, node_name?}

# Diff two materials to spot structural differences
ue_actions_run(action_id="material.diff", params={material_name_a: "M_Base", material_name_b: "M_Base_V2"})
# → summary{node_count_diff, connection_count_diff}, property_diffs[], parameters_only_in_a[], parameters_only_in_b[]
```

## Batch Instantiation Workflow

Extract parameters from a master material, then create multiple instances in one call.

```
# Step 1 — discover all parameters and their defaults
ue_actions_run(action_id="material.extract_parameters", params={material_name: "M_Master"})
# → parameters[]{name, type, default_value, group, sort_priority}

# Step 2 — batch-create instances (failures are isolated; batch continues)
ue_actions_run(action_id="material.batch_create_instances", params={
    parent_material: "M_Master",
    instances: [
        {name: "MI_Red", scalar_parameters: {Roughness: 0.2}, vector_parameters: {BaseColor: [1,0,0,1]}},
        {name: "MI_Blue", scalar_parameters: {Roughness: 0.5}, vector_parameters: {BaseColor: [0,0,1,1]}}
    ]
})
```

## Applying Materials (via ue_python_exec)

```python
import unreal
mat = unreal.load_asset("/Game/Materials/M_Glow")
actors = unreal.GameplayStatics.get_all_actors_of_class(
    unreal.EditorLevelLibrary.get_editor_world(), unreal.StaticMeshActor)
for a in actors:
    if a.get_name() == "MyMesh":
        a.static_mesh_component.set_material(0, mat)
        break
_result = "applied"
```

## Key Patterns

- Use `material.get_summary` to inspect full graph structure before modifications
- `material.auto_layout` after batch modifications to organize the graph
- `material.refresh_editor` to update the open editor UI after programmatic changes
- For creating/compiling/applying materials → use `ue_python_exec`
- For analysis/diagnostics/layout → use retained C++ actions
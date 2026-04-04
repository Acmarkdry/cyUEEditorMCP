# Material System — Workflow Tips

## Full Material Creation Pipeline

```
ue_batch(actions=[
  {action_id: "material.create", params: {material_name: "M_Glow", domain: "Surface", blend_mode: "Translucent"}},
  {action_id: "material.add_expression", params: {material_name: "M_Glow", expression_class: "VectorParameter", node_name: "color_param", properties: {ParameterName: "BaseColor", DefaultValue: "(R=0.0,G=0.5,B=1.0,A=1.0)"}}},
  {action_id: "material.add_expression", params: {material_name: "M_Glow", expression_class: "ScalarParameter", node_name: "intensity", properties: {ParameterName: "Intensity", DefaultValue: "5.0"}}},
  {action_id: "material.add_expression", params: {material_name: "M_Glow", expression_class: "Multiply", node_name: "mult"}},
  {action_id: "material.connect_expressions", params: {material_name: "M_Glow", source_node: "color_param", target_node: "mult", target_input: "A"}},
  {action_id: "material.connect_expressions", params: {material_name: "M_Glow", source_node: "intensity", target_node: "mult", target_input: "B"}},
  {action_id: "material.connect_to_output", params: {material_name: "M_Glow", source_node: "mult", material_property: "EmissiveColor"}},
  {action_id: "material.compile", params: {material_name: "M_Glow"}}
])
```

## Expression Types (~50 supported)

**Parameters**: ScalarParameter, VectorParameter, TextureParameter, TextureSampleParameter2D, StaticSwitchParameter, StaticComponentMaskParameter
**Constants**: Constant, Constant2Vector, Constant3Vector, Constant4Vector
**Math**: Add, Subtract, Multiply, Divide, Power, Sqrt, Abs, Min, Max, Clamp, Saturate, Floor, Ceil, Frac, OneMinus, Step, SmoothStep
**Trig**: Sin, Cos
**Vector**: DotProduct, CrossProduct, Normalize, AppendVector, ComponentMask
**Procedural**: Noise, Time, Panner
**Scene**: SceneTexture, SceneDepth, ScreenPosition, TextureCoordinate, TextureSample, PixelDepth, WorldPosition, CameraPosition
**Control**: If, Lerp
**Derivative**: DDX, DDY
**Custom**: Custom (HLSL code with dynamic inputs/outputs)
**Function**: MaterialFunctionCall (reference UMaterialFunction assets)

## Material Output Properties

BaseColor, EmissiveColor, Metallic, Roughness, Specular, Normal,
Opacity, OpacityMask, WorldPositionOffset, AmbientOcclusion, Refraction

## Compile Diagnostics

- `material.compile` waits for shader compilation and returns real `errors[]`
- Each error includes: message, expression_name, expression_class, node_name
- For additional context: `ue_logs_tail(source="editor", category="LogMaterial", min_verbosity="Error")`

## Key Patterns

- Use `material.get_summary` to inspect full graph structure before modifications
- `material.auto_layout` after batch modifications to organize the graph
- `material.refresh_editor` to update the open editor UI after programmatic changes
- `material.create_instance` supports scalar/vector/texture/static_switch parameter overrides
- `material.apply_to_actor` applies to ALL PrimitiveComponents on an actor

## Material Analysis Workflow

Use the analysis actions to inspect and compare materials before making changes.

```
# Check complexity and performance budget
ue_call("analyze_material_complexity", {"material_name": "M_Character"})
# → node_count, node_type_distribution, connection_count, shader_instructions{vs, ps}, texture_samples[]

# Inspect external dependencies (textures, material functions, level references)
ue_call("analyze_material_dependencies", {"material_name": "M_Character"})
# → external_assets[]{type, path, node_name}, level_references[]{actor_name, component_name}

# Diagnose common issues (orphan nodes, excessive samples, domain/blend mode mismatches)
ue_call("diagnose_material", {"material_name": "M_Character"})
# → status("healthy"|"has_issues"), diagnostics[]{severity, code, message, node_name?}

# Diff two materials to spot structural differences
ue_call("diff_materials", {"material_name_a": "M_Base", "material_name_b": "M_Base_V2"})
# → summary{node_count_diff, connection_count_diff}, property_diffs[], parameters_only_in_a[], parameters_only_in_b[]
```

## Batch Instantiation Workflow

Extract parameters from a master material, then create multiple instances in one call.

```
# Step 1 — discover all parameters and their defaults
ue_call("extract_material_parameters", {"material_name": "M_Master"})
# → parameters[]{name, type, default_value, group, sort_priority}

# Step 2 — batch-create instances (failures are isolated; batch continues)
ue_call("batch_create_material_instances", {
    "parent_material": "M_Master",
    "instances": [
        {
            "name": "MI_Red",
            "path": "/Game/Materials/Instances",
            "scalar_parameters": {"Roughness": 0.2, "Metallic": 0.0},
            "vector_parameters": {"BaseColor": [1.0, 0.0, 0.0, 1.0]}
        },
        {
            "name": "MI_Blue",
            "scalar_parameters": {"Roughness": 0.5},
            "vector_parameters": {"BaseColor": [0.0, 0.0, 1.0, 1.0]},
            "texture_parameters": {"DetailMap": "/Game/Textures/T_Detail"}
        },
        {
            "name": "MI_Metal",
            "scalar_parameters": {"Roughness": 0.1, "Metallic": 1.0},
            "static_switch_parameters": {"UseDetail": True}
        }
    ]
})
# → created_count, failed_count, results[]{name, path?, success, error?}
```

## Node Replacement Workflow

Swap an existing expression node for a different type while preserving connections.

```
# Replace a Multiply node with a Lerp, keeping existing wired connections
ue_call("replace_material_node", {
    "material_name": "M_Glow",
    "node_name": "blend_node",
    "new_expression_class": "Lerp",
    "new_properties": {"ConstAlpha": 0.5}
})
# → replaced_node, new_node, new_expression_class,
#   migrated_connections[], failed_connections[], compile_result{}

# After replacement, refresh the editor to see changes
ue_call("refresh_material_editor", {"material_name": "M_Glow"})
```

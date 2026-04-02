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

# Blueprint Core — Workflow Tips

## Typical Creation Flow

```
ue_batch(actions=[
  {action_id: "blueprint.create", params: {name: "BP_Player", parent_class: "Character"}},
  {action_id: "blueprint.add_component", params: {blueprint_name: "BP_Player", component_type: "CapsuleComponent", component_name: "Capsule"}},
  {action_id: "blueprint.add_component", params: {blueprint_name: "BP_Player", component_type: "SkeletalMeshComponent", component_name: "Mesh"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "BP_Player"}}
])
```

## Key Patterns

- Always end a creation/modification sequence with `blueprint.compile`
- Use `blueprint.describe_full` for a single-call complete snapshot (replaces summary + N×describe)
- `blueprint.get_summary` is lighter if you only need variable/component/interface lists
- `component.bind_event` creates UK2Node_ComponentBoundEvent — the component must exist as a UPROPERTY

## Compile Diagnostics

1. `blueprint.compile` → returns status + error_count + warning_count
2. If errors, use `editor.get_logs` with `category="LogBlueprint"` + `min_verbosity="Error"` for details
3. For Ensure/Fatal context, broaden to `category="LogOutputDevice"`

## Component Types (common)

StaticMeshComponent, SkeletalMeshComponent, CapsuleComponent, SphereComponent,
BoxComponent, PointLightComponent, SpotLightComponent, AudioComponent,
CameraComponent, SpringArmComponent, ArrowComponent, WidgetComponent,
SceneComponent, ParticleSystemComponent, NiagaraComponent

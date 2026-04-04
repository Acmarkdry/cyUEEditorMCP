# Blueprint Core — Workflow Tips

## Typical Creation Flow (via ue_python_exec)

> **Note**: Blueprint create/compile/add_component are now handled through
> `ue_python_exec` using the Unreal Python API. See the `python-api` skill
> for full reference.

```python
# Create a Character blueprint with components
import unreal
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Create
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Character)
bp = asset_tools.create_asset("BP_Player", "/Game/Blueprints", None, factory)

# Add components via the SCS (Sub-object Component System)
# Note: Complex component manipulation may still need C++ actions
_result = bp.get_path_name() if bp else "failed"
```

## Key Patterns

- Use `blueprint.describe_full` for a single-call complete snapshot (replaces summary + N×describe)
- `blueprint.get_summary` is lighter if you only need variable/component/interface lists
- For creating/compiling/modifying blueprints, use `ue_python_exec` with `import unreal`
- Graph node operations (adding events, functions, wiring) still use the dedicated C++ actions

## Compile Diagnostics

1. Use `ue_python_exec` to compile: `unreal.EditorAssetLibrary.save_loaded_asset(bp)`
2. If errors, use `editor.get_logs` with `category="LogBlueprint"` + `min_verbosity="Error"` for details
3. For Ensure/Fatal context, broaden to `category="LogOutputDevice"`

## Component Types (common)

StaticMeshComponent, SkeletalMeshComponent, CapsuleComponent, SphereComponent,
BoxComponent, PointLightComponent, SpotLightComponent, AudioComponent,
CameraComponent, SpringArmComponent, ArrowComponent, WidgetComponent,
SceneComponent, ParticleSystemComponent, NiagaraComponent
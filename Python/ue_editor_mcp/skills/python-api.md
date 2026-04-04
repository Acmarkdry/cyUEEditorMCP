# Python API — ue_python_exec Workflows

> **ue_python_exec** replaces 45+ former C++ actions. Use `import unreal` to
> access the full Unreal Python API. Set `_result = <value>` to return data
> to the MCP client.

---

## 1. Conventions

| Rule | Detail |
|---|---|
| **Return data** | Assign to `_result` — it is serialized as JSON and returned to the caller. |
| **No `print()` for data** | `print()` output goes to `stdout` field; use `_result` for structured data. |
| **Idempotent operations** | Check existence before creating to avoid duplicates. |
| **Error handling** | Wrap in `try/except`; exceptions are captured in the `stderr` field. |

---

## 2. Actor Management (replaces editor.get_actors, spawn_actor, delete_actor, etc.)

### List all actors
```python
import unreal
_result = [a.get_name() for a in unreal.EditorLevelLibrary.get_all_level_actors()]
```

### Find actors by class
```python
import unreal
actors = unreal.GameplayStatics.get_all_actors_of_class(
    unreal.EditorLevelLibrary.get_editor_world(),
    unreal.StaticMeshActor
)
_result = [{"name": a.get_name(), "location": str(a.get_actor_location())} for a in actors]
```

### Spawn actor
```python
import unreal
loc = unreal.Vector(100.0, 200.0, 0.0)
rot = unreal.Rotator(0, 0, 0)
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
_result = actor.get_name()
```

### Delete actor
```python
import unreal
actors = unreal.GameplayStatics.get_all_actors_of_class(
    unreal.EditorLevelLibrary.get_editor_world(), unreal.StaticMeshActor)
for a in actors:
    if a.get_name() == "MyActorName":
        unreal.EditorLevelLibrary.destroy_actor(a)
        break
_result = "deleted"
```

### Set actor transform
```python
import unreal
actor = unreal.EditorLevelLibrary.get_all_level_actors()[0]
actor.set_actor_location(unreal.Vector(0, 0, 100), False, False)
actor.set_actor_rotation(unreal.Rotator(0, 45, 0), False)
actor.set_actor_scale3d(unreal.Vector(2, 2, 2))
```

### Get/Set actor properties
```python
import unreal
actor = unreal.EditorLevelLibrary.get_all_level_actors()[0]
# Get
_result = {
    "name": actor.get_name(),
    "class": actor.get_class().get_name(),
    "location": str(actor.get_actor_location()),
    "mobility": str(actor.root_component.mobility) if actor.root_component else "N/A"
}
```

---

## 3. Blueprint Operations (replaces blueprint.create, compile, set_property, etc.)

### Create blueprint
```python
import unreal
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
bp = asset_tools.create_asset("BP_MyActor", "/Game/Blueprints", None, factory)
_result = bp.get_path_name() if bp else "failed"
```

### Compile blueprint
```python
import unreal
bp = unreal.load_asset("/Game/Blueprints/BP_MyActor")
unreal.KismetSystemLibrary.flush_persistent_debug_lines(None)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
_result = "compiled"
```

### Set parent class
```python
import unreal
bp = unreal.load_asset("/Game/Blueprints/BP_MyActor")
# Parent class must be set via the factory at creation time
# or by modifying the CDO through C++ — Python API is limited here
```

### Spawn blueprint actor
```python
import unreal
bp = unreal.load_asset("/Game/Blueprints/BP_MyActor")
loc = unreal.Vector(0, 0, 0)
actor = unreal.EditorLevelLibrary.spawn_actor_from_object(bp, loc)
_result = actor.get_name() if actor else "failed"
```

---

## 4. Material Operations (replaces material.create, compile, apply_to_actor, etc.)

### Create material
```python
import unreal
factory = unreal.MaterialFactoryNew()
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = asset_tools.create_asset("M_MyMaterial", "/Game/Materials", None, factory)
_result = mat.get_path_name() if mat else "failed"
```

### Create material instance
```python
import unreal
factory = unreal.MaterialInstanceConstantFactoryNew()
parent = unreal.load_asset("/Game/Materials/M_MyMaterial")
factory.set_editor_property("initial_parent", parent)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mi = asset_tools.create_asset("MI_MyMaterial", "/Game/Materials", None, factory)
_result = mi.get_path_name() if mi else "failed"
```

### Apply material to actor
```python
import unreal
mat = unreal.load_asset("/Game/Materials/M_MyMaterial")
actors = unreal.GameplayStatics.get_all_actors_of_class(
    unreal.EditorLevelLibrary.get_editor_world(), unreal.StaticMeshActor)
for a in actors:
    if a.get_name() == "MyActor":
        comp = a.static_mesh_component
        comp.set_material(0, mat)
        break
_result = "applied"
```

---

## 5. Viewport Control (replaces editor.focus_viewport, get/set_viewport_transform)

### Get viewport camera
```python
import unreal
vp = unreal.UnrealEditorSubsystem()
loc, rot = vp.get_level_viewport_camera_info()
_result = {"location": str(loc), "rotation": str(rot)}
```

### Set viewport camera
```python
import unreal
vp = unreal.UnrealEditorSubsystem()
vp.set_level_viewport_camera_info(
    unreal.Vector(0, 0, 500),
    unreal.Rotator(-45, 0, 0)
)
```

---

## 6. PIE Control (replaces editor.start_pie, stop_pie, get_pie_state)

### Start PIE
```python
import unreal
unreal.AutomationLibrary.start_pie()
_result = "PIE started"
```

### Stop PIE
```python
import unreal
unreal.AutomationLibrary.end_play_map()
_result = "PIE stopped"
```

---

## 7. Asset Management (replaces editor.list_assets, save_all, rename_assets)

### List assets
```python
import unreal
ar = unreal.AssetRegistryHelpers.get_asset_registry()
assets = ar.get_assets_by_path("/Game", recursive=True)
_result = [str(a.package_name) for a in assets[:50]]
```

### Save all
```python
import unreal
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
_result = "saved"
```

### Rename/move asset
```python
import unreal
src = "/Game/OldPath/MyAsset"
dst = "/Game/NewPath/MyAsset"
unreal.EditorAssetLibrary.rename_asset(src, dst)
_result = "renamed"
```

---

## 8. Migration Reference Table

| Old C++ Action | Python Equivalent |
|---|---|
| `editor.get_actors` | `unreal.EditorLevelLibrary.get_all_level_actors()` |
| `editor.find_actors` | `unreal.GameplayStatics.get_all_actors_of_class()` |
| `editor.spawn_actor` | `unreal.EditorLevelLibrary.spawn_actor_from_class()` |
| `editor.delete_actor` | `unreal.EditorLevelLibrary.destroy_actor()` |
| `editor.set_actor_transform` | `actor.set_actor_location()` / `set_actor_rotation()` / `set_actor_scale3d()` |
| `editor.get_actor_properties` | Direct property access via `get_editor_property()` |
| `editor.set_actor_property` | `actor.set_editor_property()` |
| `editor.focus_viewport` | `UnrealEditorSubsystem.set_level_viewport_camera_info()` |
| `editor.get_viewport_transform` | `UnrealEditorSubsystem.get_level_viewport_camera_info()` |
| `editor.save_all` | `EditorLoadingAndSavingUtils.save_dirty_packages()` |
| `editor.list_assets` | `AssetRegistry.get_assets_by_path()` |
| `editor.rename_assets` | `EditorAssetLibrary.rename_asset()` |
| `editor.start_pie` | `AutomationLibrary.start_pie()` |
| `editor.stop_pie` | `AutomationLibrary.end_play_map()` |
| `blueprint.create` | `AssetTools.create_asset()` with `BlueprintFactory` |
| `blueprint.compile` | `KismetSystemLibrary` / `EditorAssetLibrary.save_loaded_asset()` |
| `material.create` | `AssetTools.create_asset()` with `MaterialFactoryNew` |
| `material.create_instance` | `AssetTools.create_asset()` with `MaterialInstanceConstantFactoryNew` |
| `material.apply_to_actor` | `StaticMeshComponent.set_material()` |
| `component.set_property` | `component.set_editor_property()` |

---

## 9. Tips

- **Batch operations**: Combine multiple operations in a single `ue_python_exec`
  call to reduce round-trips.
- **Long-running scripts**: Use `ue_async_run` with `action="submit"` for
  scripts that may take > 5 seconds (e.g. bulk asset processing).
- **Debugging**: Check `stdout` and `stderr` fields in the response for
  `print()` output and exceptions.
- **Complex node/graph operations**: These still require the dedicated C++
  actions (graph.*, node.*, layout.*) — Python cannot easily manipulate
  Blueprint graph nodes.

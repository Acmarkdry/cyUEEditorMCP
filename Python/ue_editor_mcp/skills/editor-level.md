# Editor & Level Management — Workflow Tips

## Actor Management (via ue_python_exec)

> **Note**: Actor spawning, transforms, properties, and outliner management
> are now handled through `ue_python_exec`. See the `python-api` skill.

```python
# Spawn a point light and set its properties
import unreal
loc = unreal.Vector(100.0, 0.0, 200.0)
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PointLight, loc)
actor.point_light_component.set_editor_property("intensity", 5000.0)
_result = actor.get_name()
```

## PIE (Play In Editor) Control (via ue_python_exec)

```python
# Start PIE
import unreal
unreal.AutomationLibrary.start_pie()
```

```python
# Stop PIE
import unreal
unreal.AutomationLibrary.end_play_map()
```

## Log-Based Testing

```
ue_batch(actions=[
  {action_id: "editor.clear_logs"},
  // Start PIE via ue_python_exec, wait, then stop
  {action_id: "editor.assert_log", params: {pattern: "Player spawned", category: "LogGame", should_exist: true}}
])
```

## Outliner Management (via ue_python_exec)

```python
# List all actors with folder info
import unreal
actors = unreal.EditorLevelLibrary.get_all_level_actors()
_result = [{"name": a.get_name(), "class": a.get_class().get_name(),
            "folder": a.get_folder_path().to_string()} for a in actors]
```

## Source Control Diff (retained C++ action)

```
ue_actions_run(action_id="editor.diff_against_depot", params={
  asset_path: "/Game/Blueprints/BP_Player"
})
# Returns: hasDifferences, summary, diffs[] with node-level changes
```

## Key Patterns

- Actor/viewport/PIE operations → use `ue_python_exec` with `import unreal`
- `editor.get_selected_asset_thumbnail` returns PNG base64 for Content Browser selection
- `editor.is_ready` to check if editor is fully initialized before operations
- `editor.get_logs` / `editor.clear_logs` / `editor.assert_log` for log inspection
- `editor.diff_against_depot` and `editor.get_asset_history` for source control
- For long-running operations, use `ue_async_run` with `action="submit"`
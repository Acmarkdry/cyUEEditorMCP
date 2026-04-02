# Editor & Level Management — Workflow Tips

## Actor Management

```
ue_batch(actions=[
  {action_id: "editor.spawn_actor", params: {name: "MyLight", type: "PointLight", location: [0, 0, 200]}},
  {action_id: "editor.set_actor_transform", params: {name: "MyLight", location: [100, 0, 200]}},
  {action_id: "editor.set_actor_property", params: {name: "MyLight", property_name: "Intensity", property_value: "5000"}}
])
```

## PIE (Play In Editor) Control

```
ue_actions_run(action_id="editor.start_pie", params={mode: "SelectedViewport"})
# ... wait for gameplay ...
ue_actions_run(action_id="editor.stop_pie")
ue_actions_run(action_id="editor.get_pie_state")  # → {is_playing, is_paused, ...}
```

## Log-Based Testing

```
ue_batch(actions=[
  {action_id: "editor.clear_logs"},
  {action_id: "editor.start_pie", params: {mode: "SelectedViewport"}},
  # ... wait ...
  {action_id: "editor.stop_pie"},
  {action_id: "editor.assert_log", params: {pattern: "Player spawned", category: "LogGame", should_exist: true}}
])
```

## Outliner Management

- `editor.rename_actor_label` — rename actor display name
- `editor.set_actor_folder` — organize actors into folders
- `editor.select_actors` — programmatic selection by name/class/folder
- `editor.get_outliner_tree` — full hierarchy with folders

## Source Control Diff

```
ue_actions_run(action_id="editor.diff_against_depot", params={
  asset_path: "/Game/Blueprints/BP_Player"
})
# Returns: hasDifferences, summary, diffs[] with node-level changes
```

## Key Patterns

- `editor.list_assets` with `class_filter` to find specific asset types
- `editor.rename_assets` supports batch rename with automatic redirector fixup
- `editor.get_selected_asset_thumbnail` returns PNG base64 for Content Browser selection
- `editor.is_ready` to check if editor is fully initialized before operations
- `batch.execute` is the C++ side batch — prefer `ue_batch` tool which wraps it
- Position arrays: location=[X,Y,Z], rotation=[Pitch,Yaw,Roll], scale=[X,Y,Z]

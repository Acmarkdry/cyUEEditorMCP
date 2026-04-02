# Auto Layout — Workflow Tips

## Blueprint Graph Layout

```
# Layout only selected nodes
ue_actions_run(action_id="layout.auto_selected", params={blueprint_name: "BP_Player"})

# Layout a subtree from a root node
ue_actions_run(action_id="layout.auto_subtree", params={blueprint_name: "BP_Player", root_node_id: "<GUID>"})

# Layout entire blueprint (all graphs)
ue_actions_run(action_id="layout.auto_blueprint", params={blueprint_name: "BP_Player"})
```

## Layout + Comment in One Call

```
ue_actions_run(action_id="layout.layout_and_comment", params={
  blueprint_name: "BP_Player",
  groups: [
    {node_ids: ["GUID1", "GUID2"], comment_text: "Init Logic", color: [0.15, 0.35, 0.65, 1]},
    {node_ids: ["GUID3", "GUID4"], comment_text: "Movement", color: [0.15, 0.55, 0.25, 1]}
  ]
})
```

## Spacing Parameters

- `layer_spacing` / `row_spacing`: `>0` = fixed pixels, `<=0` = auto (default)
- `horizontal_gap` / `vertical_gap`: fine-tune spacing
- `pin_align_pure`: align pure nodes to their connected pins
- `avoid_surrounding`: push nearby nodes away to prevent overlap
- `preserve_comments`: keep existing comment boxes intact

## Material Graph Layout

Use `material.auto_layout` (in materials skill) for material expression graphs.

## Key Patterns

- Run layout AFTER all node creation and wiring is complete
- `layout.auto_selected` respects current editor selection — use `graph.set_selected_nodes` first if needed
- Enhanced Sugiyama algorithm: longest-path layering, barycenter crossing optimization, width-aware spacing

# Graph Nodes & Wiring — Workflow Tips

## Node Creation → Connect → Compile

```
ue_batch(actions=[
  {action_id: "node.add_event", params: {blueprint_name: "BP_Player", event_name: "ReceiveBeginPlay"}},
  {action_id: "node.add_function_call", params: {blueprint_name: "BP_Player", target: "self", function_name: "PrintString"}},
  {action_id: "graph.connect_nodes", params: {blueprint_name: "BP_Player", source_node_id: "<GUID1>", source_pin: "then", target_node_id: "<GUID2>", target_pin: "execute"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "BP_Player"}}
])
```

## Key Patterns

- Node creation returns `node_id` (GUID) — capture it for subsequent connect calls
- Use `graph.describe` to inspect existing graph topology before modifications
- `graph.describe_enhanced` with `compact=true` reduces large graph output from 50-100KB to 10-20KB
- Pin names are case-sensitive: "then"/"execute" for exec, "ReturnValue" for outputs

## Patch System (Declarative Editing)

For complex graph modifications, prefer `graph.apply_patch` over individual node calls:
- Supports temp IDs: `"id": "my_node"` in add_node, reference as `"node": "my_node"` in connect
- Auto-compiles after execution
- Use `graph.validate_patch` for dry-run validation first

## Selection-Based Operations

1. `graph.get_selected_nodes` — read current editor selection
2. `graph.set_selected_nodes` — programmatically set selection
3. `graph.collapse_selection_to_function` / `graph.collapse_selection_to_macro` — refactor selected nodes
4. `graph.batch_select_and_act` — batch grouped selection + action (e.g., collapse per group)

## Cross-Graph Transfer

1. `graph.export_nodes` — serialize nodes to text
2. `graph.import_nodes` — paste into another graph (supports offset)

## Common Event Names

ReceiveBeginPlay, ReceiveTick, ReceiveEndPlay, ReceiveAnyDamage,
ReceiveActorBeginOverlap, ReceiveActorEndOverlap, ReceiveHit

## Flow Control Macros

ForEachLoop, ForLoop, WhileLoop, DoOnce, Gate, FlipFlop, Delay, Retriggerable Delay

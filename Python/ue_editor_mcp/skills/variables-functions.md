# Variable & Function Management — Workflow Tips

## Variable Lifecycle

```
ue_batch(actions=[
  {action_id: "variable.create", params: {blueprint_name: "BP_Player", variable_name: "Health", variable_type: "Float"}},
  {action_id: "variable.set_default", params: {blueprint_name: "BP_Player", variable_name: "Health", default_value: "100.0"}},
  {action_id: "variable.set_metadata", params: {blueprint_name: "BP_Player", variable_name: "Health", category: "Stats", instance_editable: true, tooltip: "Current health points"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "BP_Player"}}
])
```

## Variable Types

Boolean, Integer, Int64, Float, Double, String, Name, Text,
Vector, Rotator, Transform, LinearColor, Object (UObject references)

## Function Creation

```
ue_actions_run(action_id="function.create", params={
  blueprint_name: "BP_Player",
  function_name: "TakeDamage",
  inputs: [{name: "Amount", type: "Float"}],
  outputs: [{name: "IsDead", type: "Boolean"}],
  pure: false,
  category: "Combat"
})
```

## Key Patterns

- `variable.rename` auto-updates all getter/setter node references
- `function.rename` auto-updates all CallFunction references + FunctionEntry/FunctionResult nodes
- `macro.rename` auto-updates all macro instance references
- `function.call` adds a CallFunction node for a Blueprint-defined function (not engine functions)
- Use `variable.add_local` for function-scoped variables

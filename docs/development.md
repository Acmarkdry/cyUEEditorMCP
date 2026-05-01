# Development

UECliTool v0.5.0 is CLI-first at the model boundary while preserving the C++
editor bridge and action registry.

## Add A New Editor Action

1. Add a C++ `FEditorAction` subclass.
2. Register it in `MCPBridge.cpp`.
3. Add a Python `ActionDef` in `Python/ue_cli_tool/registry/actions.py`.
4. Verify discovery through `Python/ue.py query`.

Example C++ skeleton:

```cpp
class FMyNewAction : public FBlueprintNodeAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(
		const TSharedPtr<FJsonObject>& Params,
		FMCPEditorContext& Context) override;

protected:
	virtual bool Validate(
		const TSharedPtr<FJsonObject>& Params,
		FMCPEditorContext& Context,
		FString& OutError) override;

	virtual FString GetActionName() const override
	{
		return TEXT("my_new_action");
	}
};
```

Register:

```cpp
ActionHandlers.Add(TEXT("my_new_action"), MakeShared<FMyNewAction>());
```

Python registry entry:

```python
ActionDef(
	id="domain.my_new_action",
	command="my_new_action",
	tags=("domain", "keyword"),
	description="Describe what the action does.",
	input_schema={
		"type": "object",
		"properties": {
			"param1": {"type": "string", "description": "Input parameter."},
		},
		"required": ["param1"],
	},
	examples=({"param1": "value"},),
)
```

No CLI server changes should be needed. The parser derives positional arguments
from the registry schema.

## Verify A New Action

```powershell
.\Python\.venv\Scripts\python.exe .\Python\ue.py query "search my_new_action"
.\Python\.venv\Scripts\python.exe .\Python\ue.py query "help my_new_action"
.\Python\.venv\Scripts\python.exe .\Python\ue.py run "my_new_action value"
```

Use `--json` for exact test assertions:

```powershell
.\Python\.venv\Scripts\python.exe .\Python\ue.py run "my_new_action value" --json
```

## Response Formatting

Do not make C++ actions responsible for Codex-facing prose.

Return raw action fields from C++:

```json
{"success": true, "asset_path": "/Game/BP_Test", "node_count": 12}
```

Let Python handle public output:

- `connection.py` normalizes transport compatibility.
- `runtime.py` executes CLI/query requests.
- `formatter.py` converts internal envelopes to text, JSON, or raw output.
- `daemon.py` owns the persistent UE connection.

Add command-specific formatting in `formatter.py` only when the default summary
is too noisy or hides important identifiers.

## CLI-first Rules

- Do not add new MCP tools.
- Do not require model-authored JSON unless the command itself needs object data.
- Keep default output readable text.
- Keep `--json` stable enough for tests and scripts.
- Keep `--raw` for debugging only.
- Use `query "help <command>"` as the source of truth for syntax.

## Commandlet Mode

`UEEditorMCPCommandlet` remains available for CI and headless editor tasks:

```powershell
UnrealEditor-Cmd.exe YourProject.uproject `
  -run=UEEditorMCP `
  -command=exec_python `
  -params="{\"code\":\"import unreal; _result=unreal.SystemLibrary.get_engine_version()\"}" `
  -json
```

Batch commandlet input:

```json
[
  {"command": "exec_python", "params": {"code": "import unreal; _result = 'hello'"}},
  {"command": "graph.describe", "params": {"blueprint_name": "BP_Test"}}
]
```

Commandlet output with `-json` is wrapped in `JSON_BEGIN` / `JSON_END` markers.

## Tests

Run Python tests:

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
```

Current key test areas:

- CLI parser and shorthand syntax.
- Project config load/merge/save.
- Persistent connection and circuit breaker behavior.
- Runtime command/query handling.
- Daemon request handling.
- Text/JSON/raw formatter contracts.
- Registry/schema consistency.
- Skill metadata and workflow docs.

## Documentation

When changing runtime behavior, update:

- `README.md`
- `docs/architecture.md`
- `docs/installation.md`
- `docs/actions.md`
- `skills/unreal-ue-cli/SKILL.md`

Keep legacy MCP notes short and clearly marked as compatibility-only.

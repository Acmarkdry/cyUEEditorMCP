# Contributing

## Runtime Direction

UECliTool is CLI-first as of v0.5.0. New user-facing workflows should go
through:

```text
Python/ue.py -> ue_cli_tool.daemon -> PersistentUnrealConnection -> UE bridge
```

Keep MCP support as a legacy compatibility path only.

## Code Style

### Python

- Use UTF-8 without BOM.
- Keep `# coding: utf-8` in the first or second line of every `.py` file.
- Use tabs for indentation, following `.editorconfig`.
- Keep default CLI output human-readable text.
- Use `--json` only for stable machine-readable contracts.

### C++

- Follow Unreal Engine coding standards.
- Use tabs for indentation.
- Keep editor mutations transaction-aware through `FEditorAction`.

## Adding A New Action

See [docs/development.md](docs/development.md) for the full guide. Summary:

1. Create a new `FEditorAction` subclass.
2. Register it in `MCPBridge.cpp`.
3. Add an `ActionDef` entry in `Python/ue_cli_tool/registry/actions.py`.
4. Verify it through the CLI:

```powershell
.\Python\.venv\Scripts\python.exe .\Python\ue.py query "search my_new_action"
.\Python\.venv\Scripts\python.exe .\Python\ue.py query "help my_new_action"
.\Python\.venv\Scripts\python.exe .\Python\ue.py run "my_new_action value"
```

Do not add new MCP tools for new actions.

## Tests

Run the Python suite before committing:

```powershell
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
```

Run `git diff --check` to catch whitespace issues.

## Documentation

When behavior changes, update:

- `README.md`
- `docs/architecture.md`
- `docs/installation.md`
- `docs/actions.md`
- `docs/development.md`
- `skills/unreal-ue-cli/SKILL.md`

## Commit Messages

Use clear English commit messages:

```text
feat: add text formatter for material diagnostics
fix: preserve daemon_port during setup merge
docs: refresh cli-first installation guide
chore: remove obsolete kiro specs
```

# Contributing

## Code Style

### Python

- **Encoding:** UTF-8 (no BOM). Every `.py` file must include `# coding: utf-8` as the first or second line.
- **Indentation:** Tabs (not spaces). This is enforced by `.editorconfig`.
- **Line endings:** LF (`\n`). Configured via `.gitattributes`.

### C++

- Follow Unreal Engine coding standards.
- Use tabs for indentation (consistent with UE convention).

### Configuration Files

The project includes `.editorconfig` and `.gitattributes` to enforce these rules automatically. Most editors (VS Code, Visual Studio, etc.) respect `.editorconfig` out of the box.

## CI Checks

GitHub Actions runs the following checks on Python files:

1. **UTF-8 validity** — All `.py` files must be valid UTF-8
2. **No BOM** — UTF-8 BOM (`\xEF\xBB\xBF`) is not allowed
3. **Coding header** — `# coding: utf-8` must be present in the first two lines
4. **Syntax check** — `python -m py_compile` on all `.py` files

## Adding a New Action

See [docs/development.md](docs/development.md) for the full guide. Summary:

1. **C++ side:** Create a new `FEditorAction` subclass, register in `MCPBridge.cpp`
2. **Python side:** Add an `ActionDef` entry in `registry/actions.py`
3. Compile and verify — the new command auto-appears in `ue_query(query="help")`

## Commit Messages

Use clear, descriptive commit messages in English. Examples:

```
feat: add detail_level support to get_blueprint_summary
fix: resolve UTF-8 encoding corruption in Python files
docs: update installation guide with client-specific examples
chore: remove archived openspec documents
```
# Actions

Use the CLI to discover the live command catalog. Do not copy action syntax from
static documentation when the registry can answer directly.

```powershell
python .\Python\ue.py query help
python .\Python\ue.py query "help create_blueprint"
python .\Python\ue.py query "search material"
python .\Python\ue.py query skills
```

## CLI Syntax

```text
<command> [positional_args...] [--flag value ...]
@<target>     Set Blueprint/material/widget context
# comment     Ignored
```

Examples:

```powershell
python .\Python\ue.py run "get_context"
python .\Python\ue.py run "get_blueprint_summary BP_Player --detail_level normal"
```

Batch with context:

```powershell
@"
@BP_Player
add_blueprint_variable Health --variable_type Float
compile_blueprint
"@ | python .\Python\ue.py run
```

## Domains

The registry currently covers these broad domains:

| Domain | Purpose |
|--------|---------|
| `editor.*` | Context, logs, screenshots, PIE, source-control helpers |
| `asset.*` | Asset discovery, duplication, deletion, rename, redirectors |
| `blueprint.*` | Blueprint summaries and full snapshots |
| `graph.*` | Blueprint graph inspection and patching |
| `node.*` | Blueprint node creation and graph operations |
| `variable.*` | Blueprint variable management |
| `function.*` | Blueprint function management |
| `dispatcher.*` | Event dispatcher management |
| `layout.*` | Blueprint/material graph layout |
| `material.*` | Material graph inspection, diagnostics, layout |
| `widget.*` | UMG widget Blueprint creation, editing, analysis |
| `animgraph.*` | Animation Blueprint graph inspection and editing |
| `anim.*` | Animation asset analysis |
| `niagara.*` | Niagara helpers |
| `sequencer.*` | Sequencer helpers |
| `python.*` | Unreal embedded Python execution |

Run `query help` for the authoritative current list.

## Output

Default text output is intended for Codex and humans. It summarizes large
payloads and includes key fields.

Use JSON explicitly:

```powershell
python .\Python\ue.py query help --json
python .\Python\ue.py run "get_context" --json
```

Use raw mode only for debugging:

```powershell
python .\Python\ue.py run "get_context" --raw
```

## Python Execution

Use `python.exec` or its command alias only when the task genuinely needs custom
Unreal Python. Always set `_result` to return data:

```python
import unreal
actors = unreal.EditorLevelLibrary.get_all_level_actors()
_result = [{"name": a.get_name(), "class": a.get_class().get_name()} for a in actors]
```

Prefer dedicated commands for common Blueprint, graph, material, widget, and
animation workflows because they usually provide better validation and safer
transactions.

## Diagnostics

```powershell
python .\Python\ue.py doctor
python .\Python\ue.py query "logs --n 50 --source editor"
python .\Python\ue.py query metrics
python .\Python\ue.py query health
```

If a command fails, run:

```powershell
python .\Python\ue.py query "help <command>"
```

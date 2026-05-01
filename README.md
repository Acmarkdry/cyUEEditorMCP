# cyUECliTool

[English](README.md) | [中文](README.zh-CN.md)

CLI-first AI tool for controlling Unreal Engine Editor.

v0.5.0 moves the model-facing interface away from MCP tool JSON. Codex and
other agents should issue plain CLI text through the local `ue` command. A
Python daemon owns the persistent Unreal Editor connection, and default output
is concise text optimized for model reading.

MCP support remains as a legacy compatibility path during migration.

## Quick Start

```powershell
# 1. Compile the UE project so the editor plugin is available.

# 2. Install/setup the Python environment.
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\setup_mcp.ps1

# 3. Start Unreal Editor. The C++ bridge listens on tcp_port, default 55558.
D:\UnrealEngine5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -MCPPort=55558

# 4. Use the CLI-first runtime.
.\Python\.venv\Scripts\python.exe .\Python\ue.py daemon start
.\Python\.venv\Scripts\python.exe .\Python\ue.py query health
.\Python\.venv\Scripts\python.exe .\Python\ue.py run "get_context"
```

The daemon auto-starts by default for `run`, `query`, and `doctor` commands.

## Codex Skill

The plugin ships a reusable Codex skill at `skills/unreal-ue-cli`. Install it
into a Codex environment to make agents prefer the CLI-first runtime:

```powershell
$CodexSkills = "$env:USERPROFILE\.codex\skills"
New-Item -ItemType Directory -Force $CodexSkills | Out-Null
Copy-Item .\skills\unreal-ue-cli (Join-Path $CodexSkills "unreal-ue-cli") -Recurse -Force
```

After installation, agents can invoke `$unreal-ue-cli` or trigger it naturally
when working on Unreal Editor automation tasks.

## Architecture

```text
Codex / user
  -> ue.py run/query/doctor
  -> local Python daemon on 127.0.0.1:55559
  -> PersistentUnrealConnection
  -> Unreal Editor C++ bridge on 127.0.0.1:55558
  -> MCPBridge / FEditorAction handlers
```

The C++ bridge and action classes are preserved. The key change is the
model-facing boundary: agents write CLI text, not MCP JSON.

## CLI Usage

Run a single command:

```powershell
python .\Python\ue.py run "create_blueprint BP_Player --parent_class Character"
```

Run multiple commands with context:

```powershell
@"
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
compile_blueprint
"@ | python .\Python\ue.py run
```

Shortcut form:

```powershell
python .\Python\ue.py "get_context"
```

Query help and diagnostics:

```powershell
python .\Python\ue.py query help
python .\Python\ue.py query "help create_blueprint"
python .\Python\ue.py query "search material"
python .\Python\ue.py query "logs --n 50 --source editor"
python .\Python\ue.py doctor
```

## Output Modes

Default output is text:

```text
OK get_context
Asset path: /Game/Characters/BP_Player
Status: ok
```

Use JSON only when a script or test needs a stable machine-readable envelope:

```powershell
python .\Python\ue.py run "get_context" --json
```

Use raw mode for low-level debugging:

```powershell
python .\Python\ue.py run "get_context" --raw
```

## Daemon Commands

```powershell
python .\Python\ue.py daemon start
python .\Python\ue.py daemon status
python .\Python\ue.py daemon stop
python .\Python\ue.py daemon serve
```

The daemon owns:

- Persistent Unreal TCP connection.
- Heartbeat and reconnect behavior.
- Circuit breaker state.
- Metrics and operation context.
- Text/JSON/raw result envelopes for CLI callers.

## Project Configuration

`ue_mcp_config.yaml` is loaded from the project tree:

```yaml
engine_root: D:/UnrealEngine5/UnrealEngine
project_root: D:/UnrealGame/Lyra_56
tcp_port: 55558
daemon_port: 55559
auto_start_daemon: true
```

Use different `tcp_port` and `daemon_port` values when running multiple editor
instances.

## CLI Syntax

```text
<command> [positional_args...] [--flag value ...]
@<target>     Set context for blueprint/material/widget commands
# comment     Ignored
```

Positional arguments are mapped from the command schema. The `@target` context
fills the first matching context parameter such as `blueprint_name`,
`material_name`, or `widget_name`.

Array and object shorthand:

```text
--items a,b,c
--values 1,2,3
--props name=Sword,damage=50
```

JSON values still work when object data is truly needed.

## Legacy MCP Path

The legacy MCP server is still available:

```powershell
python -m ue_cli_tool.server
```

It exposes the old two-tool interface, `ue_cli` and `ue_query`, for clients that
have not migrated yet. New development should target the CLI-first path.

## Development

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
```

Key Python modules:

| Module | Purpose |
|--------|---------|
| `ue_cli_tool.cli` | Short-lived CLI entrypoint |
| `ue_cli_tool.daemon` | Long-lived local daemon |
| `ue_cli_tool.runtime` | MCP-free command/query runtime |
| `ue_cli_tool.formatter` | Text/json/raw output formatting |
| `ue_cli_tool.connection` | Persistent Unreal TCP connection |
| `ue_cli_tool.cli_parser` | CLI syntax parser |

## Documentation

| Document | Description |
|----------|-------------|
| [Installation](docs/installation.md) | CLI-first setup and troubleshooting |
| [Architecture](docs/architecture.md) | Technical details, C++ server, event system, protocols |
| [Actions](docs/actions.md) | Action domain reference |
| [Development](docs/development.md) | Adding new actions, tests, commandlet mode |
| [CLI-first Migration](docs/cli-first-migration.md) | Migration status and remaining compatibility notes |
| [GitHub Actions Runner](docs/github-actions-runner.md) | Self-hosted Windows runner setup |

## Credits

Maintained by Acmarkdry with Codex-assisted development.

Based on [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP)
(MIT License).

## License

MIT

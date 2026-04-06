# cyUECliTool

**CLI-native AI tool for controlling Unreal Engine Editor via MCP (Model Context Protocol).**

> 12 JSON tools → 2 CLI tools. 73% fewer tokens. Zero new syntax to learn.

## Quick Start

```bash
# 1. Compile UE project (plugin auto-builds with it)
# 2. Setup Python environment
cd Plugins/UECliTool
.\setup_mcp.ps1

# 3. Open Unreal Editor → plugin starts TCP:55558 automatically
#    AI client connects via MCP stdio → ready to go
```

See [docs/installation.md](docs/installation.md) for detailed setup instructions.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   AI (Claude, GPT, etc.)                     │
│                                                              │
│   ue_cli(command="@BP_Enemy\n                               │
│     add_component_to_blueprint StaticMeshComponent Mesh\n    │
│     compile_blueprint")                                      │
│                                                              │
│   ue_query(query="help create_blueprint")                    │
├──────────────────────────┬──────────────────────────────────┤
│       MCP Protocol       │   (stdio JSON — transport only)   │
├──────────────────────────┴──────────────────────────────────┤
│                                                              │
│    server.py  (2 tools: ue_cli + ue_query)                   │
│         │                                                    │
│    cli_parser.py  ← ActionRegistry (auto-derive params)     │
│         │                                                    │
│    connection.py  (CircuitBreaker + Metrics + Persistent)    │
│         │  TCP 55558                                         │
├─────────┼────────────────────────────────────────────────────┤
│    C++ MCPServer → Game Thread → MCPBridge → EditorActions   │
└─────────────────────────────────────────────────────────────┘
```

**Data flow:** AI sends CLI text → Python layer parses syntax, resolves `@target` context, maps positional args via ActionRegistry schema → sends structured JSON over TCP → C++ plugin executes on Game Thread → response flows back through the same path.

**Why a Python middle layer?** It handles CLI parsing, command batching, `@target` context management, circuit breaking, auto-reconnect, and metrics — keeping the C++ side focused on editor operations.

## CLI Syntax

```bash
<command> [positional_args...] [--flag value ...]
@<target>     # Set context (auto-fills blueprint_name/material_name/widget_name)
# comment     # Ignored
```

### Single Command
```
create_blueprint BP_Player --parent_class Character
```

### Multi-Command with Context
```
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
add_blueprint_event_node ReceiveBeginPlay
compile_blueprint
```

### Material Workflow
```
@M_Glow
create_material --path /Game/Materials
add_material_expression MaterialExpressionVectorParameter BaseColor
compile_material
```

### Positional Args
Mapped to `required` params in order from the command's schema.
Context target fills the first matching param (`blueprint_name`, etc.),
remaining positionals fill the rest.

## Token Optimization: detail_level

Commands like `get_blueprint_summary` support a `detail_level` parameter to control response verbosity:

| Level | Content | Typical Tokens |
|-------|---------|---------------|
| `brief` (default) | Stats + name lists only | ~150 |
| `normal` | Names + types + key metadata | ~500 |
| `full` | Complete details (all properties, pin info, etc.) | ~2000+ |

**Auto-detection:** If `blueprint_name` is omitted, the command automatically targets the currently selected/edited asset in the editor.

```bash
# Analyze whatever blueprint is currently open (brief by default)
get_blueprint_summary

# Full details for a specific blueprint
get_blueprint_summary BP_Enemy --detail_level full
```

## Query Examples

```
ue_query(query="help")                          # List all commands
ue_query(query="help create_blueprint")         # Command-specific help
ue_query(query="search material")               # Search commands
ue_query(query="context")                       # Session context
ue_query(query="logs --n 50 --source editor")   # Editor logs
ue_query(query="health")                        # Connection status
ue_query(query="skills")                        # Skill catalog
ue_query(query="resources conventions.md")       # Embedded docs
```

## MCP Client Configuration

Add to your MCP client config:
```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "<path-to-venv>/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": {
        "PYTHONPATH": "<path-to-plugin>/Python"
      }
    }
  }
}
```

Or if installed via `pip install -e .`:
```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "ue-cli-tool"
    }
  }
}
```

See [docs/installation.md](docs/installation.md) for client-specific examples.

## Development

```bash
# Run tests
cd Plugins/UECliTool
python -m pytest tests/ -v

# Run server directly
python -m ue_cli_tool.server
```

See [docs/development.md](docs/development.md) for adding new actions and commands.

## Documentation

| Document | Description |
|----------|-------------|
| [Installation](docs/installation.md) | Environment setup, Python config, MCP client setup |
| [Architecture](docs/architecture.md) | Technical details, C++ server, event system, protocols |
| [Actions](docs/actions.md) | Action domain reference (~120 commands) |
| [Development](docs/development.md) | Adding new actions, testing, commandlet mode |

## Credits

Based on [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) (MIT License).

## License

MIT
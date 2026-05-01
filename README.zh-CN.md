# cyUECliTool

[English](README.md) | [中文](README.zh-CN.md)

用于控制 Unreal Engine Editor 的 CLI-first AI 工具。

v0.5.0 将面向大模型的交互入口从 MCP tool JSON 改为本地 CLI 文本命令。Codex 和其他智能体应该通过本地 `ue` 命令发送普通 CLI 文本。Python daemon 负责持有到 Unreal Editor 的持久连接，默认输出是适合大模型阅读的简洁文本。

MCP 支持仍然保留，但只作为迁移期的 legacy compatibility path。

## 快速开始

```powershell
# 1. 编译 UE 项目，确保 editor plugin 可用。

# 2. 安装/初始化 Python 环境。
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\setup_mcp.ps1

# 3. 启动 Unreal Editor。C++ bridge 监听 tcp_port，默认 55558。
D:\UnrealEngine5\UnrealEngine\Engine\Binaries\Win64\UnrealEditor.exe `
  D:\UnrealGame\Lyra_56\Lyra_56.uproject -MCPPort=55558

# 4. 使用 CLI-first runtime。
.\Python\.venv\Scripts\python.exe .\Python\ue.py daemon start
.\Python\.venv\Scripts\python.exe .\Python\ue.py query health
.\Python\.venv\Scripts\python.exe .\Python\ue.py run "get_context"
```

`run`、`query` 和 `doctor` 命令默认会自动启动 daemon。

## Codex Skill

插件内置了可复用的 Codex skill：`skills/unreal-ue-cli`。把它安装到 Codex 环境后，智能体会优先使用 CLI-first runtime：

```powershell
$CodexSkills = "$env:USERPROFILE\.codex\skills"
New-Item -ItemType Directory -Force $CodexSkills | Out-Null
Copy-Item .\skills\unreal-ue-cli (Join-Path $CodexSkills "unreal-ue-cli") -Recurse -Force
```

安装后，Codex 可以显式调用 `$unreal-ue-cli`，也可以在处理 Unreal Editor 自动化任务时自然触发这个 skill。

## 架构

```text
Codex / user
  -> ue.py run/query/doctor
  -> local Python daemon on 127.0.0.1:55559
  -> PersistentUnrealConnection
  -> Unreal Editor C++ bridge on 127.0.0.1:55558
  -> MCPBridge / FEditorAction handlers
```

C++ bridge 和 action classes 继续保留。关键变化是面向模型的边界：智能体输出 CLI 文本，而不是 MCP JSON。

## CLI 用法

执行单条命令：

```powershell
python .\Python\ue.py run "create_blueprint BP_Player --parent_class Character"
```

使用上下文批量执行：

```powershell
@"
@BP_Player
add_component_to_blueprint CapsuleComponent Capsule
add_blueprint_variable Health --variable_type Float
compile_blueprint
"@ | python .\Python\ue.py run
```

快捷形式：

```powershell
python .\Python\ue.py "get_context"
```

查询帮助和诊断信息：

```powershell
python .\Python\ue.py query help
python .\Python\ue.py query "help create_blueprint"
python .\Python\ue.py query "search material"
python .\Python\ue.py query "logs --n 50 --source editor"
python .\Python\ue.py doctor
```

## 输出模式

默认输出是文本：

```text
OK get_context
Asset path: /Game/Characters/BP_Player
Status: ok
```

只有脚本或测试需要稳定机器可读 envelope 时才使用 JSON：

```powershell
python .\Python\ue.py run "get_context" --json
```

底层调试时可以使用 raw mode：

```powershell
python .\Python\ue.py run "get_context" --raw
```

## Daemon 命令

```powershell
python .\Python\ue.py daemon start
python .\Python\ue.py daemon status
python .\Python\ue.py daemon stop
python .\Python\ue.py daemon serve
```

daemon 负责：

- 持久 Unreal TCP connection。
- heartbeat 和 reconnect 行为。
- circuit breaker 状态。
- metrics 和 operation context。
- 面向 CLI 调用方的 text/JSON/raw result envelope。

## 项目配置

`ue_mcp_config.yaml` 会从项目目录加载：

```yaml
engine_root: D:/UnrealEngine5/UnrealEngine
project_root: D:/UnrealGame/Lyra_56
tcp_port: 55558
daemon_port: 55559
auto_start_daemon: true
```

同时运行多个 editor 实例时，请使用不同的 `tcp_port` 和 `daemon_port`。

## CLI 语法

```text
<command> [positional_args...] [--flag value ...]
@<target>     Set context for blueprint/material/widget commands
# comment     Ignored
```

位置参数会按照 command schema 映射。`@target` context 会填充第一个匹配的上下文参数，例如 `blueprint_name`、`material_name` 或 `widget_name`。

数组和对象简写：

```text
--items a,b,c
--values 1,2,3
--props name=Sword,damage=50
```

当确实需要对象数据时，JSON value 仍然可用。

## Legacy MCP 路径

legacy MCP server 仍然可用：

```powershell
python -m ue_cli_tool.server
```

它暴露旧的双工具接口：`ue_cli` 和 `ue_query`，供尚未迁移的客户端使用。新的开发应面向 CLI-first 路径。

## 开发

```powershell
cd D:\UnrealGame\Lyra_56\Plugins\UEEditorMCP
.\Python\.venv\Scripts\python.exe -m pytest Python\tests -q
```

关键 Python 模块：

| Module | 作用 |
|--------|------|
| `ue_cli_tool.cli` | 短生命周期 CLI entrypoint |
| `ue_cli_tool.daemon` | 长生命周期本地 daemon |
| `ue_cli_tool.runtime` | 不依赖 MCP 的 command/query runtime |
| `ue_cli_tool.formatter` | text/json/raw 输出格式化 |
| `ue_cli_tool.connection` | 持久 Unreal TCP connection |
| `ue_cli_tool.cli_parser` | CLI 语法解析器 |

## 文档

| 文档 | 说明 |
|------|------|
| [Installation](docs/installation.md) | CLI-first 安装和故障排查 |
| [Architecture](docs/architecture.md) | 技术细节、C++ server、event system、protocols |
| [Actions](docs/actions.md) | Action domain reference |
| [Development](docs/development.md) | 添加新 action、测试、commandlet mode |
| [CLI-first Migration](docs/cli-first-migration.md) | 迁移状态和剩余兼容性说明 |
| [GitHub Actions Runner](docs/github-actions-runner.md) | Self-hosted Windows runner 配置 |

## 致谢

由 Acmarkdry 维护，并由 Codex 辅助开发。

基于 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP)
（MIT License）。

## License

MIT

# 安装与配置

## 环境要求

- Unreal Engine 5.6+（内置 Python 3.11+，无需另行安装）
- Visual Studio 2022
- 任意兼容 MCP 的 AI 客户端（Claude Desktop、VS Code + Copilot、Cursor 等）

## 第 1 步：编译 C++ 插件

插件位于 `Plugins/UECliTool/`，随项目一起编译即可。用 Unreal Editor 打开 `.uproject` 或执行命令行编译：

```
Engine\Build\BatchFiles\Build.bat <ProjectName>Editor Win64 Development <Project>.uproject -waitmutex
```

编译成功后，打开 Unreal Editor 时插件自动在 `127.0.0.1:55558` 启动 TCP 服务器。

## 第 2 步：Python 环境配置

### 自动配置（推荐）

```powershell
cd Plugins/UECliTool
.\setup_mcp.ps1
```

脚本自动完成：
1. 检测 UE 内置 Python（`Engine/Binaries/ThirdParty/Python3/Win64/python.exe`）
2. 在 `Python/.venv` 创建虚拟环境并安装 `mcp` 包
3. 在项目根目录生成 `.vscode/mcp.json`

### 手动配置

```bash
cd Plugins/UECliTool/Python
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

## 第 3 步：MCP 客户端配置

MCP 客户端通过 stdio 启动 Python 服务，Python 服务通过 TCP 连接 UE 插件。

### 通用配置模板

核心配置格式（适用于所有 MCP 客户端）：

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "<venv-python-path>",
      "args": ["-m", "ue_cli_tool.server"],
      "env": {
        "PYTHONPATH": "<plugin-path>/Python"
      }
    }
  }
}
```

将 `<venv-python-path>` 替换为虚拟环境中的 Python 可执行文件路径，`<plugin-path>` 替换为插件目录的实际路径。

### VS Code（.vscode/mcp.json）

`setup_mcp.ps1` 会自动生成此文件：

```jsonc
{
  "servers": {
    "ue-cli-tool": {
      "command": "./Plugins/UECliTool/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": { "PYTHONPATH": "./Plugins/UECliTool/Python" }
    }
  }
}
```

### Claude Desktop

编辑 `claude_desktop_config.json`（通常位于 `%APPDATA%/Claude/`）：

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "D:/YourProject/Plugins/UECliTool/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": {
        "PYTHONPATH": "D:/YourProject/Plugins/UECliTool/Python"
      }
    }
  }
}
```

> **注意：** Claude Desktop 需要使用**绝对路径**。

### Cursor

编辑 `.cursor/mcp.json`（项目根目录）：

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "./Plugins/UECliTool/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_cli_tool.server"],
      "env": {
        "PYTHONPATH": "./Plugins/UECliTool/Python"
      }
    }
  }
}
```

### pip install 方式

如果通过 `pip install -e .` 安装了包，可以简化配置：

```json
{
  "mcpServers": {
    "ue-cli-tool": {
      "command": "ue-cli-tool"
    }
  }
}
```

## 第 4 步：启动

1. 打开 Unreal Editor（插件自动启动 TCP 服务器，端口 55558）
2. 打开 AI 客户端 — MCP 服务通过 stdio 启动并自动连接
3. 使用 `ue_cli` / `ue_query` 两个工具控制编辑器

### 验证连接

```
ue_query(query="health")
```

如果返回 `connected` 状态，说明一切就绪。

## 常见问题

### TCP 连接失败

- 确认 Unreal Editor 已打开且插件加载成功（查看 Output Log 中的 `MCP Server listening on port 55558`）
- 检查防火墙是否阻止了本地 TCP 连接
- 端口 55558 是否被占用（`netstat -an | findstr 55558`）

### Python 环境问题

- 确认虚拟环境存在：`Plugins/UECliTool/Python/.venv/`
- 确认 `mcp` 包已安装：`.venv/Scripts/pip list | findstr mcp`
- 如果 `setup_mcp.ps1` 报错，检查 UE 安装路径是否正确

### MCP 工具不可见

- 确认客户端配置文件路径正确
- 重启 AI 客户端以重新加载 MCP 配置
- 使用 `ue_query(query="health")` 测试连接
# 安装与配置

## 环境要求

- Unreal Engine 5.7+（内置 Python 3.11，无需另行安装）
- Visual Studio 2022
- VS Code + GitHub Copilot（或任意兼容 MCP 的客户端）

## 第 1 步：编译 C++ 插件

插件位于 `Plugins/UEEditorMCP/`，编译编辑器目标：

```
Engine\Build\BatchFiles\Build.bat <ProjectName>Editor Win64 Development <Project>.uproject -waitmutex
```

## 第 2 步：Python 环境配置

**PowerShell（推荐，使用 UE 内置 Python）：**
```powershell
cd Plugins/UEEditorMCP
.\setup_mcp.ps1
```

脚本自动完成：
1. 检测 UE 内置 Python（`Engine/Binaries/ThirdParty/Python3/Win64/python.exe`）
2. 在 `Python/.venv` 创建虚拟环境并安装 `mcp` 包
3. 在项目根目录生成 `.vscode/mcp.json`

<details>
<summary>手动配置</summary>

```bash
cd Plugins/UEEditorMCP/Python
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
```

`.vscode/mcp.json`：
```jsonc
{
  "servers": {
    "ue-editor-mcp": {
      "command": "./Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_editor_mcp.server_unified"],
      "env": { "PYTHONPATH": "./Plugins/UEEditorMCP/Python" }
    },
    "ue-editor-mcp-logs": {
      "command": "./Plugins/UEEditorMCP/Python/.venv/Scripts/python.exe",
      "args": ["-m", "ue_editor_mcp.server_unreal_logs"],
      "env": { "PYTHONPATH": "./Plugins/UEEditorMCP/Python" }
    }
  }
}
```
</details>

## 第 3 步：启动

1. 在 Unreal Editor 中打开项目（插件自动在端口 55558 启动 TCP 服务器）
2. 打开 VS Code — `ue-editor-mcp` 通过 stdio 启动并连接
3. 使用 GitHub Copilot Chat 或任意兼容 MCP 的客户端发出命令

---

## 日志工具（`ue-editor-mcp-logs`）

可选的独立日志服务器，暴露三个工具：

### `unreal.logs.get`

| 参数 | 说明 |
|------|------|
| `mode` | `auto\|live\|saved`（默认 `auto`） |
| `tailLines` | 默认 200（范围 20..2000） |
| `cursor` | 增量游标，避免重复读取 |
| `workspaceRoot` | UE 不可达时离线读取必填 |

推荐用法：首次调用保存 `cursor`，后续传入 `cursor` 只获取增量日志。

### `unreal.asset_thumbnail.get`

- 支持 `assetPath` / `assetPaths` / `assetIds` 批量输入
- 返回 `thumbnails[]`，每项含 PNG base64
- 需要 UE 编辑器连接

### `unreal.asset_diff.get`

- `assetPath`（必填）：完整资产路径
- 返回结构化差异：节点级别（蓝图）或属性级别（通用资产）
- 需要 UE 编辑器 + 源码控制（SVN/Perforce/Git）连接

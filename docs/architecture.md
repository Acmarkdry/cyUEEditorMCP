# 架构与技术细节

## 架构图

```
VS Code / MCP 客户端（GitHub Copilot 等）
        │
        │  10 个 MCP 工具（stdio）
        ▼
  server_unified.py（动作注册表 + 分发器）
        │
        ├── TCP/JSON（端口 55558，持久连接，长度前缀帧）
        │         │
        │         ▼
        │   C++ 插件（FMCPServer → FMCPClientHandler）
        │         │
        │         ├── 快速路径（ping/close/async_execute/get_task_result）
        │         │     └── 直接在 TCP 线程处理
        │         │
        │         ├── 游戏线程分发
        │         │     ├── FExecPythonAction → IPythonScriptPlugin → Unreal Python API
        │         │     └── ~95 个 FEditorAction 子类 → 校验 → 执行 → 自动保存
        │         │
        │         └── 异步路径
        │               └── SubmitAsyncTask → AsyncTask(GameThread) → 结果回写
        │
        └── Commandlet 模式（CLI/CI）
              └── UEEditorMCPCommandlet → MCPBridge → ActionHandlers
```

## C++ 服务器（`FMCPServer`）

- 监听 `127.0.0.1:55558`（仅限本地）
- 每个连接派生独立的 `FMCPClientHandler` 线程，最多 8 路并发
- **快速路径**：`ping`/`close`/`async_execute`/`get_task_result` 直接在客户端线程处理
- 其余命令通过 `AsyncTask + FEvent` 分发到游戏线程
- 客户端超时：120 秒无活动后断开
- 启用 `SO_REUSEADDR`，避免编辑器重启时端口冲突

## Python 执行引擎（`FExecPythonAction`）

- 通过 `IPythonScriptPlugin::Get()->ExecPythonCommand()` 执行代码
- Wrapper 脚本捕获 stdout/stderr 并重定向
- `_result` 变量约定：脚本设置 `_result = <value>`，C++ 通过临时 JSON 文件读取
- 替代 45+ 原有 C++ 动作（Actor、蓝图、材质、视口、PIE 等）

## 异步执行基础设施

- `SubmitAsyncTask(command, params)` → 返回 UUID task_id
- `AsyncTask(ENamedThreads::GameThread)` 在游戏线程执行
- `GetTaskResult(task_id)` → pending/completed+result
- 过期清理：300 秒超时自动删除
- 线程安全：`FCriticalSection AsyncTasksLock` 保护 `AsyncTasks` 映射

## Commandlet 模式

```bash
# 单条命令
UnrealEditor-Cmd.exe Project.uproject -run=UEEditorMCP -command=exec_python -params="{...}" -json

# 批量执行
UnrealEditor-Cmd.exe Project.uproject -run=UEEditorMCP -batch -file=commands.json -json

# 帮助/Schema 导出
UnrealEditor-Cmd.exe Project.uproject -run=UEEditorMCP -help
UnrealEditor-Cmd.exe Project.uproject -run=UEEditorMCP -format=json
```

## Python 服务器（`server_unified.py`）

- 10 个固定工具的单一 MCP 服务器
- 新增工具：`ue_python_exec`（Python 代码执行）、`ue_async_run`（异步 submit/poll）
- 动作注册表含 ~95+ 个 ActionDef，支持关键字搜索和模式自省
- `ue_batch` 批量执行（每次最多 50 个动作，单次 TCP 往返）
- 命令日志环形缓冲区，供 `ue_logs_tail` 使用

## 通信协议

```
[4 字节：消息长度（大端序）] [UTF-8 JSON 载荷]
```

请求：
```json
{"type": "exec_python", "params": {"code": "import unreal; _result = unreal.SystemLibrary.get_engine_version()"}}
```

响应：
```json
{"success": true, "return_value": "5.6.0", "stdout": "", "stderr": ""}
```

## 编辑器专属安全保障

| 层级 | 机制 | 效果 |
|------|------|------|
| `.uplugin` | `"Type": "Editor"` | UBT 对所有非编辑器目标跳过此模块 |
| `.Build.cs` | 依赖 `UnrealEd`、`BlueprintGraph`、`Kismet`、`UMGEditor`、`PythonScriptPlugin` | 无法链接到游戏目标 |
| `.uplugin` | `"PlatformAllowList": ["Win64", "Mac", "Linux"]` | 仅限桌面编辑器平台 |

## 关键文件

| 文件 | 用途 |
|------|------|
| `Python/ue_editor_mcp/server_unified.py` | 单一 MCP 服务器，10 个工具，动作分发 |
| `Python/ue_editor_mcp/registry/__init__.py` | ActionRegistry 类，关键字搜索引擎 |
| `Python/ue_editor_mcp/registry/actions.py` | ~95+ ActionDef 条目 + python.exec |
| `Python/ue_editor_mcp/skills/python-api.md` | Python API 工作流、迁移对照表 |
| `Python/ue_editor_mcp/connection.py` | `PersistentUnrealConnection`（TCP、心跳、自动重连） |
| `Source/Private/MCPServer.cpp` | TCP Accept + 快速路径 + 游戏线程分发 |
| `Source/Private/MCPBridge.cpp` | 动作处理器注册表 + 异步任务管理 |
| `Source/Private/Actions/PythonActions.cpp` | FExecPythonAction（Python 执行引擎） |
| `Source/Private/UEEditorMCPCommandlet.cpp` | Commandlet CLI 模式 |
| `Source/Private/Actions/AnimGraphActions.cpp` | AnimGraph 全部 18 个 Action |
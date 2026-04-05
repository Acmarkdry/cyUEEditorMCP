# 架构与技术细节

## 架构图

```
VS Code / MCP 客户端（GitHub Copilot 等）
        │
        │  12 个 MCP 工具（stdio）
        ▼
  server_unified.py（动作注册表 + 分发器）
        │
        ├── ContextStore（.context/ 持久化）
        │     ├── session.json    — 会话元数据 + UE 连接状态
        │     ├── history.jsonl   — 操作历史（摘要级）
        │     └── workset.json    — 当前工作集（资产路径）
        │
        ├── PermissionPolicy（auto/confirm_destructive/readonly）
        │
        ├── TCP/JSON（端口 55558，持久连接，长度前缀帧）
        │         │
        │         ▼
        │   C++ 插件（FMCPServer → FMCPClientHandler）
        │         │
        │         ├── 快速路径（无需游戏线程）
        │         │     ├── ping/close
        │         │     ├── async_execute/get_task_result
        │         │     └── subscribe_events/poll_events/unsubscribe_events ← P9 NEW
        │         │
        │         ├── 游戏线程分发
        │         │     ├── FExecPythonAction → IPythonScriptPlugin → Unreal Python API
        │         │     ├── ~123 个 FEditorAction 子类 → 校验 → FScopedTransaction → 执行 → 自动保存
        │         │     └── FBatchExecuteAction → 原子事务回滚（transactional 模式）
        │         │
        │         ├── 异步路径
        │         │     └── SubmitAsyncTask → AsyncTask(GameThread) → 结果回写
        │         │
        │         └── 事件推送（P9 NEW）
        │               └── FMCPEventHub → 编辑器委托 → 每客户端事件队列 → poll 读取
        │
        └── Commandlet 模式（CLI/CI）
              └── UEEditorMCPCommandlet → MCPBridge → ActionHandlers
```

## C++ 服务器（`FMCPServer`）

- 监听 `127.0.0.1:55558`（仅限本地）
- 每个连接派生独立的 `FMCPClientHandler` 线程，最多 8 路并发
- **快速路径**：`ping`/`close`/`async_execute`/`get_task_result`/`subscribe_events`/`poll_events`/`unsubscribe_events` 直接在客户端线程处理
- 其余命令通过 `AsyncTask + FEvent` 分发到游戏线程
- 客户端超时：300 秒无活动后断开
- 启用 `SO_REUSEADDR`，避免编辑器重启时端口冲突

## 事件推送系统（`FMCPEventHub` — P9 新增）

- **架构**：编辑器委托 → `FMCPEventHub::EnqueueEvent` → 每客户端队列 → TCP poll 读取
- **支持事件类型**：
  - `blueprint_compiled` — 蓝图编译完成（含状态/错误）
  - `asset_saved` / `asset_deleted` / `asset_renamed` — 资产增删改
  - `pie_started` / `pie_ended` — PIE 启停
  - `level_changed` — 地图切换/Sublevel 变化
  - `selection_changed` — 编辑器选择变化
  - `undo_performed` — Undo 操作
- **线程安全**：`FCriticalSection` 保护客户端队列，每客户端最多 500 个待处理事件
- **协议命令**：
  - `subscribe_events` — 订阅（可选 `event_types` 过滤，空 = 全部）
  - `poll_events` — 获取待处理事件并清空（`max_events` 参数）
  - `unsubscribe_events` — 取消订阅
- **绑定的 UE 委托**：`OnBlueprintCompiled`、`OnAssetRemoved`、`OnAssetRenamed`、`MapChange`、`BeginPIE`、`EndPIE`、`OnUndo`

## 批量原子回滚（P9 新增）

- `ue_batch` / `batch_execute` 新增 `transactional` 参数（默认 false）
- 当 `transactional=true` 时，所有子命令包裹在单个 `FScopedTransaction` 中
- 任何子命令失败（且 `stop_on_error=true`）时，自动调用 `GEditor->UndoTransaction()` 回滚全部更改
- 返回结果含 `transactional` 和 `rolled_back` 布尔标记

## 权限分级（P9 新增）

- `PermissionPolicy` 三级策略：`auto`（默认全允许）、`confirm_destructive`（破坏性操作需确认）、`readonly`（阻止写操作）
- 每个 `ActionDef` 携带 `capabilities` 和 `risk` 元数据
- 在 `_execute_action` 中执行前检查，被拒绝时返回结构化错误

## Undo/Redo 事务（P7 新增）

- `FEditorAction::Execute()` 自动检测 `IsWriteAction()`，为写操作包裹 `FScopedTransaction`
- 事务描述：`"MCP: <ActionName>"`
- 三个新 Action：`undo`、`redo`、`get_undo_history`
- `IsWriteAction()` 默认复用 `RequiresSave()`，零改动覆盖所有现有 Action

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

## Context Layer（`context.py`）

- `ContextStore` 类嵌入 `server_unified.py` 进程，与 MCP 服务器同生命周期
- 会话管理：启动时检测上次 session 状态（正常退出/异常终止），创建新 session UUID
- 操作历史：每次工具调用自动追加摘要到 `history.jsonl`（JSONL 格式，上限 500 条）
- 工作集追踪：自动从参数中提取资产路径（`blueprint_name`、`material_name` 等），维护 `workset.json`
- UE 连接监控：通过 `PersistentUnrealConnection.on_state_change` 回调感知连接/崩溃/重连
- 崩溃时立即持久化当前状态到 `session.json` 的 `crash_context` 字段
- 文件持久化：原子写入（temp + rename）防损坏，读取时 try/except 容错
- `ue_context` 工具：resume（恢复上下文）、status、history、workset、clear 五种动作

## Python 服务器（`server_unified.py`）

- 12 个固定工具的单一 MCP 服务器
- 新增工具：`ue_events`（事件推送订阅/轮询）、`ue_python_exec`（Python 代码执行）、`ue_async_run`（异步 submit/poll）、`ue_context`（上下文管理）
- 动作注册表含 ~123 个 ActionDef，支持关键字搜索和模式自省
- `ue_batch` 批量执行（每次最多 50 个动作，单次 TCP 往返，可选原子回滚）
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

事件推送（subscribe → poll 模式）：
```json
// 订阅
{"type": "subscribe_events", "params": {"event_types": ["blueprint_compiled", "pie_started"]}}

// 轮询
{"type": "poll_events", "params": {"max_events": 50}}
// 响应：{"success": true, "result": {"count": 2, "events": [{...}, {...}]}}
```

## 编辑器专属安全保障

| 层级 | 机制 | 效果 |
|------|------|------|
| `.uplugin` | `"Type": "Editor"` | UBT 对所有非编辑器目标跳过此模块 |
| `.Build.cs` | 依赖 `UnrealEd`、`BlueprintGraph`、`Kismet`、`UMGEditor`、`PythonScriptPlugin` 等 | 无法链接到游戏目标 |
| `.uplugin` | `"PlatformAllowList": ["Win64", "Mac", "Linux"]` | 仅限桌面编辑器平台 |

## 关键文件

| 文件 | 用途 |
|------|------|
| `Python/ue_editor_mcp/server_unified.py` | 单一 MCP 服务器，12 个工具，动作分发 |
| `Python/ue_editor_mcp/registry/__init__.py` | ActionRegistry 类，关键字搜索引擎 |
| `Python/ue_editor_mcp/registry/actions.py` | ~123 ActionDef 条目 + python.exec |
| `Python/ue_editor_mcp/permissions.py` | PermissionPolicy（auto/confirm_destructive/readonly） |
| `Python/ue_editor_mcp/context.py` | ContextStore（会话、历史、工作集、UE 连接监控） |
| `Python/ue_editor_mcp/connection.py` | PersistentUnrealConnection（TCP、心跳、自动重连、状态回调） |
| `Source/Private/MCPServer.cpp` | TCP Accept + 快速路径 + 事件协议 + 游戏线程分发 |
| `Source/Private/MCPBridge.cpp` | 动作处理器注册表 + 异步任务管理 + EventHub 生命周期 |
| `Source/Private/MCPEventHub.cpp` | 事件推送系统（编辑器委托 → 客户端队列） |
| `Source/Private/Actions/PythonActions.cpp` | FExecPythonAction（Python 执行引擎） |
| `Source/Private/Actions/EditorActions.cpp` | batch_execute（含原子回滚）、Undo/Redo、截图 |
| `Source/Private/Actions/NiagaraActions.cpp` | Niagara 粒子系统 7 个 Action |
| `Source/Private/Actions/DataTableActions.cpp` | DataTable 5 个 Action |
| `Source/Private/Actions/SequencerActions.cpp` | Sequencer 5 个 Action |
| `Source/Private/Actions/ExtendedActions.cpp` | P10: 测试(2) + 关卡(2) + 性能(2) |
| `Source/Private/Actions/AnimGraphActions.cpp` | AnimGraph 全部 18 个 Action |
| `Source/Private/UEEditorMCPCommandlet.cpp` | Commandlet CLI 模式 |
| `tests/test_runtime_e2e.py` | 运行时 E2E 功能测试（~50 测试用例） |
| `tests/test_schema_contract.py` | Python tool ↔ C++ Action schema 一致性验证 |
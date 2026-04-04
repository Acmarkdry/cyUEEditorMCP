## ADDED Requirements

### Requirement: Async command submission
系统 SHALL 支持通过 `async_execute` 命令异步提交任意已注册命令。客户端发送 `async_execute` 请求后，系统 MUST 立即返回一个唯一的 `task_id`（UUID v4 格式），不阻塞等待游戏线程执行完成。

#### Scenario: Submit a long-running command asynchronously
- **WHEN** 客户端发送 `{"type": "async_execute", "params": {"command": "compile_blueprint", "params": {"blueprint_name": "BP_Heavy"}}}`
- **THEN** 系统立即返回 `{"success": true, "result": {"task_id": "<uuid>", "status": "submitted"}}`，不等待编译完成

#### Scenario: Submit with missing inner command
- **WHEN** 客户端发送 `{"type": "async_execute", "params": {}}` 缺少 `command` 字段
- **THEN** 系统返回 `{"success": false, "error": "Missing 'command' field in async_execute params"}`

### Requirement: Async result polling
系统 SHALL 支持通过 `get_task_result` 命令查询异步任务的执行结果。系统 MUST 返回三种状态之一：`pending`（仍在执行）、`success`（执行成功）、`error`（执行失败）。

#### Scenario: Poll a completed task
- **WHEN** 客户端发送 `{"type": "get_task_result", "params": {"task_id": "<uuid>"}}` 且该任务已在游戏线程完成
- **THEN** 系统返回该命令的完整执行结果，并从结果缓存中移除该 task_id

#### Scenario: Poll a pending task
- **WHEN** 客户端发送 `get_task_result` 且该任务仍在游戏线程中执行
- **THEN** 系统返回 `{"success": true, "result": {"task_id": "<uuid>", "status": "pending"}}`

#### Scenario: Poll an unknown task_id
- **WHEN** 客户端发送 `get_task_result` 且 `task_id` 不存在（未知或已过期）
- **THEN** 系统返回 `{"success": false, "error": "Unknown or expired task_id: <uuid>"}`

### Requirement: Async task TTL cleanup
系统 SHALL 自动清理超过 5 分钟未被获取的异步任务结果，防止内存泄漏。

#### Scenario: Task result expires after TTL
- **WHEN** 一个异步任务在 5 分钟内未被 `get_task_result` 获取
- **THEN** 系统自动从缓存中移除该任务结果

### Requirement: Async task crash protection
异步提交的命令 MUST 复用现有的 SEH + C++ 异常双重保护机制。即使命令在游戏线程崩溃，系统 SHALL 将结果记录为 error 状态而非崩溃编辑器。

#### Scenario: Async command crashes on game thread
- **WHEN** 异步提交的命令在游戏线程执行时触发 SEH 异常
- **THEN** 系统捕获异常，将该 task_id 的结果设为 `{"success": false, "error": "crash prevented", "error_type": "seh_exception"}`

### Requirement: Async commands bypass game thread blocking
`async_execute` 和 `get_task_result` 命令 MUST 在 TCP 客户端线程直接处理（与 `ping`/`close` 同层），不经过 `AsyncTask(GameThread) + FEvent::Wait` 的同步路径。

#### Scenario: async_execute does not block client thread
- **WHEN** 游戏线程正在执行一个 30 秒的同步命令
- **THEN** 另一个客户端连接仍可立即发送 `async_execute` 并获得 `task_id` 响应

### Requirement: Python MCP async tool
Python MCP Server SHALL 暴露第 9 个工具 `ue_async_run`，支持两种操作：`submit`（提交异步任务）和 `poll`（查询任务结果）。

#### Scenario: AI submits async action via MCP
- **WHEN** AI 调用 `ue_async_run` 工具，参数 `{"action": "submit", "action_id": "blueprint.compile", "params": {"blueprint_name": "BP_Test"}}`
- **THEN** 工具返回 `{"success": true, "task_id": "<uuid>", "status": "submitted"}`

#### Scenario: AI polls async result via MCP
- **WHEN** AI 调用 `ue_async_run` 工具，参数 `{"action": "poll", "task_id": "<uuid>"}`
- **THEN** 工具返回任务的当前状态和结果

### Requirement: Thread-safe async task storage
异步任务结果 MUST 使用线程安全的存储（FCriticalSection 保护的 TMap），支持多客户端并发提交和查询。

#### Scenario: Concurrent async submissions from multiple clients
- **WHEN** 两个客户端同时提交 `async_execute` 请求
- **THEN** 两个任务均成功创建，各自返回不同的 task_id，无数据竞争

# Tasks: Python Middleware 全面重构

## Phase 1: 统一连接层 + Circuit Breaker

### Task 1.1: connection.py — 增加 Circuit Breaker 和分级超时
- [x] 新增 `CircuitState` 枚举（CLOSED / OPEN / HALF_OPEN）
- [x] 新增 `CircuitBreakerConfig` 数据类
- [x] 新增 `CircuitBreaker` 类：record_success / record_failure / allow_request / state
- [x] 新增 `TimeoutTier` 枚举及 `_TIMEOUT_MAP` 命令映射表
- [x] 将 `send_command` 内部拆分为 `_send_impl`（实际收发）+ 外层 Circuit Breaker 检查
- [x] `send_command` 签名增加 `*, timeout_tier=None` keyword-only 参数
- [x] 在发送前按 `_TIMEOUT_MAP` 或 `timeout_tier` 临时调整 socket timeout
- [x] 新增 `send_raw_dict(command_type, params) -> dict` 便捷方法
- [x] 确保心跳 ping 不触发 Circuit Breaker（心跳失败走现有逻辑）
- [ ] 所有现有测试通过

### Task 1.2: connection.py — Metrics 集成点预留
- [x] 新增 `on_request_complete` 可选回调 `(command, duration_ms, success, error) -> None`
- [x] 在 `send_command` 结束时调用回调（如已注册）
- [x] 不在此阶段创建 MetricsCollector，仅留 hook

## Phase 2: 可观测性层

### Task 2.1: 新建 metrics.py — MetricsCollector
- [x] 新建 `Python/ue_editor_mcp/metrics.py`
- [x] 实现 `RequestMetrics` 数据类（command, start_time, end_time, success, error, request_id）
- [x] 实现 `MetricsCollector`：deque history + counters + error_counters
- [x] `record(metrics)` 线程安全记录
- [x] `get_summary(last_n=100)` 返回 total/success_rate/avg_ms/p50/p95/max/by_command/errors_by_command
- [x] `get_recent(last_n=20)` 返回最近 N 条请求详情
- [x] `reset()` 清空所有计数
- [x] 全局单例 `get_metrics()` / `reset_metrics()`
- [ ] 单元测试

### Task 2.2: 新建 tracer.py — RequestTracer
- [x] 新建 `Python/ue_editor_mcp/tracer.py`
- [x] 实现 `RequestTracer` 类，持有 MetricsCollector 引用
- [x] `trace(command)` 上下文管理器：自动计时 + 记录到 metrics
- [x] 生成 request_id（command + 8位随机hex）
- [ ] 单元测试

### Task 2.3: 连接 Metrics 到 connection.py
- [x] 在 `PersistentUnrealConnection.__init__` 中通过 on_request_complete 回调集成 MetricsCollector
- [x] 全局 `get_connection()` 自动调用 `_wire_metrics()` 挂载 metrics
- [x] 暴露 `get_health()` 方法
- [x] 全局 `get_connection()` 自动具备 metrics 能力

## Phase 3: 动态命令代理

### Task 3.1: 新建 command_proxy.py — CommandProxy
- [x] 新建 `Python/ue_editor_mcp/command_proxy.py`
- [x] 实现 `CommandProxy.__init__(connection)` — 持有连接和 registry 引用
- [x] 实现 `__getattr__` 动态查找：先匹配 command name，再匹配 action_id 的 snake_case
- [x] 动态方法缓存到 `_method_cache`
- [x] 实现 `call(command, **params)` 直接调用
- [x] 实现 `search(query, **kwargs)` 代理到 registry.search
- [x] 实现 `list_commands()` 返回所有可用命令
- [ ] 单元测试：测试动态方法生成、缓存、查找失败、search

### Task 3.2: 重写 ue_bridge.py — 复用统一连接 + CommandProxy
- [x] 删除 `UEConnection` 类（~70行）
- [x] `UEBridge.__init__` 改为创建 `PersistentUnrealConnection` + `CommandProxy`
- [x] 保留 ~25 个高频显式方法
- [x] 这些显式方法内部调用 `self.call(...)` 
- [x] 增加 `__getattr__` 委托到 `self._proxy`
- [x] 增加 `close()` 方法调用 `self._conn.disconnect()`
- [x] 增加 `batch()` 方法
- [x] 增加 `async_submit()` / `async_wait()` 方法
- [x] 保留 REPL 和 CLI 功能（增强：health, stats, search 命令）
- [ ] 验证现有用法全部兼容（docstring 中的示例代码应可运行）
- [x] 文件从 ~1615 行降至 ~650 行（含 docstring + CLI）

## Phase 4: 请求管道化

### Task 4.1: 新建 pipeline.py — BatchContext
- [x] 新建 `Python/ue_editor_mcp/pipeline.py`
- [x] 实现 `BatchContext.__init__(connection, continue_on_error, transactional)`
- [x] 实现 `add(command, params)` 命令入队
- [x] 实现 `__getattr__` 动态排队调用
- [x] 实现 `__enter__ / __exit__` 上下文管理器
- [x] 实现 `execute()` — 打包发送到 `batch_execute`
- [x] 实现 `results` 属性
- [x] 限制最大队列长度（50）
- [ ] 单元测试

### Task 4.2: pipeline.py — AsyncSubmitter
- [x] 实现 `AsyncSubmitter.__init__(connection)`
- [x] 实现 `submit(command, params)` → task_id
- [x] 实现 `poll(task_id)` → 结果或 pending 状态
- [x] 实现 `wait(task_id, timeout, interval)` → 阻塞等待
- [x] 超时后返回超时错误
- [ ] 单元测试

### Task 4.3: 集成 Pipeline 到 UEBridge
- [x] `UEBridge.batch()` 返回 `BatchContext`
- [x] `UEBridge.async_submit()` 和 `UEBridge.async_wait()` 委托到 `AsyncSubmitter`
- [x] 更新 docstring 和示例

## Phase 5: CLI 增强 + MCP 集成

### Task 5.1: 重构 ue_mcp_cli.py
- [ ] 改用 `PersistentUnrealConnection` 替代内联 socket 代码
- [ ] 增加 `--health` 子命令：显示连接状态 + Circuit Breaker 状态 + 心跳状态
- [ ] 增加 `--stats` 子命令：显示 MetricsCollector 摘要
- [ ] 增加 `--batch <file.json>` 子命令：读取 JSON 文件批量执行
- [ ] 保持现有 `<command> [json_params]` 用法不变
- [ ] 改善错误信息：区分连接拒绝、超时、熔断等不同情况
- [ ] 增加 `--timeout <seconds>` 可选参数

### Task 5.2: server_unified.py — Metrics 查询集成
- [ ] 在 `ue_logs_tail` tool handler 中增加 `source="metrics"` 选项
- [ ] 返回 MetricsCollector.get_summary() 结果
- [ ] 增加 `source="health"` 选项返回连接 + Circuit Breaker 状态

### Task 5.3: 生成 ue_bridge.pyi 类型存根
- [ ] 创建脚本或手动生成 `ue_bridge.pyi`
- [ ] 包含所有 ~20 个显式方法的完整签名
- [ ] 包含 `__getattr__` 的 fallback 类型提示
- [ ] 包含 `batch()` / `async_submit()` / `async_wait()` 签名

## Phase 6: 验证与文档

### Task 6.1: 端到端验证
- [ ] 运行 `test_runtime_e2e.py` — 所有现有测试通过
- [ ] 运行 `test_schema_contract.py` — Registry 契约测试通过
- [ ] 运行 `test_context.py` — Context 测试通过
- [ ] 手动验证 `python ue_bridge.py ping` 能正常工作
- [ ] 手动验证 `python ue_mcp_cli.py ping` 能正常工作
- [ ] 手动验证 REPL 模式正常
- [ ] 手动验证 `with ue.batch() as b:` 批量执行正常

### Task 6.2: 文档更新
- [ ] 更新 `docs/architecture.md` — 反映新架构图
- [ ] 更新 `docs/development.md` — 新增 metrics/pipeline 使用说明
- [ ] 更新 `ue_bridge.py` 文件头 docstring
- [ ] 更新 `README.md` — 如果涉及安装/使用变更

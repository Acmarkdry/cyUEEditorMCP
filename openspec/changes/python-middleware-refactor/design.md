# Design: Python Middleware 全面重构

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                          AI / User                              │
├─────────────────────────────────────────────────────────────────┤
│  MCP Protocol (stdio)          CLI (ue_mcp_cli.py)             │
│  server_unified.py             ue_bridge.py (public SDK)       │
├─────────────────────────────────────────────────────────────────┤
│                    ┌──────────────────┐                         │
│                    │  CommandProxy    │ ← 动态方法生成          │
│                    │  (command_proxy) │                         │
│                    └────────┬─────────┘                         │
│                             │                                   │
│              ┌──────────────┼──────────────┐                    │
│              │              │              │                     │
│         ┌────▼───┐    ┌────▼───┐    ┌────▼────┐               │
│         │Pipeline│    │Metrics │    │ Tracer  │               │
│         │  批量  │    │  指标  │    │  追踪   │               │
│         └────┬───┘    └────┬───┘    └────┬────┘               │
│              │              │              │                     │
│         ┌────▼──────────────▼──────────────▼────┐              │
│         │         PersistentUnrealConnection     │              │
│         │     (统一连接层 + Circuit Breaker)      │              │
│         └──────────────────┬────────────────────┘              │
│                            │ TCP 55558                          │
├────────────────────────────┼────────────────────────────────────┤
│                    C++ MCPServer                                │
│                    → Game Thread                                │
│                    → MCPBridge                                  │
│                    → EditorActions                              │
└─────────────────────────────────────────────────────────────────┘
```

## Module Design

### Module 1: 统一连接层 (`connection.py` 增强)

**当前问题**: `ue_bridge.py` 的 `UEConnection` 和 `connection.py` 的 `PersistentUnrealConnection` 完全独立。

**设计方案**:

1. 在 `PersistentUnrealConnection` 上增加 **Circuit Breaker** 状态机：

```python
class CircuitState(Enum):
    CLOSED = "closed"        # 正常，所有请求通过
    OPEN = "open"            # 熔断，所有请求直接失败
    HALF_OPEN = "half_open"  # 试探，允许单个请求通过

@dataclass
class CircuitBreakerConfig:
    failure_threshold: int = 5        # 连续失败次数触发熔断
    recovery_timeout: float = 10.0    # 熔断后多久进入半开
    success_threshold: int = 2        # 半开状态连续成功次数恢复
```

2. 增加 **分级超时**：

```python
class TimeoutTier(Enum):
    PING = 3.0
    FAST = 15.0         # 简单查询
    NORMAL = 30.0       # 常规命令
    SLOW = 120.0        # compile, batch
    EXTRA_SLOW = 240.0  # 大型 batch

# 自动根据命令类型选择超时级别
_TIMEOUT_MAP = {
    "ping": TimeoutTier.PING,
    "compile_blueprint": TimeoutTier.SLOW,
    "compile_material": TimeoutTier.SLOW,
    "batch_execute": TimeoutTier.EXTRA_SLOW,
    "exec_python": TimeoutTier.SLOW,
}
```

3. 增加 **send_command_with_tier** 方法，不破坏现有接口：

```python
def send_command(self, command_type, params=None, *, timeout_tier=None):
    """向后兼容的增强版，timeout_tier 可选。"""
```

4. 暴露 `send_raw_dict` 简易接口给 `ue_bridge.py` 使用：

```python
def send_raw_dict(self, command_type: str, params: dict | None = None) -> dict:
    """发送命令返回原始 dict（非 CommandResult），供 UEBridge 使用。"""
    result = self.send_command(command_type, params)
    return result.to_dict()
```

### Module 2: 动态命令代理 (`command_proxy.py` 新建)

**核心思想**: 利用 `ActionRegistry` 的元数据自动生成方法，不再手写。

```python
class CommandProxy:
    """基于 ActionRegistry 动态代理命令调用。"""

    def __init__(self, connection: PersistentUnrealConnection):
        self._conn = connection
        self._registry = get_registry()
        self._method_cache: dict[str, Callable] = {}

    def __getattr__(self, name: str) -> Callable:
        """按 snake_case 方法名查找 action，返回可调用对象。"""
        if name.startswith("_"):
            raise AttributeError(name)

        if name in self._method_cache:
            return self._method_cache[name]

        # 策略1: 直接匹配 C++ command name (e.g. create_blueprint)
        action = self._registry.get_by_command(name)
        if action is None:
            # 策略2: 匹配 action_id 的 snake_case 形式 (e.g. blueprint.create → blueprint_create)
            dotted = name.replace("_", ".", 1)  # first underscore → dot
            action = self._registry.get(dotted)

        if action is None:
            raise AttributeError(
                f"No action found for '{name}'. Use search() to find actions."
            )

        # 生成动态方法
        def dynamic_method(**kwargs) -> dict:
            params = {k: v for k, v in kwargs.items() if v is not None}
            return self._conn.send_raw_dict(action.command, params or None)

        dynamic_method.__name__ = name
        dynamic_method.__doc__ = f"{action.description}\n\nSchema: {action.input_schema}"
        self._method_cache[name] = dynamic_method
        return dynamic_method

    def call(self, command: str, **params) -> dict:
        """直接按 C++ command type 调用（不经过 registry 查找）。"""
        clean = {k: v for k, v in params.items() if v is not None}
        return self._conn.send_raw_dict(command, clean or None)

    def search(self, query: str, **kwargs) -> list[dict]:
        """搜索可用的命令。"""
        return self._registry.search(query, **kwargs)
```

**UEBridge 重构方案**:

```python
class UEBridge:
    """公开 SDK — 保留高频显式方法 + 其余动态代理。"""

    def __init__(self, host="127.0.0.1", port=55558):
        from ue_editor_mcp.connection import PersistentUnrealConnection, ConnectionConfig
        config = ConnectionConfig(host=host, port=port)
        self._conn = PersistentUnrealConnection(config)
        self._proxy = CommandProxy(self._conn)

    def call(self, command: str, **params) -> dict:
        return self._proxy.call(command, **params)

    # === 保留 ~20 个高频显式方法（IDE 补全友好）===

    def ping(self) -> dict: ...
    def save_all(self) -> dict: ...
    def create_blueprint(self, name, parent_class, *, path=None) -> dict: ...
    def compile_blueprint(self, blueprint_name) -> dict: ...
    def connect_blueprint_nodes(self, ...) -> dict: ...
    # ... 其他高频方法

    # === 其余 78+ 个方法通过 __getattr__ 动态代理 ===

    def __getattr__(self, name):
        return getattr(self._proxy, name)
```

### Module 3: 请求管道化 (`pipeline.py` 新建)

**批量执行上下文管理器**:

```python
class BatchContext:
    """收集一系列命令，一次性发送到 C++ batch_execute。"""

    def __init__(self, connection, *, continue_on_error=False, transactional=False):
        self._conn = connection
        self._commands: list[dict] = []
        self._continue_on_error = continue_on_error
        self._transactional = transactional
        self._results: list[dict] = []

    def add(self, command: str, params: dict | None = None) -> int:
        """添加命令到队列，返回索引。"""
        self._commands.append({"type": command, "params": params or {}})
        return len(self._commands) - 1

    def __getattr__(self, name: str):
        """动态方法：bc.create_blueprint(...) → 自动加入队列。"""
        def queued_call(**kwargs):
            params = {k: v for k, v in kwargs.items() if v is not None}
            return self.add(name, params)
        return queued_call

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            self.execute()
        return False

    def execute(self) -> list[dict]:
        """执行所有排队的命令。"""
        if not self._commands:
            return []
        batch_params = {
            "commands": self._commands,
            "continue_on_error": self._continue_on_error,
            "transactional": self._transactional,
        }
        result = self._conn.send_raw_dict("batch_execute", batch_params)
        self._results = result.get("results", [])
        return self._results

    @property
    def results(self) -> list[dict]:
        return self._results
```

**异步提交器**:

```python
class AsyncSubmitter:
    """提交命令到 C++ async_execute，轮询结果。"""

    def __init__(self, connection):
        self._conn = connection

    def submit(self, command: str, params: dict | None = None) -> str:
        """提交异步命令，返回 task_id。"""
        result = self._conn.send_raw_dict("async_execute", {
            "command": command,
            "params": params or {},
        })
        return result.get("result", {}).get("task_id", "")

    def poll(self, task_id: str) -> dict:
        """轮询任务结果。"""
        return self._conn.send_raw_dict("get_task_result", {"task_id": task_id})

    def wait(self, task_id: str, *, timeout: float = 120, interval: float = 0.5) -> dict:
        """阻塞等待任务完成。"""
        import time
        deadline = time.time() + timeout
        while time.time() < deadline:
            result = self.poll(task_id)
            status = result.get("result", {}).get("status", "")
            if status in ("completed", "failed"):
                return result
            time.sleep(interval)
        return {"success": False, "error": f"Task {task_id} timed out after {timeout}s"}
```

**UEBridge 集成**:

```python
class UEBridge:
    # ...
    def batch(self, *, continue_on_error=False, transactional=False) -> BatchContext:
        """返回批量执行上下文。用法: with ue.batch() as b: b.xxx()"""
        return BatchContext(self._conn, continue_on_error=continue_on_error,
                          transactional=transactional)

    def async_submit(self, command: str, **params) -> str:
        """异步提交命令。"""
        return self._async.submit(command, params)

    def async_wait(self, task_id: str, **kwargs) -> dict:
        """等待异步命令完成。"""
        return self._async.wait(task_id, **kwargs)
```

### Module 4: 可观测性 (`metrics.py` + `tracer.py` 新建)

**Metrics 收集器**:

```python
@dataclass
class RequestMetrics:
    """单次请求的指标。"""
    command: str
    start_time: float
    end_time: float
    success: bool
    error: str | None = None
    request_id: str = ""

    @property
    def duration_ms(self) -> float:
        return (self.end_time - self.start_time) * 1000

class MetricsCollector:
    """全局指标收集器。"""

    def __init__(self, max_history: int = 1000):
        self._history: deque[RequestMetrics] = deque(maxlen=max_history)
        self._counters: dict[str, int] = defaultdict(int)
        self._error_counters: dict[str, int] = defaultdict(int)
        self._lock = threading.Lock()

    def record(self, metrics: RequestMetrics):
        with self._lock:
            self._history.append(metrics)
            self._counters[metrics.command] += 1
            if not metrics.success:
                self._error_counters[metrics.command] += 1

    def get_summary(self, last_n: int = 100) -> dict:
        """返回最近 N 条请求的统计摘要。"""
        with self._lock:
            recent = list(self._history)[-last_n:]
        if not recent:
            return {"total": 0}

        durations = [m.duration_ms for m in recent]
        successes = sum(1 for m in recent if m.success)
        return {
            "total": len(recent),
            "success_rate": round(successes / len(recent) * 100, 1),
            "avg_ms": round(sum(durations) / len(durations), 1),
            "p50_ms": round(sorted(durations)[len(durations) // 2], 1),
            "p95_ms": round(sorted(durations)[int(len(durations) * 0.95)], 1),
            "max_ms": round(max(durations), 1),
            "by_command": dict(self._counters),
            "errors_by_command": dict(self._error_counters),
        }
```

**Request Tracer**:

```python
class RequestTracer:
    """请求链路追踪。"""

    def __init__(self, metrics: MetricsCollector):
        self._metrics = metrics

    @contextmanager
    def trace(self, command: str, params: dict | None = None):
        """上下文管理器，自动记录请求指标。"""
        request_id = f"{command}_{uuid4().hex[:8]}"
        start = time.perf_counter()
        result_holder = {"success": False, "error": None}

        try:
            yield result_holder
        finally:
            end = time.perf_counter()
            self._metrics.record(RequestMetrics(
                command=command,
                start_time=start,
                end_time=end,
                success=result_holder["success"],
                error=result_holder["error"],
                request_id=request_id,
            ))
```

**集成到连接层**:

```python
class PersistentUnrealConnection:
    def __init__(self, ...):
        # ...
        self._metrics = MetricsCollector()
        self._tracer = RequestTracer(self._metrics)

    def send_command(self, command_type, params=None, *, timeout_tier=None):
        with self._tracer.trace(command_type) as ctx:
            result = self._send_impl(command_type, params, timeout_tier)
            ctx["success"] = result.success
            ctx["error"] = result.error
            return result

    @property
    def metrics(self) -> MetricsCollector:
        return self._metrics
```

### Module 5: CLI 增强 (`ue_mcp_cli.py` 重构)

```python
# 新增子命令
python ue_mcp_cli.py ping                                    # 现有
python ue_mcp_cli.py <command> [json_params]                 # 现有
python ue_mcp_cli.py --batch commands.json                   # 新：批量执行
python ue_mcp_cli.py --stats                                 # 新：显示性能指标
python ue_mcp_cli.py --health                                # 新：连接健康检查
python ue_mcp_cli.py --repl                                  # 现有（来自 ue_bridge.py）
```

## File Changes Summary

| File | Action | Description |
|------|--------|-------------|
| `connection.py` | **MODIFY** | +CircuitBreaker, +TimeoutTier, +send_raw_dict, +metrics 集成 |
| `command_proxy.py` | **NEW** | 动态命令代理，基于 ActionRegistry |
| `pipeline.py` | **NEW** | BatchContext + AsyncSubmitter |
| `metrics.py` | **NEW** | MetricsCollector + RequestTracer |
| `ue_bridge.py` | **REWRITE** | 1600→300行，复用 connection.py + CommandProxy |
| `ue_mcp_cli.py` | **MODIFY** | +batch, +stats, +health 子命令 |
| `server_unified.py` | **MODIFY** | metrics 查询集成到 ue_logs_tail |
| `ue_bridge.pyi` | **NEW** | 类型存根，保证 IDE 补全 |

## Backward Compatibility

- `UEBridge` 所有现有方法签名不变
- `UEBridge.call(command, **params)` 接口不变
- `ue_mcp_cli.py` 现有 CLI 用法不变
- `server_unified.py` 的 12 个 MCP tool 不变
- `PersistentUnrealConnection.send_command()` 签名不变（新增可选 keyword-only 参数）

## Testing Strategy

- 为 `CommandProxy` 增加单元测试：动态方法生成 + registry 查找
- 为 `BatchContext` 增加单元测试：命令排队 + 执行
- 为 `CircuitBreaker` 增加状态转换测试
- 为 `MetricsCollector` 增加统计计算测试
- 现有 `test_runtime_e2e.py` 应全部通过（向后兼容验证）

# Proposal: Python Middleware 全面重构

## Summary

对 UEEditorMCP 的 Python 中间层进行全面重构，统一连接层、自动化命令生成、引入请求管道化和弹性连接模式，并增加全链路可观测性。

## Motivation

当前 Python 中间层存在以下核心问题：

1. **连接层重复实现**：`ue_bridge.py` 有自己的 `UEConnection` 类（~70行），`connection.py` 有完整的 `PersistentUnrealConnection`（~550行），两者完全独立，功能重叠。
2. **1600行样板代码**：`ue_bridge.py` 手写了98个方法，每个都是 `return self.call("xxx", ...)` 的样板，新增 C++ Action 时需要同步手写 Python wrapper。
3. **Game Thread 同步阻塞**：每条命令都通过 `AsyncTask(GameThread)` + `FEvent::Wait()` 阻塞等待，Python 侧没有利用 `async_execute` 提供并发能力。
4. **错误处理不统一**：`connection.py` 区分 Format 1/Format 2 响应格式、`ue_bridge.py` 只做简单重试，CLI 错误信息不友好。
5. **缺乏批量能力**：`ue_bridge.py` 每次调用都是独立 TCP 请求，没有 batch/pipeline 支持。
6. **缺乏可观测性**：没有请求耗时统计、连接健康仪表板、链路追踪。

## Goals

- **G1**: 统一为单一连接层，消除重复实现
- **G2**: 基于 action registry 元数据自动生成 UEBridge 方法，将1600行降至 <200行
- **G3**: 引入请求管道化，支持批量命令、并发提交、背压控制
- **G4**: 增加 Circuit Breaker 模式和分级超时，提升连接弹性
- **G5**: 添加全链路 Metrics 收集、RequestTracer、性能仪表板
- **G6**: 保持完全向后兼容 — 现有 CLI 和 SDK 调用方式不变

## Non-Goals

- 不修改 C++ 侧的 MCPServer / MCPBridge 实现
- 不改变 MCP 协议层（server_unified.py 的12个 tool 接口不变）
- 不引入外部依赖（保持零额外 pip 包）

## Approach

### Phase 1: 统一连接层
- 将 `ue_bridge.py` 的 `UEConnection` 替换为基于 `PersistentUnrealConnection` 的薄包装
- 让 `UEBridge` 直接使用 `connection.py` 的全局连接实例
- 保留 `ue_bridge.py` 作为公开 SDK，但底层复用同一个连接

### Phase 2: 动态命令生成
- 创建 `command_proxy.py` — 基于 `registry/actions.py` 的元数据动态生成方法
- `UEBridge.__getattr__` 动态代理未预定义的命令到 action registry
- 保留高频命令的显式方法签名（IDE 补全友好），低频命令自动生成

### Phase 3: 请求管道化
- 创建 `pipeline.py` — 命令队列 + 批量打包 + 异步提交
- 支持 `with ue.batch() as b: b.xxx(); b.yyy()` 语法糖
- 集成 C++ 侧已有的 `batch_execute` 和 `async_execute`

### Phase 4: 连接弹性增强
- 在 `connection.py` 中增加 Circuit Breaker 状态机
- 分级超时：ping 3s, 普通命令 30s, 编译命令 120s, batch 240s
- 优雅降级：连接中断时返回缓存的上下文数据

### Phase 5: 可观测性层
- 创建 `metrics.py` — 请求计数、耗时直方图、错误率
- 创建 `tracer.py` — 请求链路追踪（request_id → C++ → response）
- 扩展 `ue_logs_tail` 支持 metrics 查询
- CLI 增加 `--stats` 和 `--health` 子命令

## Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| 动态方法生成破坏 IDE 补全 | 中 | 保留高频方法的显式签名 + 生成 .pyi stub 文件 |
| 批量打包引入时序问题 | 中 | 默认禁用自动打包，仅显式 batch 上下文启用 |
| Circuit Breaker 误判断开 | 低 | 保守阈值 + 半开状态快速恢复 |
| 向后兼容性破坏 | 高 | 所有公开 API 保持签名不变，仅替换内部实现 |

## Success Criteria

- `ue_bridge.py` 从 1600+ 行降至 <300 行（不含自动生成）
- 连接层只有一套实现，代码重复率 <5%
- 批量操作（10条命令）延迟降低 >50%（1 RTT vs 10 RTT）
- 连接中断后 <5s 自动恢复
- 所有现有测试通过，无 API 签名变更

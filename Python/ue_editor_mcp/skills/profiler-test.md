# 性能分析与自动化测试工作流

## 帧统计

```
ue_actions_run(action="profiler.frame_stats", params={})
```

返回当前帧的性能数据：FPS、帧时间（ms）、游戏线程时间、渲染线程时间、GPU 时间等。

## 内存统计

```
ue_actions_run(action="profiler.memory_stats", params={})
```

返回内存使用概况：已用物理内存、可用内存、纹理内存、Mesh 内存等。

## 性能诊断工作流

```
1. profiler.frame_stats   → 获取帧性能基线
2. （执行某些操作，如加载 Sublevel、Spawn Actor）
3. profiler.frame_stats   → 对比操作后的帧性能
4. profiler.memory_stats  → 检查内存是否有异常增长
```

## 列出自动化测试

```
ue_actions_run(action="test.list", params={"filter": "MCP"})
```

按关键字过滤 UE 自动化测试列表。省略 `filter` 返回所有测试。

## 运行自动化测试

```
ue_actions_run(action="test.run", params={"test_filter": "Project.Functional.MyTest"})
```

执行匹配的自动化测试并返回结果（通过/失败/错误数量）。

## 提示

- `frame_stats` 在 PIE 运行时更有意义（能看到实际游戏帧性能）
- `memory_stats` 适合检测资产加载前后的内存变化
- `test.list` 和 `test.run` 对应 UE 的 Session Frontend → Automation 面板
- 自动化测试需要在编辑器中运行，不支持 Commandlet 模式

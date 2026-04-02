# AnimGraph 动画图工作流指南

本文档描述通过 MCP 操作 Unreal Engine 动画蓝图（Animation Blueprint）的典型工作流。

---

## 关键概念

- **节点 GUID**：每个图节点都有唯一 GUID，写入操作（连接、设置属性等）需要先通过读取操作获取 GUID。
- **compact 模式**：`describe_topology`、`get_state_subgraph`、`get_transition_rule` 均支持 `compact: true` 参数，可省略隐藏引脚和元数据，显著减少输出体积。建议在大型图中优先使用。
- **编译诊断**：修改动画蓝图后务必调用 `animgraph.compile`，通过 `error_count` 和 `errors` 字段确认修改正确。

---

## 工作流一：读取查询

了解现有动画蓝图的结构。

```
1. animgraph.list_graphs
   → 获取所有图名称、类型、节点数量、Skeleton 引用

2. animgraph.describe_topology  (compact: true)
   → 获取指定图（如 AnimGraph）的节点/引脚/边拓扑

3. animgraph.get_state_machine
   → 获取状态机的状态列表、转换规则、入口状态

4. animgraph.get_state_subgraph
   → 获取某个状态内部的动画节点拓扑（含资产引用）

5. animgraph.get_transition_rule
   → 获取转换规则的条件表达式（含引用的蓝图变量）
```

---

## 工作流二：创建开发

从零搭建一个完整的动画蓝图。

```
1. animgraph.create_blueprint
   → 创建动画蓝图，指定 skeleton 和 parent_class

2. animgraph.add_state_machine
   → 在 AnimGraph 中添加状态机节点（如 "Locomotion"）

3. animgraph.add_state  (多次调用)
   → 添加各个状态（Idle、Walk、Run 等）

4. animgraph.add_transition  (多次调用)
   → 在状态之间添加转换规则

5. animgraph.add_node
   → 在状态子图中添加动画节点（AnimSequencePlayer 等）
   → 通过 anim_asset 参数直接绑定动画资产

6. animgraph.connect_nodes
   → 连接动画节点引脚（需要先通过 get_state_subgraph 获取节点 GUID）

7. animgraph.compile
   → 编译并验证，检查 error_count == 0
```

---

## 工作流三：修改编辑

调整现有动画蓝图的状态机结构。

```
1. animgraph.get_state_machine
   → 读取当前状态机结构，获取状态名称和转换规则

2. animgraph.rename_state
   → 重命名状态（如 "Walk" → "Walking"）

3. animgraph.set_transition_priority
   → 调整转换规则优先级

4. animgraph.remove_state / animgraph.remove_transition
   → 删除不需要的状态或转换规则（注意：不能删除入口状态）

5. animgraph.compile
   → 编译验证修改结果
```

---

## 关键模式说明

### 获取节点 GUID 的流程

写入操作（`connect_nodes`、`set_node_property`、`disconnect_node`）需要节点 GUID：

```
animgraph.get_state_subgraph  →  nodes[].node_id  →  用于后续写入操作
animgraph.describe_topology   →  nodes[].node_id  →  用于后续写入操作
```

### compact 模式降低输出体积

大型状态机或复杂图中，建议使用 compact 模式：

```json
{"blueprint_name": "ABP_Player", "graph_name": "AnimGraph", "compact": true}
```

### 编译诊断流程

```
animgraph.compile 返回：
  compiled: true/false
  status: "UpToDate" | "UpToDateWithWarnings" | "Error"
  error_count: N
  errors: [{node, node_id, message}, ...]

如果 error_count > 0，根据 errors[].node_id 定位问题节点。
```

### 动画节点类型速查

| node_type | 说明 |
|-----------|------|
| `AnimSequencePlayer` | 播放单个动画序列 |
| `BlendSpacePlayer` | 播放混合空间 |
| `LayeredBlendPerBone` | 按骨骼分层混合 |
| `TwoWayBlend` | 双向 Alpha 混合 |
| `BlendPosesByBool` | 布尔条件混合 |
| `BlendPosesByInt` | 整数条件混合 |

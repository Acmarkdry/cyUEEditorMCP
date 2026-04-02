# 动作域参考

## 动作域（核心）

| 域 | 数量 | 说明 | 示例 ID |
|----|------|------|--------|
| `blueprint.*` | 11 | 蓝图增删改查、组件、接口、完整快照 | `blueprint.create`、`blueprint.compile`、`blueprint.add_component`、`blueprint.describe_full` |
| `batch.*` | 1 | 批量命令执行 | `batch.execute` |
| `component.*` | 4 | 组件属性、网格体、物理、事件绑定 | `component.set_property`、`component.set_static_mesh`、`component.bind_event` |
| `editor.*` | 18 | 关卡 Actor、视口、资产、日志、生命周期、源码控制差异 | `editor.spawn_actor`、`editor.list_assets`、`editor.get_logs`、`editor.is_ready` |
| `layout.*` | 3 | 节点自动布局 | `layout.auto_selected`、`layout.auto_subtree`、`layout.auto_blueprint` |
| `node.*` | 19 | 蓝图图节点创建 | `node.add_event`、`node.add_function_call`、`node.add_branch` |
| `variable.*` | 8 | 变量增删改查、默认值、元数据 | `variable.create`、`variable.add_getter`、`variable.set_default` |
| `function.*` | 4 | 函数创建、管理与重构 | `function.create`、`function.call`、`function.delete`、`function.rename` |
| `dispatcher.*` | 4 | 事件派发器管理 | `dispatcher.create`、`dispatcher.call`、`dispatcher.bind` |
| `graph.*` | 18 | 图连线、检视、注释、补丁、折叠重构 | `graph.connect_nodes`、`graph.describe`、`graph.apply_patch` |
| `macro.*` | 1 | 宏管理 | `macro.rename` |
| `material.*` | 16 | 材质创建、编辑、编译诊断、图检视与布局、关卡应用 | `material.create`、`material.compile`、`material.auto_layout` |
| `widget.*` | 21 | UMG 控件蓝图（24 种类型）+ MVVM | `widget.create`、`widget.add_component`、`widget.mvvm_add_viewmodel` |
| `input.*` | 4 | 增强输入系统 | `input.create_action`、`input.create_mapping_context` |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 | `animgraph.list_graphs`、`animgraph.create_blueprint`、`animgraph.compile` |

## AI 工作流

### 快速路径（1 次往返）

```
ue_batch(actions=[
  {action_id: "blueprint.create", params: {name: "BP_Player", parent_class: "Character"}},
  {action_id: "variable.create", params: {blueprint_name: "BP_Player", variable_name: "Speed", variable_type: "Float"}},
  {action_id: "blueprint.compile", params: {blueprint_name: "BP_Player"}}
])
```

### 发现路径（3 次往返）

```
1. ue_actions_search(query="create blueprint")
2. ue_actions_schema(action_id="blueprint.create")
3. ue_actions_run(action_id="blueprint.create", params={...})
```

## 编译诊断

### 蓝图

- `blueprint.compile` 返回 `status`、`error_count`、`warning_count`
- 详细诊断：`editor.get_logs(count=200, category="LogBlueprint", min_verbosity="Error")`
- Fatal 上下文：`category="LogOutputDevice"`

### 材质

- `material.compile` 同步等待着色器编译，返回 `errors[]`（含 `message`、`expression_name`、`expression_class`）
- 详细日志：`ue_logs_tail(source="editor", category="LogMaterial", min_verbosity="Error")`

# 动作域参考

## ue_python_exec

`ue_python_exec` 是一个 MCP 工具，可以在 Unreal 的嵌入式 Python 环境中执行任意代码。
它替代了 45+ 原有 C++ 动作（Actor 管理、蓝图创建/编译、材质操作、视口控制、PIE 等）。

```python
# 示例：列出所有关卡 Actor
import unreal
_result = [a.get_name() for a in unreal.EditorLevelLibrary.get_all_level_actors()]
```

设置 `_result = <value>` 返回数据。详见 [python-api skill](../Python/ue_editor_mcp/skills/python-api.md)。

## 动作域（核心）

| 域 | 数量 | 说明 | 示例 ID |
|----|------|------|--------|
| `python.*` | 1 | Python 代码执行（替代 45+ C++ 动作） | `python.exec` |
| `blueprint.*` | 2 | 蓝图内省与完整快照（创建/编译→Python） | `blueprint.get_summary`、`blueprint.describe_full` |
| `graph.*` | 18 | 图连线、检视、注释、补丁、折叠重构 | `graph.connect_nodes`、`graph.describe`、`graph.apply_patch` |
| `node.*` | 19 | 蓝图图节点创建 | `node.add_event`、`node.add_function_call`、`node.add_branch` |
| `variable.*` | 8 | 变量增删改查、默认值、元数据 | `variable.create`、`variable.add_getter`、`variable.set_default` |
| `function.*` | 4 | 函数创建、管理与重构 | `function.create`、`function.call`、`function.delete`、`function.rename` |
| `dispatcher.*` | 4 | 事件派发器管理 | `dispatcher.create`、`dispatcher.call`、`dispatcher.bind` |
| `layout.*` | 4 | 节点自动布局 | `layout.auto_selected`、`layout.auto_subtree`、`layout.auto_blueprint` |
| `macro.*` | 1 | 宏管理 | `macro.rename` |
| `material.*` | 14 | 材质分析、诊断、布局（创建/编译→Python） | `material.get_summary`、`material.auto_layout`、`material.diagnose` |
| `widget.*` | 21 | UMG 控件蓝图（24 种类型）+ MVVM | `widget.create`、`widget.add_component`、`widget.mvvm_add_viewmodel` |
| `input.*` | 4 | 增强输入系统 | `input.create_action`、`input.create_mapping_context` |
| `animgraph.*` | 18 | AnimGraph 读取/创建/修改/编译 | `animgraph.list_graphs`、`animgraph.create_blueprint`、`animgraph.compile` |
| `editor.*` | 8 | 日志、缩略图、源码控制 diff（Actor/PIE→Python） | `editor.get_logs`、`editor.diff_against_depot`、`editor.is_ready` |

## AI 工作流

### 推荐路径：ue_python_exec

```
# 大部分操作都可以通过 ue_python_exec 完成
ue_python_exec(code="import unreal\nfactory = unreal.BlueprintFactory()\n...")
```

### 批量路径（1 次往返）

```
ue_batch(actions=[
  {action_id: "variable.create", params: {blueprint_name: "BP_Player", variable_name: "Speed", variable_type: "Float"}},
  {action_id: "node.add_event", params: {blueprint_name: "BP_Player", event_name: "BeginPlay"}}
])
```

### 异步路径

```
# 提交长时间运行的操作
ue_async_run(action="submit", command="exec_python", params={code: "..."})
# → {task_id: "uuid"}

# 轮询结果
ue_async_run(action="poll", task_id="uuid")
# → {status: "completed", result: {...}}
```

### 发现路径（3 次往返）

```
1. ue_actions_search(query="material layout")
2. ue_actions_schema(action_id="material.auto_layout")
3. ue_actions_run(action_id="material.auto_layout", params={...})
```

## 编译诊断

### 蓝图

- 通过 `ue_python_exec` 编译蓝图
- 详细诊断：`editor.get_logs` → `category="LogBlueprint"`, `min_verbosity="Error"`
- Fatal 上下文：`category="LogOutputDevice"`

### 材质

- 通过 `ue_python_exec` 或保留的 C++ 动作操作材质
- 使用 `material.diagnose` 检查常见问题
- 详细日志：`ue_logs_tail(source="editor", category="LogMaterial", min_verbosity="Error")`
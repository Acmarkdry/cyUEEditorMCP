## Why

当前 cyUEEditorMCP 有 159 个 C++ 手写 Action，其中约 45 个（editor/actor 操作、基础材质操作、组件属性等）完全可以被 Unreal 内嵌 Python API（`unreal.*` 模块）替代。维护这些冗余的 C++ Action 增加了代码量和同步成本。同时，所有命令只能同步执行，长耗时操作（编译、批量创建）阻塞客户端；且插件只能在编辑器运行时通过 MCP 使用，无法用于 CI/CD。

参考 UECLI 项目（github.com/wlxklyh/UECLI）的优秀设计，我们应当：
1. 新增 `ue_python_exec` 工具，让 AI 直接在 Unreal 内嵌 Python 中执行代码，替代大量冗余 C++ Action
2. 删除被 Python API 完全覆盖的 ~45 个 C++ Action，大幅精简代码
3. 新增异步命令模式和 Commandlet 模式

## What Changes

### 做减法——删除冗余 C++ Action（~45 个）
- **删除 editor 域**：`get_actors`, `find_actors`, `spawn_actor`, `delete_actor`, `set_actor_transform`, `get_actor_properties`, `set_actor_property`, `focus_viewport`, `get_viewport_transform`, `set_viewport_transform`, `save_all`, `list_assets`, `rename_assets`, `get_selected_assets`, `rename_actor_label`, `set_actor_folder`, `select_actors`, `get_outliner_tree`, `open_asset_editor`, `start_pie`, `stop_pie`, `get_pie_state`
- **删除 blueprint 域**：`create_blueprint`, `compile_blueprint`, `set_blueprint_property`, `spawn_blueprint_actor`, `set_parent_class`, `add_interface`, `remove_interface`, `add_component`, `create_colored_material`
- **删除 component 域**：`set_component_property`, `set_static_mesh_properties`, `set_physics_properties`
- **删除 material 基础操作**：`create_material`, `add_material_expression`, `connect_material_expressions`, `connect_to_material_output`, `set_material_expression_property`, `compile_material`, `create_material_instance`, `create_post_process_volume`, `apply_material_to_component`, `apply_material_to_actor`
- **对应删除**：Python ActionDef、C++ FEditorAction 子类、MCPBridge 注册
- **保留**：所有蓝图节点操作(node.*)、图操作(graph.*)、AnimGraph、UMG Widget、Layout、材质分析/诊断等 Python API 无法替代的 ~95 个 Action

### 做加法——新增能力
- **新增 `ue_python_exec` MCP 工具**（第 9 个工具）：在 Unreal 内嵌 Python 环境中执行任意代码，完整捕获返回值和异常
- **新增 `exec_python` C++ Action**：调用 `IPythonScriptPlugin::ExecPythonCommand()`，在游戏线程执行 Python 代码
- **新增异步命令支持**：C++ 端 `async_execute` / `get_task_result` + Python 端 `ue_async_run` 工具（第 10 个）
- **新增 Commandlet 模式**：`UUEEditorMCPCommandlet`，支持 `-run=UEEditorMCP -command=xxx`
- **新增 `python-api` Skill 文档**：教 AI 如何使用 `unreal.*` API 替代已删除的 Action

### 联动更新
- **Skills 系统**：更新 `blueprint-core`、`editor-level`、`materials` 三个 SkillDef，移除已删除 action_id，新增 python-api skill
- **Skill 工作流文档**：更新 `blueprint-core.md`、`editor-level.md`、`materials.md`，示例改用 `ue_python_exec`
- **Tests**：更新 `test_skills.py`、`test_schema_contract.py` 以反映 action 变更
- **文档**：更新 `README.md`、`docs/architecture.md`、`docs/actions.md`、`docs/development.md`

## Capabilities

### New Capabilities
- `python-exec`: 在 Unreal 内嵌 Python 中执行任意代码的能力——包含 C++ exec_python Action、MCP ue_python_exec 工具、Python API 教学 Skill 文档
- `async-commands`: 异步命令执行能力——支持 async_execute 提交长耗时操作并返回 task_id，通过 get_task_result 轮询结果
- `commandlet-mode`: Commandlet 无窗口执行模式——支持通过 UE 命令行 `-run=UEEditorMCP` 执行命令

### Modified Capabilities
<!-- 无现有 specs 需要修改 -->

## Impact

- **C++ 端**：
  - 删除 ~45 个 FEditorAction 子类（来自 EditorActions.cpp、MaterialActions.cpp、BlueprintActions.cpp）
  - MCPBridge.cpp：删除对应 ActionHandlers.Add 行，新增 exec_python 和异步任务管理
  - MCPServer.cpp：新增 async_execute/get_task_result 快速路径
  - 新增 `UEEditorMCPCommandlet.h/.cpp`
  - `UEEditorMCP.Build.cs`：新增 `PythonScriptPlugin` 依赖
  - `UEEditorMCP.uplugin`：新增 `PythonScriptPlugin` 依赖，版本号更新
- **Python 端**：
  - `registry/actions.py`：删除 ~45 个 ActionDef，新增 `python.exec`
  - `server_unified.py`：新增 `ue_python_exec` 和 `ue_async_run` 两个 MCP 工具
  - `skills/__init__.py`：更新 SkillDef，新增 `python-api` skill
  - `skills/editor-level.md`、`blueprint-core.md`、`materials.md`：更新示例
  - 新增 `skills/python-api.md`：Python API 教学文档
- **Tests**：
  - `test_skills.py`：适配 action 变更
  - `test_schema_contract.py`：适配删除的 actions
- **文档**：
  - `README.md`：工具数量、action 数量、架构描述更新
  - `docs/architecture.md`：新增 Python exec 和异步路径
  - `docs/actions.md`：删除已移除 action 的条目
  - `docs/development.md`：新增 Python exec 开发指南
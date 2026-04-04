## 1. C++ — Python 执行基础设施

- [x] 1.1 在 `UEEditorMCP.uplugin` 中添加 `PythonScriptPlugin` 插件依赖（Enabled: true）
- [x] 1.2 在 `UEEditorMCP.Build.cs` 中添加 `"PythonScriptPlugin"` 到 PrivateDependencyModuleNames
- [x] 1.3 创建 `Source/UEEditorMCP/Private/Actions/PythonActions.cpp`，实现 `FExecPythonAction`：通过 `IPythonScriptPlugin::Get()->ExecPythonCommand()` 执行代码，包裹 Python stdout/stderr 重定向脚本，读取 `_result` 变量，返回 `{return_value, stdout, stderr}`
- [x] 1.4 创建 `Source/UEEditorMCP/Public/Actions/PythonActions.h`，声明 `FExecPythonAction`
- [x] 1.5 在 `MCPBridge.cpp` 的 `RegisterActions()` 中添加 `ActionHandlers.Add(TEXT("exec_python"), MakeShared<FExecPythonAction>())`

## 2. C++ — 异步命令基础设施

- [x] 2.1 在 `MCPBridge.h` 中添加异步任务数据结构：`FAsyncTaskEntry`（task_id, status, result, created_time）、`TMap<FString, FAsyncTaskEntry> AsyncTasks` + `FCriticalSection AsyncTasksLock`
- [x] 2.2 在 `MCPBridge.h/.cpp` 中添加 `SubmitAsyncTask(command, params) → task_id`：生成 FGuid、注册 pending 状态、AsyncTask(GameThread) 执行 ExecuteCommandSafe 并回写结果
- [x] 2.3 在 `MCPBridge.h/.cpp` 中添加 `GetTaskResult(task_id) → result_or_pending`：线程安全查询、完成则返回结果并删除条目
- [x] 2.4 在 `MCPBridge.h/.cpp` 中添加 `CleanupExpiredTasks()`：清除超过 300 秒的条目，在 SubmitAsyncTask 或 GetTaskResult 时顺便调用
- [x] 2.5 在 `MCPServer.cpp` 的 `FMCPClientHandler::Run()` 中，与 `ping`/`close` 同层添加 `async_execute` 和 `get_task_result` 快速路径，直接调用 MCPBridge 的 Submit/Get 方法，不走游戏线程等待

## 3. C++ — Commandlet 模式

- [x] 3.1 创建 `Source/UEEditorMCP/Public/UEEditorMCPCommandlet.h`，声明 `UUEEditorMCPCommandlet : public UCommandlet`
- [x] 3.2 创建 `Source/UEEditorMCP/Private/UEEditorMCPCommandlet.cpp`，实现 `Main()` 入口，解析 `-command` / `-params` / `-help` / `-batch` / `-json` / `-format` 参数
- [x] 3.3 Commandlet 中通过 `GEditor->GetEditorSubsystem<UMCPBridge>()` 获取 MCPBridge 实例，复用 ActionHandlers 和 ExecuteCommandSafe
- [x] 3.4 实现 `-help` 输出：遍历 ActionHandlers.GetKeys() 输出命令列表
- [x] 3.5 实现 `-batch -file=xxx` 批量模式：读取 JSON 文件，顺序执行
- [x] 3.6 实现 `-json` 输出格式：`JSON_BEGIN\n{...}\nJSON_END` 包裹
- [x] 3.7 实现 `-format=json|markdown` schema 导出

## 4. C++ — 删除冗余 Action

- [x] 4.1 从 `MCPBridge.cpp` 的 `RegisterActions()` 中删除 ~45 个 ActionHandlers.Add 行（editor 域 22 个、blueprint 域 9 个、component 域 3 个、material 基础域 10 个、batch_execute 1 个）
- [x] 4.2 从 `Source/UEEditorMCP/Private/Actions/EditorActions.cpp` 中删除对应的 FEditorAction 子类实现（get_actors, find_actors, spawn_actor, delete_actor, set_actor_transform, get/set_actor_properties, focus/get/set_viewport, save_all, list_assets, rename_assets, get_selected_assets, rename_actor_label, set_actor_folder, select_actors, get_outliner_tree, open_asset_editor, start/stop/get_pie）
- [x] 4.3 从 `Source/UEEditorMCP/Private/Actions/BlueprintActions.cpp` 中删除对应子类（create_blueprint, compile_blueprint, set_blueprint_property, spawn_blueprint_actor, set_parent_class, add/remove_interface, add_component, create_colored_material）
- [x] 4.4 从 `Source/UEEditorMCP/Private/Actions/MaterialActions.cpp` 中删除基础操作子类（create_material, add_expression, connect_expressions, connect_to_output, set_expression_property, compile_material, create_instance, create_post_process_volume, apply_to_component, apply_to_actor）
- [x] 4.5 从对应的 `.h` 头文件中删除已删除子类的声明
- [x] 4.6 更新 `UEEditorMCP.uplugin` 的 VersionName

## 5. Python — 新增 MCP 工具

- [x] 5.1 在 `server_unified.py` 的 TOOLS 列表中添加 `ue_python_exec` Tool 定义（name, description, inputSchema with code + timeout_seconds）
- [x] 5.2 在 `server_unified.py` 的 `_handle_tool` 中添加 `ue_python_exec` 处理分支：发送 `exec_python` 命令到 C++ 端
- [x] 5.3 在 `server_unified.py` 的 TOOLS 列表中添加 `ue_async_run` Tool 定义（submit 和 poll 两种 action）
- [x] 5.4 在 `server_unified.py` 的 `_handle_tool` 中添加 `ue_async_run` 处理分支：submit 发送 `async_execute`，poll 发送 `get_task_result`
- [x] 5.5 更新 `server_unified.py` 顶部文档字符串，工具数量从 8 更新为 10

## 6. Python — 删除冗余 ActionDef & 新增 python.exec

- [x] 6.1 从 `registry/actions.py` 中删除 `_EDITOR_ACTIONS` 中的 22 个 editor 域 ActionDef
- [x] 6.2 从 `registry/actions.py` 中删除 `_BLUEPRINT_ACTIONS` 中的 9 个 blueprint 域 ActionDef（create, compile, set_property, spawn_actor, set_parent_class, add/remove_interface, add_component, create_colored_material）
- [x] 6.3 从 `registry/actions.py` 中删除整个 `_COMPONENT_ACTIONS` 列表（3 个）
- [x] 6.4 从 `registry/actions.py` 中删除 `_MATERIAL_ACTIONS` 中的 10 个基础操作 ActionDef
- [x] 6.5 在 `registry/actions.py` 中新增 `_PYTHON_ACTIONS` 列表，包含 `python.exec` ActionDef（command=exec_python）
- [x] 6.6 在 `register_all_actions()` 中调用 `registry.register_many(_PYTHON_ACTIONS)`，移除已删除列表的注册
- [x] 6.7 更新文件顶部文档字符串中的 action 数量

## 7. Python — Skills 系统更新

- [x] 7.1 更新 `skills/__init__.py` 中 `blueprint-core` SkillDef：移除 create/compile/set_property/spawn_actor/set_parent_class/add_interface/remove_interface/add_component/create_colored_material，保留 get_summary/describe_full；移除 component.set_property/set_static_mesh/set_physics
- [x] 7.2 更新 `skills/__init__.py` 中 `editor-level` SkillDef：移除所有已删除的 editor.* action_id，保留 diff_against_depot/get_asset_history/get_logs/is_ready/request_shutdown/get_selected_asset_thumbnail/clear_logs/assert_log
- [x] 7.3 更新 `skills/__init__.py` 中 `materials` SkillDef：移除 create/add_expression/connect_expressions/connect_to_output/set_expression_property/compile/create_instance/set_property/create_post_process_volume/apply_to_component/apply_to_actor，保留 get_summary/remove_expression/auto_layout/auto_comment/refresh_editor/get_selected_nodes 及全部分析/诊断 action
- [x] 7.4 在 `skills/__init__.py` 中新增 `python-api` SkillDef：id="python-api"，包含 "python.exec" action_id，workflows_file="python-api.md"
- [x] 7.5 创建 `skills/python-api.md`：包含 Unreal Python API 教学、`_result` 约定、常用代码片段（Actor 操作、材质操作、蓝图创建/编译、资产管理、视口控制、PIE 控制等）、从旧 Action 到 Python 的迁移对照表
- [x] 7.6 更新 `skills/blueprint-core.md`：将 create/compile/add_component 示例改为 `ue_python_exec` 等价代码
- [x] 7.7 更新 `skills/editor-level.md`：将 Actor 管理、PIE 控制、Outliner 管理示例改为 `ue_python_exec` 等价代码
- [x] 7.8 更新 `skills/materials.md`：将材质创建管线示例改为 `ue_python_exec` + 保留的分析 action 混合工作流

## 8. Tests 更新

- [x] 8.1 更新 `tests/test_skills.py`：适配新增/修改的 SkillDef（python-api 新增，其他三个 action_ids 变更）
- [x] 8.2 更新 `tests/test_schema_contract.py`：将已删除 C++ action 从检查范围中排除（或更新基线）；新增 exec_python 的契约检查

## 9. 文档更新

- [ ] 9.1 重写 `README.md`：更新工具数量（10 个）、action 数量（~95 个）、新增 ue_python_exec / ue_async_run / Commandlet 模式说明、架构图更新
- [ ] 9.2 更新 `docs/architecture.md`：新增 Python exec 路径和异步路径到架构图、新增 Commandlet 模式段落、更新关键文件表
- [ ] 9.3 更新 `docs/actions.md`：移除已删除 action 条目，新增 python.exec，更新域统计数量
- [ ] 9.4 更新 `docs/development.md`：新增 "使用 ue_python_exec 代替 C++ Action" 开发指南、新增 Commandlet 使用说明

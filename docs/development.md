# 开发指南

## 新增动作

### 第 1 步：C++ 实现

```cpp
// Source/UEEditorMCP/Public/Actions/MyActions.h
class FMyNewAction : public FBlueprintNodeAction
{
public:
    virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
    virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
    virtual FString GetActionName() const override { return TEXT("my_new_action"); }
};
```

在 `MCPBridge.cpp` 的 `RegisterActions()` 中注册：
```cpp
ActionHandlers.Add(TEXT("my_new_action"), MakeShared<FMyNewAction>());
```

### 第 2 步：Python ActionDef

在 `Python/ue_editor_mcp/registry/actions.py` 中添加：
```python
ActionDef(
    id="domain.my_new_action",
    command="my_new_action",
    tags=("domain", "keyword"),
    description="动作说明",
    input_schema={
        "type": "object",
        "properties": {
            "param1": {"type": "string", "description": "..."},
        },
        "required": ["param1"],
    },
    examples=({"param1": "value"},),
)
```

在 `register_all_actions()` 中调用 `registry.register_many(_MY_ACTIONS)`。

### 第 3 步：编译验证

```
Engine\Build\BatchFiles\Build.bat <ProjectName>Editor Win64 Development ...
```

无需修改服务器代码，`ue_actions_search` / `ue_actions_run` 自动识别新动作。

---

## 使用 ue_python_exec 代替 C++ Action

在新增编辑器操作时，**优先使用 `ue_python_exec`**，仅在 Python API 无法实现时才编写 C++ Action。

### 适合 ue_python_exec 的场景

- Actor 增删改查、属性设置
- 蓝图创建、编译、设置属性
- 材质创建、连接表达式、编译
- 视口控制、PIE 启停
- 资产管理（列出、重命名、保存）
- 任何 `unreal.*` 模块能直接完成的操作

### 仍需 C++ Action 的场景

- 蓝图图节点操作（`node.*`、`graph.*`）— 需要 `UEdGraph` / `UK2Node` 等非 Python 暴露的类
- AnimGraph 操作 — `UAnimGraphNode_*` 未暴露 Python API
- UMG Widget 蓝图操作 — `UWidgetBlueprint` 编辑器 API 未暴露
- 材质分析/诊断 — 需要遍历 `UMaterialExpression` 内部图结构
- 高性能批量操作 — C++ 直接操作避免 Python 解释器开销

### 迁移示例

```python
# 旧方式：C++ get_actors Action
# ue_actions_run(action_id="editor.get_actors", params={})

# 新方式：ue_python_exec
ue_python_exec(code="""
import unreal
actors = unreal.EditorLevelLibrary.get_all_level_actors()
_result = [{"name": a.get_name(), "class": a.get_class().get_name()} for a in actors]
""")
```

```python
# 旧方式：C++ create_blueprint Action
# ue_actions_run(action_id="blueprint.create", params={"name": "BP_Test", "parent_class": "Actor"})

# 新方式：ue_python_exec
ue_python_exec(code="""
import unreal
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class", unreal.Actor)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
bp = asset_tools.create_asset("BP_Test", "/Game/Blueprints", unreal.Blueprint, factory)
_result = {"path": bp.get_path_name()} if bp else {"error": "Failed to create blueprint"}
""")
```

详细的 Python API 用法和迁移对照表见 [python-api skill](../Python/ue_editor_mcp/skills/python-api.md)。

---

## Commandlet 模式（CLI/CI）

`UEEditorMCPCommandlet` 允许在无窗口模式下直接执行命令，适用于 CI/CD 管线和自动化脚本。

### 基本用法

```bash
# 单条命令执行
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -command=exec_python -params="{\"code\":\"import unreal; _result=unreal.SystemLibrary.get_engine_version()\"}" -json

# JSON 格式输出（便于脚本解析）
# 输出格式：JSON_BEGIN\n{...}\nJSON_END
```

### 批量执行

```bash
# 从 JSON 文件读取命令列表并顺序执行
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -batch -file=commands.json -json
```

`commands.json` 格式：
```json
[
  {"command": "exec_python", "params": {"code": "import unreal; _result = 'hello'"}},
  {"command": "graph.describe", "params": {"blueprint_name": "BP_Test"}}
]
```

### 帮助和 Schema 导出

```bash
# 列出所有可用命令
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -help

# 导出所有命令的 JSON Schema
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -format=json

# 导出为 Markdown 格式
UnrealEditor-Cmd.exe YourProject.uproject -run=UEEditorMCP -format=markdown
```

### 注意事项

- Commandlet 复用 `MCPBridge` 的 `ActionHandlers` 和 `ExecuteCommandSafe`，行为与 MCP 调用完全一致
- 支持 `exec_python`，可在 CI 中执行任意 Python 脚本
- 输出带 `-json` 标志时包裹在 `JSON_BEGIN` / `JSON_END` 标记中，便于外部脚本解析
- Commandlet 继承编辑器的 SEH + C++ 异常保护

---

## 编辑器快捷键

- 蓝图编辑器 `Auto Layout`：`Ctrl+Alt+L`（有选区布局选区，无选区布局整图）
- 材质编辑器 `Auto Layout`：`Edit` 菜单中的 `Auto Layout` 菜单项

---

## 测试

```bash
cd Plugins/UEEditorMCP
python -m pytest tests/ -v
```

当前测试覆盖：
- `test_animgraph.py` — AnimGraph ActionDef 结构、capabilities、JSON round-trip
- `test_skills.py` — Skill 系统完整性
- `test_schema_contract.py` — Python ↔ C++ 接口一致性
- `test_unreal_logs_server.py` — 日志服务器基础功能
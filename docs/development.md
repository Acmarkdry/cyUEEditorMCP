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

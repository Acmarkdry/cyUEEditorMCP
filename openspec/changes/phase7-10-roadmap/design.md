## Context

UEEditorMCP 当前架构：

```
VS Code / MCP Client ──(stdio)──▶ server_unified.py (11 tools)
                                       │
                                       ├── ContextStore (.context/ 持久化)
                                       ├── ActionRegistry (~95 ActionDef)
                                       │
                                       │ TCP/JSON (port 55558, 长度前缀帧)
                                       ▼
                                  C++ Plugin (FMCPServer → FMCPClientHandler)
                                       │
                                       ├── 快速路径 (ping/close/async_execute/get_task_result)
                                       ├── GameThread 分发 → FEditorAction 子类 (~95个)
                                       └── FExecPythonAction → IPythonScriptPlugin → unreal.*
```

Phase 1-6 已全部完成。本设计覆盖 Phase 7-10 四个阶段的技术方案。

## Goals / Non-Goals

**Goals:**
- Phase 7: AI 操作可 Ctrl+Z 撤销；AI 可看到编辑器视口画面；错误响应含修复建议
- Phase 8: AI 可操作 Niagara 粒子、DataTable 数据表、Sequencer 过场动画
- Phase 9: C++ 能主动推送事件到 AI；危险操作需确认；批量操作可原子回滚
- Phase 10: AI 可跑自动化测试、管理子关卡、查看性能数据

**Non-Goals:**
- 不做完整的 UE C++ 代码生成（无法在运行时编译）
- 不做 3D 视口内的实时拖拽交互（MCP 是异步协议）
- 不做多用户协同编辑（单编辑器实例假设不变）
- 不替换现有 `ue_python_exec` 的通用能力（新 Action 只覆盖 Python API 难以精确操作的场景）
- 不重构现有架构（纯增量扩展）

## Decisions

### D1: Undo 事务方案 — FScopedTransaction 基类注入

**选择**：在 `FEditorAction::Execute()` 中为所有 capabilities 标记为 `write` 的 Action 自动包裹 `FScopedTransaction`。

```cpp
TSharedPtr<FJsonObject> FEditorAction::Execute(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
    // 只有写操作才创建事务
    if (bIsWriteAction)
    {
        FScopedTransaction Transaction(
            FText::FromString(FString::Printf(TEXT("MCP: %s"), *CommandType)));
        return ExecuteWithCrashProtection(Params, Context);
    }
    return ExecuteWithCrashProtection(Params, Context);
}
```

**Undo/Redo 暴露为 Action**（不增加 MCP 工具数）：
```
editor.undo → GEditor->UndoTransaction()
editor.redo → GEditor->RedoTransaction()
editor.get_undo_history → 返回最近 N 条可撤销事务的描述
```

**备选方案**：
- 每个 Action 自己决定是否创建事务 → 太分散，容易遗漏
- 独立的 `ue_undo` MCP 工具 → 增加工具数，不符合固定工具数哲学

**理由**：基类统一注入保证 100% 覆盖，开发者新增 Action 时零额外工作。

### D2: 视口截图方案 — FViewport::ReadPixels + Base64

**选择**：新增 `FTakeScreenshotAction`，通过活跃视口的 `FViewport::ReadPixels()` 捕获画面。

```cpp
// 核心流程
FViewport* Viewport = GEditor->GetActiveViewport();
TArray<FColor> Bitmap;
Viewport->ReadPixels(Bitmap);
// → 缩放到请求尺寸（默认 512x512，最大 1024）
// → IImageWrapper 压缩为 PNG
// → FBase64::Encode 转为 base64 字符串
// → 返回 {"image": "data:image/png;base64,...", "width": 512, "height": 512}
```

**输入参数**：
- `width` / `height`（默认 512，最大 1024 — 平衡质量与传输大小）
- `viewport`（`"active"` / `"perspective"` / `"top"` / `"front"` / `"right"`）

**备选方案**：
- `HighResScreenshot` 模式 → 过重，用于最终品质截图
- `SceneCaptureComponent2D` → 需要在场景中创建 Actor，太侵入

**理由**：`ReadPixels()` 是最轻量的截图方式，不修改场景，不产生副作用。

### D3: 智能错误建议方案 — 错误响应格式扩展

**选择**：在 `FEditorAction::CreateErrorResponse()` 中增加可选的 `suggestions` 和 `context` 字段。

```json
{
  "success": false,
  "error": "[validation] Pin 'execute' not found on node 'K2Node_MathExpression'",
  "error_type": "validation",
  "suggestions": [
    "This node is a pure expression node with no exec pins.",
    "Available pins: A, B, ReturnValue",
    "Use the output pin 'ReturnValue' to connect to a data input."
  ],
  "available_pins": [
    {"name": "A", "direction": "input", "category": "real"},
    {"name": "B", "direction": "input", "category": "real"},
    {"name": "ReturnValue", "direction": "output", "category": "real"}
  ]
}
```

**实现策略**：
- 不是所有 Action 都立即添加建议 — 优先覆盖**高频失败**的场景：
  1. Pin 未找到 → 返回可用 Pin 列表
  2. 节点类型未找到 → 返回相似类型
  3. 资产路径无效 → 返回搜索建议
  4. 参数校验失败 → 返回参数 schema 摘要
- 在 `FBlueprintNodeAction` 基类中提供 helper：`CollectAvailablePins(Node)` → `suggestions`

**理由**：渐进式添加，不阻塞其他功能开发。高频失败路径优先，ROI 最高。

### D4: Niagara 操作方案 — NiagaraEditor API

**选择**：通过 `NiagaraEditor` 模块 API 操作 Niagara 资产，而非尝试直接修改 runtime 数据。

**Action 设计**（共 ~10 个）：

| Action ID | C++ 命令 | 核心 API |
|-----------|---------|---------|
| `niagara.create_system` | `create_niagara_system` | `FAssetToolsModule::CreateAsset<UNiagaraSystem>` |
| `niagara.add_emitter` | `add_niagara_emitter` | `UNiagaraSystem::AddEmitterHandle()` |
| `niagara.describe_system` | `describe_niagara_system` | 遍历 `EmitterHandles[]` + `SystemSpawnScript` |
| `niagara.set_module_parameter` | `set_niagara_module_param` | `UNiagaraStackEntry::SetValueByPinName()` 或反射 |
| `niagara.add_module` | `add_niagara_module` | `UNiagaraNodeFunctionCall` + Stack API |
| `niagara.remove_emitter` | `remove_niagara_emitter` | `UNiagaraSystem::RemoveEmitterHandle()` |
| `niagara.set_emitter_property` | `set_niagara_emitter_prop` | 反射设置 Emitter 属性 |
| `niagara.add_renderer` | `add_niagara_renderer` | `AddRenderer<UNiagaraSpriteRendererProperties>()` 等 |
| `niagara.compile` | `compile_niagara_system` | `UNiagaraSystem::RequestCompile()` |
| `niagara.get_available_modules` | `get_niagara_modules` | 枚举可用模块列表 |

**Build.cs 依赖**：`"Niagara"`, `"NiagaraEditor"`, `"NiagaraCore"`

**风险**：Niagara Editor API 较复杂且版本间变化大。优先实现只读的 `describe_system` 和简单的 `create_system` + `add_emitter`。

### D5: DataTable 操作方案

**Action 设计**（共 ~6 个）：

| Action ID | 核心 API |
|-----------|---------|
| `datatable.create` | `FAssetToolsModule::CreateAsset<UDataTable>` + `RowStruct` 指定 |
| `datatable.add_row` | `UDataTable::AddRow()` + 反射填充字段 |
| `datatable.set_cell` | 反射定位 Row + Field，设置值 |
| `datatable.delete_row` | `UDataTable::RemoveRow()` |
| `datatable.describe` | 遍历 `RowMap` 返回结构信息 |
| `datatable.export_json` | `UDataTable::GetTableAsJSON()` |

**挑战**：`AddRow()` 需要知道行结构体（`UScriptStruct`），且字段设置依赖反射。通过 `FProperty` 遍历 + 类型分发（Int/Float/String/Enum/Object）实现通用设置。

### D6: Sequencer 操作方案

**Action 设计**（共 ~8 个）：

| Action ID | 核心 API |
|-----------|---------|
| `sequencer.create_level_sequence` | `FAssetToolsModule::CreateAsset<ULevelSequence>` |
| `sequencer.add_possessable` | `UMovieScene::AddPossessable()` + Actor 绑定 |
| `sequencer.add_track` | `UMovieScene::AddTrack<UMovieScene3DTransformTrack>()` 等 |
| `sequencer.add_keyframe` | `MovieSceneSection::AddKey()` / Channel API |
| `sequencer.describe` | 遍历 Tracks/Sections/Channels 返回结构 |
| `sequencer.set_range` | `UMovieScene::SetPlaybackRange()` |
| `sequencer.add_camera_cut` | 添加 CameraCut Track + 绑定 |
| `sequencer.play_preview` | `ISequencer::SetPlaybackStatus(Playing)` |

**Build.cs 依赖**：`"LevelSequence"`, `"MovieScene"`, `"MovieSceneTracks"`, `"LevelSequenceEditor"`

### D7: 事件推送方案 — TCP 协议扩展

**选择**：在现有 TCP 长度前缀帧协议中增加 `notification` 消息类型。

**协议扩展**：
```json
// C++ → Python 主动推送
{
  "type": "notification",
  "event": "blueprint_compiled",
  "data": {
    "blueprint_name": "BP_Player",
    "success": true,
    "error_count": 0
  },
  "timestamp": "2026-04-05T14:30:00Z"
}
```

**C++ 侧**：新增 `FMCPNotificationManager` 类，注册 UE Delegate：
- `FEditorDelegates::OnBlueprintCompiled` → 推送蓝图编译结果
- `FEditorDelegates::BeginPIE` / `EndPIE` → 推送 PIE 状态变化
- `FEditorDelegates::OnAssetsDeleted` → 推送资产删除事件
- 通过 `FMCPClientHandler` 的 socket 直接发送 notification 帧

**Python 侧**：`PersistentUnrealConnection` 增加后台接收线程，将 notification 放入队列，`server_unified.py` 可查询或在响应中附带。

**MCP 限制**：MCP 协议本身是请求-响应的，无法真正"推送"给 AI。解决方案：
1. 内部缓冲 notification 队列
2. AI 下次调用任何工具时，在响应中附带 `pending_notifications[]`
3. 或 AI 主动调用 `ue_context(action="notifications")` 查询

### D8: Action 权限分级方案

**选择**：在 `ActionDef` 已有的 `risk` 字段基础上，在 Python 侧 `_execute_action` 中增加检查。

```python
def _execute_action(action_id, params):
    action = registry.get(action_id)
    if action.risk == "destructive":
        if _permission_policy == "confirm_destructive":
            return {"success": False, "error": "CONFIRMATION_REQUIRED",
                    "action_id": action_id,
                    "risk": "destructive",
                    "description": action.description,
                    "confirm_hint": "Re-call with confirm=true to proceed"}
    # ... 正常执行
```

**权限策略配置**：通过 `ue_config` 工具或启动参数配置。

### D9: 批量原子回滚方案

**选择**：在 C++ `FBatchExecuteAction` 中增加可选 `transactional` 模式。

```cpp
if (bTransactional)
{
    // 在批量开始前记录 Undo Scope
    GEditor->BeginTransaction(TEXT("MCP Batch"));
    // ... 执行所有命令 ...
    if (bAnyFailed)
    {
        GEditor->UndoTransaction();  // 回滚全部
    }
    else
    {
        GEditor->EndTransaction();   // 提交
    }
}
```

## Risks / Trade-offs

| Risk | Phase | Severity | Mitigation |
|------|-------|----------|------------|
| FScopedTransaction 对 Python exec 操作无效（Python API 不一定走 UE 事务） | 7 | Medium | 文档明确标注 `ue_python_exec` 的操作不在事务保护范围内 |
| 视口截图在无头模式（Commandlet）下无法工作 | 7 | Low | `FTakeScreenshotAction::Validate` 检查 `GIsEditor && !IsRunningCommandlet()` |
| Niagara Editor API 在 UE5.6+ 版本间可能有破坏性变更 | 8 | High | 用 `#if ENGINE_MAJOR_VERSION` 条件编译；优先实现只读 Action |
| DataTable 行字段通过反射设置，类型覆盖可能不完整 | 8 | Medium | 先支持 Int/Float/String/Name/Bool/Enum，复杂类型标记 unsupported |
| 事件推送改变 TCP 协议，心跳线程可能收到 notification 帧 | 9 | High | notification 帧使用不同的 type 标识，接收端按 type 分流 |
| 批量原子回滚依赖 UE 事务系统，部分操作（文件创建）不可回滚 | 9 | Medium | 文档明确"原子回滚仅覆盖编辑器内操作，不含资产创建/删除" |
| Phase 10 的自动化测试需要 PIE 运行环境，与编辑器操作可能冲突 | 10 | Medium | 测试运行时锁定其他操作 |

## Architecture Overview

### Phase 7-10 架构演进

```
VS Code / MCP Client ──(stdio)──▶ server_unified.py (11-12 tools)
                                       │
                                       ├── ContextStore (.context/ 持久化)
                                       ├── ActionRegistry (~130+ ActionDef)  ← Phase 8: +24 actions
                                       ├── NotificationQueue              ← Phase 9: 事件缓冲
                                       ├── PermissionPolicy               ← Phase 9: 权限检查
                                       │
                                       │ TCP/JSON (port 55558)
                                       │ ┌─ 请求-响应帧（现有）
                                       │ └─ 通知帧（Phase 9 新增）
                                       ▼
                                  C++ Plugin
                                       │
                                       ├── FMCPServer + FMCPClientHandler
                                       │     └── Phase 9: 双向帧分发
                                       │
                                       ├── FMCPNotificationManager (Phase 9)
                                       │     ├── FEditorDelegates::OnBlueprintCompiled
                                       │     ├── FEditorDelegates::BeginPIE / EndPIE
                                       │     └── FEditorDelegates::OnAssetsDeleted
                                       │
                                       ├── FEditorAction (基类)
                                       │     ├── Phase 7: +FScopedTransaction
                                       │     └── Phase 7: +CreateErrorResponseWithSuggestions
                                       │
                                       ├── 现有 Actions (~95个)
                                       │     └── Phase 7: 高频失败路径添加 suggestions
                                       │
                                       └── 新增 Actions
                                             ├── Phase 7: Undo/Redo/Screenshot (3个)
                                             ├── Phase 8: Niagara (10个) + DataTable (6个) + Sequencer (8个)
                                             ├── Phase 9: Permission/Config (2个)
                                             └── Phase 10: Testing (3个) + Level (4个) + Profiler (3个)
```

### Action 数量预估

| Phase | 新增 Action 数 | 累计 |
|-------|---------------|------|
| 现有 | — | ~95 |
| Phase 7 | +6 (undo, redo, undo_history, screenshot, screenshot_pie, config) | ~101 |
| Phase 8 | +24 (niagara 10 + datatable 6 + sequencer 8) | ~125 |
| Phase 9 | +3 (get_notifications, confirm_action, set_permission) | ~128 |
| Phase 10 | +10 (testing 3 + level 4 + profiler 3) | ~138 |

## Phase 7 — 安全性与交互质量（短期，高优先级）✅ 完成

> 详见 `openspec/changes/phase7-safety-and-ux/tasks.md`  
> 11/11 tasks completed

### P7.1 Undo 事务支持 — 基类注入 FScopedTransaction

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **P7.1.1** | FEditorAction 增加 `bIsWriteAction` 标记 | `Source/Public/Actions/EditorAction.h` | 在 `FEditorAction` 基类中新增 `bool bIsWriteAction = false` 成员变量。派生类在构造函数中设置。默认 false（只读安全）。 | ☐ |
| **P7.1.2** | Execute 中自动包裹 FScopedTransaction | `Source/Private/Actions/EditorAction.cpp` | 修改 `FEditorAction::Execute()`：当 `bIsWriteAction == true` 时，在调用 `ExecuteWithCrashProtection()` 外层包裹 `FScopedTransaction`，事务描述为 `"MCP: <CommandType>"`。 | ☐ |
| **P7.1.3** | 标记所有现有写 Action | `Source/Private/Actions/*.cpp` | 遍历所有 Action 子类，在构造函数中设置 `bIsWriteAction = true`。覆盖范围：所有 Create/Add/Set/Delete/Connect/Disconnect/Compile/Collapse/Rename/Move 类 Action。只读 Action（Describe/Get/List/Find）保持 false。 | ☐ |
| **P7.1.4** | `FUndoAction` — editor.undo | `Source/Private/Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 新增只读 Action，调用 `GEditor->UndoTransaction()`。返回 `{undone: true, description: "MCP: create_blueprint"}`。检查 `GEditor->Trans->CanUndo()` 防空。 | ☐ |
| **P7.1.5** | `FRedoAction` — editor.redo | `Source/Private/Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 新增只读 Action，调用 `GEditor->RedoTransaction()`。返回 `{redone: true, description: "..."}`。 | ☐ |
| **P7.1.6** | `FGetUndoHistoryAction` — editor.get_undo_history | `Source/Private/Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 只读 Action，遍历 `GEditor->Trans->UndoBuffer` 最近 N 条事务，返回 `{entries: [{index, description, timestamp}]}`。输入参数 `limit`（默认 20）。 | ☐ |
| **P7.1.7** | Python ActionDef 注册 | `Python/ue_editor_mcp/registry/actions.py` | 新增 `editor.undo`、`editor.redo`、`editor.get_undo_history` 三条 ActionDef，含 input_schema 和 examples。 | ☐ |
| **P7.1.8** | Skill 文档更新 | `Python/ue_editor_mcp/skills/editor-level.md` | 更新 editor-level skill，加入 Undo/Redo 工作流说明和最佳实践。 | ☐ |

### P7.2 视口截图

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **P7.2.1** | `FTakeViewportScreenshotAction` | `Source/Private/Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 新增 `editor.take_screenshot` Action。核心流程：`GEditor->GetActiveViewport()->ReadPixels()` → 缩放到目标尺寸 → IImageWrapper 压缩为 PNG → FBase64::Encode。输入：`width`（默认 512，最大 1024）、`height`、`viewport`（active/perspective/top/front/right）。输出：`{image: "base64...", width, height, format: "png"}`。Validate 检查 `!IsRunningCommandlet()`。 | ☐ |
| **P7.2.2** | `FTakePIEScreenshotAction` | `Source/Private/Actions/EditorActions.h/.cpp`, `MCPBridge.cpp` | 新增 `editor.take_pie_screenshot` Action。捕获 PIE 运行时画面（`GEditor->PlayWorld` 的视口）。仅在 PIE Running 时可用。 | ☐ |
| **P7.2.3** | Build.cs 增加 ImageWrapper 依赖 | `Source/UEEditorMCP/UEEditorMCP.Build.cs` | 新增 `"ImageWrapper"` 模块依赖（PNG 压缩需要）。 | ☐ |
| **P7.2.4** | Python ActionDef 注册 | `Python/ue_editor_mcp/registry/actions.py` | 新增 `editor.take_screenshot`、`editor.take_pie_screenshot` ActionDef。 | ☐ |
| **P7.2.5** | server_unified.py ImageContent 支持 | `Python/ue_editor_mcp/server_unified.py` | 在 `_handle_tool` 的 `ue_actions_run` 路径中检测返回值含 `image` 字段时，使用 `ImageContent` 类型返回而非 `TextContent`。 | ☐ |

### P7.3 智能错误建议

| # | 任务 | 文件 | 说明 | 状态 |
|---|------|------|------|------|
| **P7.3.1** | CreateErrorResponseWithSuggestions helper | `Source/Private/Actions/EditorAction.cpp` | 在 `FEditorAction` 基类新增 `CreateErrorResponseWithSuggestions(ErrorMsg, ErrorType, Suggestions, ContextData)` 方法，生成含 `suggestions[]` 和可选上下文数据的错误响应。 | ☐ |
| **P7.3.2** | CollectAvailablePins helper | `Source/Private/MCPCommonUtils.cpp` | 新增 `FMCPCommonUtils::CollectAvailablePins(Node)` → 返回 JSON 数组 `[{name, direction, category, is_connected}]`。 | ☐ |
| **P7.3.3** | Pin 未找到 → 返回可用 Pin 列表 | `Source/Private/Actions/NodeActions.cpp`, `GraphActions.cpp` | 在所有 `FindPin` 失败的路径中，调用 `CollectAvailablePins` 并附加到错误建议。覆盖：connect_nodes、set_pin_default、disconnect_pin、patch connect/disconnect。 | ☐ |
| **P7.3.4** | 节点类型未找到 → 返回相似类型 | `Source/Private/Actions/NodeActions.cpp` | `add_function_call` 找不到函数时，搜索类似名称的函数并返回建议。使用 `FName::Find` + `EditDistance` 匹配。 | ☐ |
| **P7.3.5** | 资产路径无效 → 返回搜索建议 | `Source/Private/MCPCommonUtils.cpp` | `FindBlueprint`/`FindMaterial` 失败时，使用 `IAssetRegistry::GetAssetsByPath` 搜索同名前缀资产，返回最多 5 个建议路径。 | ☐ |
| **P7.3.6** | 文档更新 | `Python/ue_editor_mcp/resources/error_codes.md` | 更新错误码文档，说明新的 `suggestions` 字段和 AI 应如何利用它。 | ☐ |

---

## Phase 8 — 领域覆盖扩展（中期）✅ 完成

> **目标**：Niagara 粒子、DataTable 数据表、Sequencer 过场动画  
> **预计 Action 新增**：+24  
> **新增 C++ 文件**：3 个 Action 文件 + 3 个 Header

### P8.1 Niagara 粒子系统

| # | Task | Status |
|---|------|--------|
| P8.1.1 | NiagaraActions.h — 7 个 Action 类声明 | ✅ |
| P8.1.2 | NiagaraActions.cpp — 7 个 Action 完整实现 + FindNiagaraSystem helper | ✅ |
| P8.1.3 | MCPBridge.cpp — 注册 7 个 Niagara Action | ✅ |
| P8.1.4 | Build.cs — Niagara/NiagaraEditor/NiagaraCore 依赖 | ✅ |
| P8.1.5 | Python ActionDef — 7 个 niagara.* ActionDef | ✅ |

### P8.2 DataTable 操作

| # | Task | Status |
|---|------|--------|
| P8.2.1 | DataTableActions.h — 5 个 Action 类声明 | ✅ |
| P8.2.2 | DataTableActions.cpp — 5 个 Action 完整实现 + FindDataTable helper | ✅ |
| P8.2.3 | MCPBridge.cpp — 注册 5 个 DataTable Action | ✅ |
| P8.2.4 | Build.cs — DataTableEditor 依赖 | ✅ |
| P8.2.5 | Python ActionDef — 5 个 datatable.* ActionDef | ✅ |

### P8.3 Sequencer 基础

| # | Task | Status |
|---|------|--------|
| P8.3.1 | SequencerActions.h — 5 个 Action 类声明 | ✅ |
| P8.3.2 | SequencerActions.cpp — 5 个 Action 完整实现 + FindLevelSequence helper | ✅ |
| P8.3.3 | MCPBridge.cpp — 注册 5 个 Sequencer Action | ✅ |
| P8.3.4 | Build.cs — LevelSequence/MovieScene/MovieSceneTracks 依赖 | ✅ |
| P8.3.5 | Python ActionDef — 5 个 sequencer.* ActionDef | ✅ |

---

## Phase 9 — 架构升级（中期）✅ 完成

> **目标**：事件推送、权限分级、批量原子回滚  
> **预计 Action 新增**：+3  
> **架构变更**：TCP 协议扩展、Python 连接层改造

### P9.1 事件推送机制
> 推迟到后续迭代（需要改 TCP 协议，影响面大）

### P9.2 Action 权限分级

| # | Task | Status |
|---|------|--------|
| P9.2.1 | permissions.py — PermissionPolicy 类（auto/confirm_destructive/readonly） | ✅ |
| P9.2.2 | server_unified.py — 导入 + 全局实例 + _execute_action 权限检查钩子 | ✅ |

### P9.3 批量原子回滚
> 依赖 Phase 7 的 FScopedTransaction，已具备基础。后续可在 C++ batch_execute 中加 transactional 参数。

---

## Phase 10 — 深度能力（长期）✅ 完成

> **目标**：自动化测试、关卡设计增强、性能分析  
> **预计 Action 新增**：+10  
> **新增 C++ 文件**：3 个 Action 文件

### P10.1 自动化测试

| # | Task | Status |
|---|------|--------|
| P10.1.1 | FRunAutomationTestAction + FListAutomationTestsAction | ✅ |
| P10.1.2 | MCPBridge 注册 | ✅ |
| P10.1.3 | Python ActionDef — test.run, test.list | ✅ |

### P10.2 关卡设计增强

| # | Task | Status |
|---|------|--------|
| P10.2.1 | FListSublevelsAction + FGetWorldSettingsAction | ✅ |
| P10.2.2 | MCPBridge 注册 | ✅ |
| P10.2.3 | Python ActionDef — level.list_sublevels, level.get_world_settings | ✅ |

### P10.3 性能分析

| # | Task | Status |
|---|------|--------|
| P10.3.1 | FGetFrameStatsAction + FGetMemoryStatsAction | ✅ |
| P10.3.2 | MCPBridge 注册 | ✅ |
| P10.3.3 | Python ActionDef — profiler.frame_stats, profiler.memory_stats | ✅ |

---

## 总计

| Phase | 新增 C++ Action | 新增 Python ActionDef | 新增文件 | 状态 |
|-------|-----------------|----------------------|----------|------|
| 7 | 5 | 5 | 0 | ✅ |
| 8 | 17 | 17 | 6 (.h/.cpp) | ✅ |
| 9 | 0 | 0 | 1 (.py) | ✅ |
| 10 | 6 | 6 | 2 (.h/.cpp) | ✅ |
| **Total** | **28** | **28** | **9** | **✅** |

Action 总数：~95 (原有) + 28 (新增) = **~123 个 Action**  
MCP 工具数不变：**11 个**

---

## 实施路线总结

```
Phase 7 (短期):
  P7.1 Undo事务 ─────→ P7.2 视口截图 ─────→ P7.3 智能错误建议
  [8 tasks]            [5 tasks]              [6 tasks]

Phase 8 (中期):
  P8.1 Niagara ────→ P8.2 DataTable ────→ P8.3 Sequencer
  [10 tasks]         [6 tasks]            [9 tasks]

Phase 9 (中期):
  P9.1 事件推送 ────→ P9.2 权限分级 ────→ P9.3 原子回滚
  [5 tasks]          [4 tasks]            [3 tasks]

Phase 10 (长期):
  P10.1 测试 ────→ P10.2 关卡 ────→ P10.3 性能
  [4 tasks]        [4 tasks]        [3 tasks]
```

**总计**：67 个任务，跨 4 个 Phase、12 个子模块
**每个 Phase 建议拆为独立 Change 实施**，按 P7 → P8 → P9 → P10 顺序推进
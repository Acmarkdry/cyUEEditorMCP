## Why

UEEditorMCP 已完成 Phase 1-6 全部里程碑，覆盖蓝图、材质、UMG、AnimGraph、增强输入、PIE、日志断言、Outliner 管理等核心域，总计 ~95 个 C++ Action + Python exec + 11 个 MCP 工具。但在实际使用中，仍存在以下核心限制：

1. **操作不可撤销** — 所有写操作都是"执行了就执行了"，没有 UE 事务包装，用户无法 `Ctrl+Z` 回滚 AI 的错误操作，这是与人类协作的最大安全隐患
2. **AI 无法"看到"编辑器** — 缺少视口截图能力，AI 只能基于结构化数据（describe/summary）推理，无法进行视觉验证
3. **错误修复靠猜** — 当前错误响应只有文本消息，不含修复建议或可用替代项，AI 需多轮试错
4. **领域覆盖盲区** — Niagara 粒子系统、Sequencer 过场动画、DataTable 数据表、关卡子世界管理完全缺失
5. **纯请求-响应** — AI 无法被动感知编辑器事件（编译完成、PIE 崩溃、资产变更），只能主动轮询
6. **批量操作无回滚** — `ue_batch` 遇到失败时已执行的操作无法回滚

本提案规划 Phase 7-10 路线图，将 UEEditorMCP 从"能操作编辑器"提升到"能像人一样高效使用编辑器"。

## What Changes

### Phase 7 — 安全性与交互质量（短期，高优先级）
- **Undo 事务支持**：所有写 Action 自动包裹 `FScopedTransaction`，新增 `ue_undo` / `ue_redo` 工具
- **视口截图**：新增 `editor.take_screenshot` C++ Action，捕获编辑器视口画面返回 base64 图片
- **智能错误建议**：错误响应中附带 `suggestions[]` 和 `available_alternatives`，减少 AI 试错

### Phase 8 — 领域覆盖扩展（中期）
- **Niagara 粒子系统**：创建/查看/修改 Niagara System、Emitter、Module 参数
- **DataTable 操作**：创建/增删改查 DataTable 行、PrimaryDataAsset 管理
- **Sequencer 基础**：创建 LevelSequence、添加轨道、插入关键帧、描述序列结构

### Phase 9 — 架构升级（中期）
- **事件推送机制**：TCP 协议增加 `notification` 消息类型，C++ 通过 UE Delegate 主动推送编译完成/PIE 事件
- **Action 权限分级**：`destructive` 级 Action 执行前要求确认；可配置权限策略
- **批量原子回滚**：`ue_batch` 支持可选事务模式（失败时 Undo 全部已执行操作）

### Phase 10 — 深度能力（长期）
- **自动化测试集成**：运行/创建 UE 自动化测试、获取测试报告
- **关卡设计增强**：子关卡管理、World Settings、地形操作
- **性能分析工具**：帧统计、GPU 绘制调用、内存概览

## Capabilities

### New Capabilities
- `undo-redo`: 操作撤销/重做 — FScopedTransaction 事务包装 + ue_undo/ue_redo 工具
- `viewport-screenshot`: 视口截图 — 捕获编辑器画面供 AI 视觉验证
- `smart-error-suggestions`: 智能错误建议 — 错误响应附带修复建议和可用替代项
- `niagara-system`: Niagara 粒子系统操作 — System/Emitter/Module/Renderer 全生命周期
- `datatable-ops`: DataTable 操作 — 创建/增删改查/导出
- `sequencer-basics`: Sequencer 基础 — LevelSequence/Track/Keyframe
- `event-notifications`: 事件推送 — C++ 主动推送编译/PIE/资产变更事件
- `action-permissions`: Action 权限分级 — destructive 操作确认机制
- `atomic-batch`: 批量原子回滚 — ue_batch 事务模式
- `automation-testing`: 自动化测试集成 — 运行/创建/报告
- `level-design`: 关卡设计增强 — 子关卡/World Settings/地形
- `perf-profiler`: 性能分析工具 — 帧/GPU/内存统计

### Modified Capabilities
- `editor-actions`: 扩展视口截图、关卡子世界管理
- `batch-execute`: 增加事务模式支持
- `editor-action-base`: 所有写 Action 增加 FScopedTransaction 包装

## Impact

### Phase 7（安全性与交互质量）
- **Source/UEEditorMCP/Private/Actions/EditorAction.cpp** — `FEditorAction::Execute` 添加 FScopedTransaction
- **Source/UEEditorMCP/Private/Actions/EditorActions.cpp** — 新增 `FTakeScreenshotAction`、`FUndoAction`、`FRedoAction`
- **Source/UEEditorMCP/Private/MCPBridge.cpp** — 注册新 Action
- **Python/ue_editor_mcp/server_unified.py** — 新增 `ue_undo` 工具或复用 `ue_actions_run`
- **Python/ue_editor_mcp/registry/actions.py** — 新增 ActionDef
- **所有现有写 Action 的错误响应格式** — 增加 `suggestions` 字段

### Phase 8（领域覆盖扩展）
- **Source/UEEditorMCP/Private/Actions/NiagaraActions.cpp** — 新文件，Niagara 全部 Action
- **Source/UEEditorMCP/Private/Actions/DataTableActions.cpp** — 新文件，DataTable Action
- **Source/UEEditorMCP/Private/Actions/SequencerActions.cpp** — 新文件，Sequencer Action
- **Source/UEEditorMCP/UEEditorMCP.Build.cs** — 新增 `Niagara`、`NiagaraEditor`、`LevelSequence`、`MovieScene` 模块依赖
- **Python/ue_editor_mcp/skills/** — 新增 niagara.md、datatable.md、sequencer.md

### Phase 9（架构升级）
- **Source/UEEditorMCP/Private/MCPServer.cpp** — TCP 协议扩展支持 notification 帧
- **Source/UEEditorMCP/Private/MCPNotificationManager.cpp** — 新文件，UE Delegate 监听 + 通知分发
- **Python/ue_editor_mcp/connection.py** — 接收端支持 notification 消息
- **Python/ue_editor_mcp/server_unified.py** — Action 权限检查钩子

### Phase 10（深度能力）
- **Source/UEEditorMCP/Private/Actions/TestingActions.cpp** — 新文件
- **Source/UEEditorMCP/Private/Actions/LevelActions.cpp** — 新文件
- **Source/UEEditorMCP/Private/Actions/ProfilerActions.cpp** — 新文件

### 工具数量变化
- Phase 7: +0（undo/redo 作为 Action 通过 `ue_actions_run` 调用）或 +1（`ue_undo` 独立工具）
- Phase 8-10: +0（新 Action 全部走 Action Registry）
- **工具总数维持 11-12 个**，符合固定工具数设计哲学

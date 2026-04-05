## Why

Phase 7 是 UEEditorMCP Phase 7-10 路线图的第一阶段，聚焦**安全性与交互质量**。解决三个最紧迫的问题：

1. **操作不可撤销** — AI 执行的所有写操作没有 UE 事务包装，用户无法 Ctrl+Z 回滚
2. **AI 无法"看到"编辑器** — 缺少视口截图能力，无法进行视觉验证
3. **错误修复靠猜** — 错误响应只有文本，不含修复建议或可用替代项

## What Changes

### P7.1 Undo 事务支持
- `FEditorAction` 基类增加 `bIsWriteAction` 标记
- `Execute()` 中为写 Action 自动包裹 `FScopedTransaction`
- 标记所有现有 ~60 个写 Action
- 新增 `editor.undo`、`editor.redo`、`editor.get_undo_history` 三个 Action

### P7.2 视口截图
- 新增 `editor.take_screenshot` Action — `FViewport::ReadPixels()` → PNG → Base64
- 新增 `editor.take_pie_screenshot` Action — PIE 运行时画面捕获
- Build.cs 增加 `ImageWrapper` 依赖
- Python 侧支持 `ImageContent` 返回

### P7.3 智能错误建议
- 基类新增 `CreateErrorResponseWithSuggestions()` helper
- 新增 `CollectAvailablePins()` 工具函数
- Pin 未找到 → 返回可用 Pin 列表
- 节点/函数未找到 → 返回相似名称建议
- 资产路径无效 → 返回 Asset Registry 搜索建议

## Capabilities

### New Capabilities
- `undo-redo`: FScopedTransaction 事务包装 + editor.undo/redo/get_undo_history
- `viewport-screenshot`: 编辑器/PIE 视口截图，返回 Base64 PNG
- `smart-error-suggestions`: 错误响应附带修复建议和可用替代项

### Modified Capabilities
- `editor-action-base`: FEditorAction 增加 bIsWriteAction + FScopedTransaction + CreateErrorResponseWithSuggestions

## Impact

- **Source/UEEditorMCP/Public/Actions/EditorAction.h** — 新增 bIsWriteAction、CreateErrorResponseWithSuggestions
- **Source/UEEditorMCP/Private/Actions/EditorAction.cpp** — Execute 事务包装、新 helper
- **Source/UEEditorMCP/Private/Actions/EditorActions.cpp** — +5 新 Action（Undo/Redo/History/Screenshot/PIEScreenshot）
- **Source/UEEditorMCP/Public/Actions/EditorActions.h** — +5 新 Action 声明
- **Source/UEEditorMCP/Private/Actions/NodeActions.cpp** — Pin/函数未找到时附加建议
- **Source/UEEditorMCP/Private/Actions/GraphActions.cpp** — Patch 操作错误建议
- **Source/UEEditorMCP/Private/MCPCommonUtils.cpp** — 新增 CollectAvailablePins
- **Source/UEEditorMCP/Public/MCPCommonUtils.h** — 新增声明
- **Source/UEEditorMCP/Private/MCPBridge.cpp** — 注册新 Action
- **Source/UEEditorMCP/UEEditorMCP.Build.cs** — +ImageWrapper 依赖
- **Source/UEEditorMCP/Private/Actions/BlueprintActions.cpp** — 标记 bIsWriteAction
- **Source/UEEditorMCP/Private/Actions/MaterialActions.cpp** — 标记 bIsWriteAction
- **Source/UEEditorMCP/Private/Actions/UMGActions.cpp** — 标记 bIsWriteAction
- **Source/UEEditorMCP/Private/Actions/AnimGraphActions.cpp** — 标记 bIsWriteAction
- **Python/ue_editor_mcp/registry/actions.py** — +5 ActionDef
- **Python/ue_editor_mcp/server_unified.py** — ImageContent 支持
- **Action 总数**：~95 → ~100（+5）
- **MCP 工具数不变**：仍为 11

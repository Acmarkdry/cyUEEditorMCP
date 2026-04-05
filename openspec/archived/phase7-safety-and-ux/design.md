## Context

当前 `FEditorAction::Execute()` 流程：Validate → ExecuteWithCrashProtection → PostValidate → AutoSave。
所有写操作直接执行，无 UE 事务包装，无撤销支持。错误响应只返回 `{success:false, error:"...", error_type:"..."}`。

## Goals / Non-Goals

**Goals:**
- 所有写 Action 自动获得 Ctrl+Z 撤销能力（零侵入，子类不需改代码）
- AI 可捕获编辑器视口/PIE 画面供视觉验证
- 高频失败路径（Pin/节点/资产未找到）返回修复建议
- 维持固定 11 个 MCP 工具数（新功能通过 Action 暴露）

**Non-Goals:**
- 不做 Python exec 操作的事务保护（Python API 不走 UE Transaction）
- 不做每个 Action 的独立错误建议（只覆盖高频路径）
- 不做视频录制（仅截图）
- 不修改 TCP 协议

## Decisions

### D1: bIsWriteAction 标记方案

在 `FEditorAction` 基类中新增 `protected bool bIsWriteAction = false`。  
写 Action 在**构造函数**中设置 `bIsWriteAction = true`。  
`Execute()` 检查此标记决定是否创建 `FScopedTransaction`。

**关键问题**：`FScopedTransaction` 必须在 `ExecuteWithCrashProtection()` 返回前保持存活（其析构函数提交事务）。所以事务在 Execute() 中创建，scope 覆盖整个执行流。

```cpp
TSharedPtr<FJsonObject> FEditorAction::Execute(...)
{
    // Validate (不在事务内，验证失败不产生事务记录)
    if (!Validate(...)) return error;
    
    // 事务包裹执行
    TSharedPtr<FJsonObject> Result;
    if (bIsWriteAction)
    {
        FScopedTransaction Transaction(FText::FromString(
            FString::Printf(TEXT("MCP: %s"), *GetActionName())));
        Result = ExecuteWithCrashProtection(Params, Context);
    }
    else
    {
        Result = ExecuteWithCrashProtection(Params, Context);
    }
    
    // PostValidate + AutoSave (在事务提交后)
    ...
}
```

### D2: 视口截图技术方案

使用 `FViewport::ReadPixels()` 捕获原始像素，然后：
1. `FImageUtils::ImageResize()` 缩放到目标尺寸
2. `IImageWrapperModule` 创建 PNG wrapper
3. `FBase64::Encode()` 编码

默认 512×512，最大 1024×1024。约 100-300KB base64 字符串。

PIE 截图通过 `GEditor->GetPIEWorldContext()->GameViewport->Viewport` 获取。

### D3: 错误建议的增量添加策略

新增 `CreateErrorResponseWithSuggestions()` 不替换现有 `CreateErrorResponse()`，而是作为新 overload。现有代码不动，逐步在高频失败路径替换调用。

优先覆盖：
1. `FindPin` 失败（connect_nodes, set_pin_default, disconnect_pin）
2. `add_function_call` 找不到函数
3. `FindBlueprint`/`FindMaterial` 找不到资产

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| FScopedTransaction 开销 | UE 原生机制，开销极小。且只有写操作触发 |
| ReadPixels 在无窗口环境失败 | Validate 检查 `GEditor->GetActiveViewport() != nullptr` |
| base64 图片太大（>1MB）| 限制最大 1024x1024，PNG 压缩后通常 <300KB |
| 错误建议增加响应体积 | suggestions 只在失败时返回，且限制最多 5 条 |

## Context

cyUEEditorMCP 当前架构为三层结构：AI 客户端 → Python MCP Server (stdio) → C++ TCP Server (端口 55558) → Game Thread → UE Editor API。159 个 C++ 手写 FEditorAction 中，约 45 个可被 Unreal 内嵌 Python API 完全替代（editor/actor/material 基础操作等），维护冗余。

参考 UECLI 项目（github.com/wlxklyh/UECLI），其优秀的异步命令、Commandlet 模式值得借鉴。但更关键的是：通过引入 `ue_python_exec` 工具让 AI 直接执行 `unreal.*` Python 代码，可以**减去**大量冗余 C++ Action。

本次改动的核心理念是**先做减法，再做加法**。

## Goals / Non-Goals

**Goals:**
- 新增 `exec_python` C++ Action + `ue_python_exec` MCP 工具，实现 AI 直接执行 Unreal Python API
- 删除 ~45 个被 Python API 完全覆盖的冗余 C++ Action
- 新增异步命令执行（async_execute / get_task_result）
- 新增 Commandlet 模式用于 CI/CD
- 全面联动更新 Skills、Tests、文档
- 保持向后兼容——保留所有 Python API 无法替代的 ~95 个 Action

**Non-Goals:**
- 不删除蓝图节点操作(node.*)、图操作(graph.*)、AnimGraph、UMG Widget、Layout 等 Python 无法替代的 Action
- 不添加 TextureGraph 领域支持（独立改动）
- 不移除 MCP 协议
- 不修改 TCP 通信帧格式

## Decisions

### Decision 1: Python 代码执行机制

**选择：C++ 端新增 `FExecPythonAction`，通过 `IPythonScriptPlugin::ExecPythonCommand()` 执行**

理由：
- UE 5.x 的 `PythonScriptPlugin` 提供了 `ExecPythonCommand()` API，在游戏线程执行 Python 代码
- 执行结果（stdout/stderr）可通过重定向 `sys.stdout` / `sys.stderr` 捕获
- 返回值通过约定变量 `_result` 传回 JSON

协议设计：
```json
// 请求
{"type": "exec_python", "params": {"code": "import unreal\nactors = unreal.EditorLevelLibrary.get_all_level_actors()\n_result = [a.get_name() for a in actors]"}}

// 响应
{"success": true, "result": {"return_value": [...], "stdout": "", "stderr": ""}}
```

执行安全：
- 代码在游戏线程通过 `AsyncTask(GameThread)` 执行，享有现有 SEH + C++ 异常保护
- 单次执行超时继承现有 240s 上限
- Python 异常被捕获为 error 响应，不影响编辑器

替代方案：使用 `FPythonCommandEx` 或直接 `PyRun_SimpleString`
- 否决原因：`IPythonScriptPlugin` 是 Epic 官方接口，更稳定；自行调 CPython API 需要额外链接和 GIL 管理

### Decision 2: ue_python_exec MCP 工具设计

**选择：独立工具，支持 code + optional context 参数**

schema 设计：
```json
{
  "name": "ue_python_exec",
  "inputSchema": {
    "properties": {
      "code": {"type": "string", "description": "Python code to execute in Unreal's embedded Python environment. Use `import unreal` to access UE API. Set `_result = ...` to return data."},
      "timeout_seconds": {"type": "integer", "description": "Max execution time (default: 30, max: 240)"}
    },
    "required": ["code"]
  }
}
```

理由：
- 独立工具避免与现有 `ue_actions_run` 的 action 分发逻辑混淆
- `_result` 约定简单明了，AI 只需在代码末尾赋值即可
- timeout 参数让 AI 可以控制长耗时 Python 脚本

### Decision 3: 哪些 Action 删除的判定标准

**标准：Unreal Python API 能在 ≤10 行代码内实现等价功能的 Action 删除**

分类结果：
| 状态 | 数量 | 说明 |
|------|------|------|
| **删除** | ~45 | editor 域(22)、blueprint 基础(9)、component(3)、material 基础(10)、batch.execute(1) |
| **保留** | ~95 | node/graph/animgraph/widget/layout/dispatcher/variable/function/macro + 材质分析/诊断 |
| **灰色保留** | ~19 | blueprint.get_summary/describe_full、editor.get_logs 等——Python 理论可做但 C++ 效率更高，暂保留 |

### Decision 4: 异步任务管理

**选择：放在 MCPBridge（UEditorSubsystem）中，在 TCP 层作为特殊命令处理**

与之前 design 中的 Decision 1/2 相同（见异步命令 spec）。
- 异步任务存储使用 `FCriticalSection` + `TMap<FString, FAsyncTaskEntry>`
- task_id 为 `FGuid::NewGuid().ToString()`
- 结果 TTL 5 分钟

### Decision 5: Commandlet 设计

**选择：`UUEEditorMCPCommandlet`，调用方式 `-run=UEEditorMCP`**

- 复用 MCPBridge 的 ActionHandlers 和 ExecuteCommandSafe
- 支持 `-command=xxx -params="{...}"` / `-help` / `-batch -file=xxx` / `-json`
- Commandlet 也可以调用 `exec_python`，实现 CI/CD 中的 Python 脚本执行

### Decision 6: Skills 和文档联动更新策略

**选择：删除的 Action 从 SkillDef.action_ids 中移除，新增 `python-api` Skill 替代**

- `blueprint-core` skill：移除 create/compile/set_property 等，保留 describe_full/get_summary
- `editor-level` skill：大幅精简，保留 diff/logs/assert_log/thumbnail 等 Python 无法做的
- `materials` skill：移除 create/add_expression/connect 等基础操作，保留分析/诊断/layout
- 新增 `python-api` skill：教 AI 如何使用 `unreal.*` API，包含常用代码片段

## Risks / Trade-offs

- **[Risk] AI 生成的 Python 代码可能有错误** → ue_python_exec 返回完整的 stderr 和异常信息，AI 可自行修正
- **[Risk] 删除 Action 后旧的 AI 对话上下文可能引用它们** → 在 python-api skill 中明确列出迁移映射
- **[Risk] PythonScriptPlugin 未启用** → 在 uplugin 中添加依赖，StartupModule 中检测可用性
- **[Trade-off] 删除 Action 降低了结构化程度** → ue_python_exec 更灵活但也更容易出错，通过 skill 文档减轻
- **[Trade-off] 工具数量从 8 增到 10** → 换来的是 action 从 159 降到 ~95，净简化
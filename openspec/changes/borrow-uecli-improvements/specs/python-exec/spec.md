## ADDED Requirements

### Requirement: Python code execution via C++ bridge
系统 SHALL 支持通过 `exec_python` 命令在 Unreal 内嵌 Python 环境中执行任意 Python 代码。代码 MUST 在游戏线程上通过 `IPythonScriptPlugin::ExecPythonCommand()` 执行，享有 SEH + C++ 异常双重保护。

#### Scenario: Execute simple Python code
- **WHEN** 客户端发送 `{"type": "exec_python", "params": {"code": "import unreal\n_result = unreal.SystemLibrary.get_engine_version()"}}`
- **THEN** 系统返回 `{"success": true, "result": {"return_value": "5.6.0-...", "stdout": "", "stderr": ""}}`

#### Scenario: Execute code with syntax error
- **WHEN** 客户端发送 `{"type": "exec_python", "params": {"code": "def foo(:\n  pass"}}`
- **THEN** 系统返回 `{"success": false, "error": "SyntaxError: ...", "stderr": "..."}`

#### Scenario: Execute code with runtime exception
- **WHEN** 客户端发送包含运行时异常的 Python 代码
- **THEN** 系统返回 `{"success": false, "error": "<exception message>", "stderr": "<traceback>"}`

### Requirement: Return value convention
系统 SHALL 使用 `_result` 约定变量来传回 Python 执行结果。如果执行完毕后 `_result` 变量存在，系统 MUST 尝试将其 JSON 序列化后放入响应的 `return_value` 字段。如果 `_result` 不存在，`return_value` SHALL 为 null。

#### Scenario: Code sets _result to a list
- **WHEN** 代码为 `import unreal\nactors = unreal.EditorLevelLibrary.get_all_level_actors()\n_result = [a.get_name() for a in actors]`
- **THEN** `return_value` 为 actor 名称的 JSON 数组

#### Scenario: Code does not set _result
- **WHEN** 代码为 `import unreal\nunreal.log("hello")`
- **THEN** `return_value` 为 null，`stdout` 可能包含输出

### Requirement: Stdout/stderr capture
系统 SHALL 捕获 Python 代码执行期间的 stdout 和 stderr 输出，通过响应中的 `stdout` 和 `stderr` 字段返回给客户端。

#### Scenario: Code prints to stdout
- **WHEN** 代码为 `print("hello world")\n_result = 42`
- **THEN** 响应包含 `"stdout": "hello world\n"` 和 `"return_value": 42`

### Requirement: MCP ue_python_exec tool
Python MCP Server SHALL 暴露 `ue_python_exec` 工具，接受 `code`（必填）和 `timeout_seconds`（可选，默认 30，最大 240）参数。工具 MUST 将代码通过 TCP 发送到 C++ 端的 `exec_python` 命令执行。

#### Scenario: AI executes Python via MCP tool
- **WHEN** AI 调用 `ue_python_exec` 工具，参数 `{"code": "import unreal\n_result = len(unreal.EditorLevelLibrary.get_all_level_actors())"}`
- **THEN** 工具返回 `{"success": true, "return_value": 42, "stdout": "", "stderr": ""}`

### Requirement: PythonScriptPlugin dependency
系统 MUST 在 `UEEditorMCP.uplugin` 中声明对 `PythonScriptPlugin` 的依赖（Enabled: true）。如果运行时 `PythonScriptPlugin` 不可用，`exec_python` 命令 SHALL 返回明确的错误信息。

#### Scenario: PythonScriptPlugin not available
- **WHEN** `PythonScriptPlugin` 未启用且客户端调用 `exec_python`
- **THEN** 系统返回 `{"success": false, "error": "PythonScriptPlugin is not available. Enable it in .uproject."}`

### Requirement: Python API skill documentation
系统 SHALL 提供 `python-api` Skill 文档（`skills/python-api.md`），包含：
- 常用 `unreal.*` API 代码片段（替代已删除 Action 的等价代码）
- Actor 操作、材质操作、蓝图创建/编译、资产管理等领域的 Python 示例
- `_result` 约定使用说明
- 从旧 Action 到 Python 代码的迁移对照表

#### Scenario: AI loads python-api skill
- **WHEN** AI 调用 `ue_skills(action="load", skill_id="python-api")`
- **THEN** 返回包含代码片段、迁移映射的完整教学文档

### Requirement: Redundant C++ Action removal
系统 SHALL 删除所有可被 Unreal Python API 在 ≤10 行代码内完全替代的 C++ Action。删除范围包括 C++ FEditorAction 子类、MCPBridge 注册、Python ActionDef、SkillDef 引用。

#### Scenario: Deleted action returns error
- **WHEN** 客户端尝试调用已删除的命令（如 `spawn_actor`）
- **THEN** 系统返回 `{"success": false, "error": "Unknown command type: spawn_actor"}`

#### Scenario: Python skill provides equivalent
- **WHEN** AI 需要在关卡中生成 Actor
- **THEN** AI 使用 `ue_python_exec` 执行 `import unreal; actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0,0,0))`

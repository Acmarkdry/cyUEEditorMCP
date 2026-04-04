## ADDED Requirements

### Requirement: Commandlet single command execution
系统 SHALL 支持通过 Unreal Engine 命令行 `-run=UEEditorMCP -command=<name> -params="{...}"` 执行单个命令。命令 MUST 复用 MCPBridge 中已注册的全部 Action 处理器。

#### Scenario: Execute a single command via commandlet
- **WHEN** 用户运行 `UnrealEditor.exe <project> -run=UEEditorMCP -command=create_blueprint -params="{\"name\":\"BP_Test\",\"parent_class\":\"Actor\"}"`
- **THEN** 系统执行 `create_blueprint` 命令并将 JSON 结果输出到 stdout，退出码为 0（成功）

#### Scenario: Execute with unknown command
- **WHEN** 用户运行 `-run=UEEditorMCP -command=nonexistent_command`
- **THEN** 系统输出错误 JSON 并返回退出码 1

### Requirement: Commandlet key-value parameter parsing
系统 SHALL 支持两种参数传递方式：JSON 字符串（`-params="{...}"`）和键值对（`-name=BP_Test -parent_class=Actor`）。键值对参数 MUST 根据 ToolSchema 中定义的参数类型自动转换（string/number/boolean）。

#### Scenario: Parameters via key-value pairs
- **WHEN** 用户运行 `-run=UEEditorMCP -command=create_blueprint -name=BP_Test -parent_class=Actor`
- **THEN** 系统将键值对组装为 `{"name": "BP_Test", "parent_class": "Actor"}` 并执行命令

#### Scenario: Parameters via JSON string
- **WHEN** 用户运行 `-run=UEEditorMCP -command=create_blueprint -params="{\"name\":\"BP_Test\"}"`
- **THEN** 系统解析 JSON 字符串作为参数

### Requirement: Commandlet help output
系统 SHALL 支持 `-help` 参数输出所有已注册命令的帮助信息，包括命令名、分类和参数描述。

#### Scenario: Display help
- **WHEN** 用户运行 `-run=UEEditorMCP -help`
- **THEN** 系统输出所有命令的名称、分类和参数列表

#### Scenario: Display help filtered by category
- **WHEN** 用户运行 `-run=UEEditorMCP -help -category=Material`
- **THEN** 系统仅输出 Material 分类下的命令

### Requirement: Commandlet batch mode
系统 SHALL 支持 `-batch -file=<path>` 从 JSON 文件批量执行命令。JSON 文件 MUST 为数组格式，每个元素包含 `command` 和可选的 `params` 字段。

#### Scenario: Batch execute from file
- **WHEN** 用户运行 `-run=UEEditorMCP -batch -file=commands.json`，文件内容为 `[{"command":"ping"},{"command":"create_blueprint","params":{"name":"BP_A"}}]`
- **THEN** 系统按顺序执行所有命令，输出包含所有结果的 JSON 数组

#### Scenario: Batch file not found
- **WHEN** 用户运行 `-run=UEEditorMCP -batch -file=nonexistent.json`
- **THEN** 系统输出错误信息并返回退出码 1

### Requirement: Commandlet JSON output mode
系统 SHALL 支持 `-json` 参数以 `JSON_BEGIN\n{...}\nJSON_END` 格式输出结果，方便外部脚本解析。

#### Scenario: JSON output format
- **WHEN** 用户运行 `-run=UEEditorMCP -command=ping -json`
- **THEN** 输出格式为 `JSON_BEGIN\n{"status":"success",...}\nJSON_END`

### Requirement: Commandlet format export
系统 SHALL 支持 `-format=json|markdown` 参数导出所有命令的 schema 信息。

#### Scenario: Export as JSON schema
- **WHEN** 用户运行 `-run=UEEditorMCP -format=json`
- **THEN** 系统输出所有命令的 JSON schema 定义

#### Scenario: Export as Markdown table
- **WHEN** 用户运行 `-run=UEEditorMCP -format=markdown`
- **THEN** 系统输出所有命令的 Markdown 表格

### Requirement: Commandlet reuses existing infrastructure
Commandlet MUST 复用 MCPBridge 中的 ActionHandlers 注册表和 ExecuteCommandSafe 崩溃保护机制，不得创建独立的命令处理路径。

#### Scenario: Commandlet uses same handlers as TCP server
- **WHEN** Commandlet 执行 `create_blueprint` 命令
- **THEN** 使用与 TCP 服务器完全相同的 `FCreateBlueprintAction` 处理器

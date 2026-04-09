# 需求文档：v0.4.0 平台扩展

## 简介

UE Editor MCP 工具 v0.4.0 版本升级，涵盖七大改进方向：项目配置系统、多编辑器实例支持、UMG Widget 分析扩展、动画蓝图分析增强、CLI 语法优化（减少 JSON）、测试覆盖补全、错误恢复与诊断改进。目标是提升开发者体验、增强工具可靠性、扩展编辑器操控能力。

## 术语表

- **MCP_Server**：Python 中间层 MCP 服务器（`Python/ue_cli_tool/server.py`），通过 stdio 与 AI 客户端通信，通过 TCP 与 UE 编辑器通信
- **UE_Plugin**：C++ 编辑器插件（`Source/UECliTool/`），在 UE 编辑器进程内运行，监听 TCP 连接并执行 Action
- **CLI_Parser**：CLI 语法解析器（`Python/ue_cli_tool/cli_parser.py`），将文本命令解析为可执行的命令字典
- **ActionRegistry**：Action 注册表（`Python/ue_cli_tool/registry/`），维护所有 Action 的元数据、Schema 和搜索索引
- **Connection**：持久化 TCP 连接管理器（`Python/ue_cli_tool/connection.py`），含 Circuit Breaker、心跳、自动重连
- **ContextStore**：会话持久化存储（`Python/ue_cli_tool/context.py`），管理会话元数据、操作历史和工作集
- **BatchContext**：批量执行上下文（`Python/ue_cli_tool/pipeline.py`），收集多条命令在单次 TCP 往返中执行
- **AsyncSubmitter**：异步提交器（`Python/ue_cli_tool/pipeline.py`），提交长时间运行的命令并轮询结果
- **CommandProxy**：动态命令代理（`Python/ue_cli_tool/command_proxy.py`），将 Python 方法调用路由到 C++ 命令
- **Circuit_Breaker**：断路器模式实现，防止级联故障，含 CLOSED/OPEN/HALF_OPEN 三种状态
- **Project_Config**：项目级配置文件（`ue_mcp_config.yaml`），持久化 UE 引擎路径、项目路径、端口等设置
- **Widget_Blueprint**：UMG Widget 蓝图，用于构建 UI 界面的可视化蓝图资产
- **Anim_Blueprint**：动画蓝图，包含状态机、转换规则和动画节点的蓝图资产
- **Steering_File**：Kiro steering 文件（`.kiro/steering/ue-editor-mcp.md`），为 AI 提供项目上下文和工作流指引

---

## 需求

### 需求 1：项目配置文件的创建与加载

**用户故事：** 作为开发者，我希望项目的 UE 引擎路径、项目路径、Lua 脚本目录等配置能持久化到一个配置文件中，这样每次启动工具时无需重复配置。

#### 验收标准

1. THE MCP_Server SHALL 在启动时从工作区根目录向上搜索 `ue_mcp_config.yaml` 配置文件并加载其中的配置项
2. WHEN `ue_mcp_config.yaml` 文件不存在时，THE MCP_Server SHALL 使用默认配置值正常启动，不产生错误
3. THE Project_Config SHALL 支持以下配置项：`engine_root`（UE 引擎根目录）、`project_root`（项目根目录）、`tcp_port`（TCP 端口号，默认 55558）、`lua_script_dirs`（Lua 脚本目录列表）、`extra_action_paths`（额外 Action 定义路径列表）
4. WHEN 配置文件中包含无法识别的配置项时，THE MCP_Server SHALL 忽略该配置项并记录一条 WARNING 级别日志
5. WHEN 配置文件格式不合法（非有效 YAML）时，THE MCP_Server SHALL 记录错误日志并回退到默认配置启动
6. FOR ALL 有效的 Project_Config 对象，将其序列化为 YAML 再解析回 Project_Config 对象 SHALL 产生等价的配置（round-trip 属性）

### 需求 2：Setup 脚本自动写入配置

**用户故事：** 作为开发者，我希望 `setup_mcp.ps1` 脚本在发现 UE 引擎路径后自动将路径信息写入配置文件，这样后续启动无需再次搜索。

#### 验收标准

1. WHEN `setup_mcp.ps1` 成功检测到 UE 引擎路径时，THE Setup_Script SHALL 将 `engine_root` 和 `project_root` 写入 `ue_mcp_config.yaml` 文件
2. WHEN `ue_mcp_config.yaml` 已存在时，THE Setup_Script SHALL 合并新检测到的值与已有配置，保留用户手动添加的配置项
3. WHEN `setup_mcp.bat` 执行时，THE Setup_Script SHALL 执行与 `setup_mcp.ps1` 相同的配置文件写入逻辑

### 需求 3：Steering 文件从配置自动生成

**用户故事：** 作为开发者，我希望 steering 文件中的硬编码路径能从配置文件自动生成，这样切换项目时无需手动修改 steering 文件。

#### 验收标准

1. WHEN MCP_Server 启动且 Project_Config 中包含 `engine_root` 和 `project_root` 时，THE MCP_Server SHALL 通过 `ue_query(query="context")` 返回包含引擎路径、项目路径和编译命令的上下文信息
2. THE MCP_Server SHALL 在 `ue_query(query="context")` 的返回结果中包含基于 Project_Config 生成的编译命令模板

### 需求 4：多编辑器实例端口管理

**用户故事：** 作为开发者，我希望能同时运行多个 UE 编辑器实例并分别通过 MCP 控制，这样可以并行开发多个项目。

#### 验收标准

1. THE Project_Config SHALL 支持 `tcp_port` 配置项，允许用户为每个项目指定不同的 TCP 端口号
2. WHEN `tcp_port` 未在配置文件中指定时，THE MCP_Server SHALL 使用默认端口 55558
3. WHEN MCP_Server 启动时，THE Connection SHALL 从 Project_Config 读取 `tcp_port` 并连接到对应端口
4. THE UE_Plugin SHALL 支持通过插件设置（Project Settings）或命令行参数 `-MCPPort=<port>` 配置监听端口
5. WHEN 指定端口被占用时，THE UE_Plugin SHALL 在日志中输出明确的端口冲突错误信息，包含被占用的端口号和建议的解决方案

### 需求 5：Widget Blueprint 全景快照

**用户故事：** 作为开发者，我希望能一次性获取 Widget Blueprint 的完整结构信息（组件树、事件绑定、动画、属性），这样可以快速理解 UI 蓝图的全貌。

#### 验收标准

1. WHEN 收到 `describe_widget_blueprint_full` 命令时，THE UE_Plugin SHALL 返回包含以下信息的完整快照：组件层级树（含每个组件的类型、名称、可见性、Slot 属性）、事件绑定列表、UMG 动画列表、MVVM 绑定列表、Widget 变量列表
2. THE UE_Plugin SHALL 在 `describe_widget_blueprint_full` 的返回结果中为每个组件包含其直接子组件列表，形成完整的层级树结构
3. WHEN 指定的 Widget Blueprint 不存在时，THE UE_Plugin SHALL 返回包含资产路径的错误信息

### 需求 6：Widget 动画查询与创建

**用户故事：** 作为开发者，我希望能通过 MCP 查询和创建 UMG 动画，这样可以用 AI 辅助构建 UI 动画效果。

#### 验收标准

1. WHEN 收到 `widget_list_animations` 命令时，THE UE_Plugin SHALL 返回指定 Widget Blueprint 中所有 UMG 动画的名称、时长和绑定的属性轨道列表
2. WHEN 收到 `widget_create_animation` 命令时，THE UE_Plugin SHALL 在指定 Widget Blueprint 中创建一个新的 UMG 动画，并返回动画名称
3. WHEN 收到 `widget_add_animation_track` 命令时，THE UE_Plugin SHALL 为指定动画添加属性轨道，支持绑定到组件的 Opacity、RenderTransform、Color 等可动画属性
4. IF 指定的动画名称已存在，THEN THE UE_Plugin SHALL 返回名称冲突错误而非覆盖已有动画

### 需求 7：Widget Blueprint 引用关系分析

**用户故事：** 作为开发者，我希望能查询 Widget Blueprint 之间的引用关系，这样可以理解 UI 系统的依赖结构。

#### 验收标准

1. WHEN 收到 `widget_get_references` 命令时，THE UE_Plugin SHALL 返回指定 Widget Blueprint 引用的其他 Widget Blueprint 列表（子 Widget 引用）
2. WHEN 收到 `widget_get_referencers` 命令时，THE UE_Plugin SHALL 返回引用了指定 Widget Blueprint 的其他资产列表

### 需求 8：Widget 样式批量查询

**用户故事：** 作为开发者，我希望能批量查询 Widget 组件的样式属性（字体、颜色、边距等），这样可以快速审查 UI 一致性。

#### 验收标准

1. WHEN 收到 `widget_batch_get_styles` 命令时，THE UE_Plugin SHALL 返回指定 Widget Blueprint 中所有组件的样式属性摘要，包含字体大小、颜色值、Padding、Margin 等视觉属性
2. WHEN 指定 `--filter_type` 参数时，THE UE_Plugin SHALL 仅返回匹配类型的组件样式（如仅 TextBlock 组件）

### 需求 9：动画蓝图全景快照

**用户故事：** 作为开发者，我希望能一次性获取动画蓝图的完整结构信息，这样可以快速理解动画系统的全貌。

#### 验收标准

1. WHEN 收到 `describe_anim_blueprint_full` 命令时，THE UE_Plugin SHALL 返回包含以下信息的完整快照：所有状态机列表（含状态和转换规则摘要）、动画资产引用列表、蓝图变量列表、Skeleton 引用、EventGraph 节点摘要
2. THE UE_Plugin SHALL 在快照中为每个状态机包含其状态数量、转换规则数量和入口状态名称
3. WHEN 指定的动画蓝图不存在时，THE UE_Plugin SHALL 返回包含资产路径的错误信息

### 需求 10：Montage 与 BlendSpace 查询

**用户故事：** 作为开发者，我希望能通过 MCP 查询 Montage 和 BlendSpace 资产的结构信息，这样可以用 AI 辅助分析动画资产。

#### 验收标准

1. WHEN 收到 `anim_describe_montage` 命令时，THE UE_Plugin SHALL 返回指定 Montage 的 Section 列表、Slot 名称、Notify 列表和引用的动画序列
2. WHEN 收到 `anim_describe_blendspace` 命令时，THE UE_Plugin SHALL 返回指定 BlendSpace 的轴配置（参数名、范围）、采样点列表和引用的动画序列
3. WHEN 指定的资产路径不存在或类型不匹配时，THE UE_Plugin SHALL 返回包含资产路径和期望类型的错误信息

### 需求 11：AnimNotify 查询与管理

**用户故事：** 作为开发者，我希望能通过 MCP 查询和管理动画通知（AnimNotify），这样可以用 AI 辅助配置动画事件。

#### 验收标准

1. WHEN 收到 `anim_list_notifies` 命令时，THE UE_Plugin SHALL 返回指定动画资产中所有 AnimNotify 和 AnimNotifyState 的名称、类型、触发时间和所在 Track
2. WHEN 收到 `anim_add_notify` 命令时，THE UE_Plugin SHALL 在指定动画资产的指定时间点添加一个 AnimNotify 实例
3. WHEN 收到 `anim_remove_notify` 命令时，THE UE_Plugin SHALL 从指定动画资产中移除指定的 AnimNotify 实例

### 需求 12：Skeleton 层级查询

**用户故事：** 作为开发者，我希望能通过 MCP 查询 Skeleton 的骨骼层级结构，这样可以用 AI 辅助理解角色骨骼拓扑。

#### 验收标准

1. WHEN 收到 `anim_get_skeleton_hierarchy` 命令时，THE UE_Plugin SHALL 返回指定 Skeleton 资产的完整骨骼层级树，包含每个骨骼的名称、索引、父骨骼索引
2. WHEN 指定 `--compact true` 参数时，THE UE_Plugin SHALL 仅返回骨骼名称和父子关系，省略 Transform 等详细数据

### 需求 13：CLI 语法增强——数组简写

**用户故事：** 作为开发者，我希望 CLI 语法能用更简洁的方式表达数组参数，减少手写 JSON 的场景。

#### 验收标准

1. WHEN CLI_Parser 遇到逗号分隔的值序列（如 `--items a,b,c`）且对应 Schema 属性类型为 `array` 时，THE CLI_Parser SHALL 将其解析为字符串数组 `["a", "b", "c"]`
2. WHEN 逗号分隔值中的元素可被解析为数字时，THE CLI_Parser SHALL 将其转换为数字数组（如 `--values 1,2,3` 解析为 `[1, 2, 3]`）
3. THE CLI_Parser SHALL 保持对现有 JSON 数组语法 `[1,2,3]` 的完全兼容
4. FOR ALL 通过逗号简写语法解析的数组值，使用 JSON 数组语法表达相同值 SHALL 产生等价的解析结果

### 需求 14：CLI 语法增强——键值对简写

**用户故事：** 作为开发者，我希望 CLI 语法能用更简洁的方式表达简单的键值对参数，减少手写 JSON 对象的场景。

#### 验收标准

1. WHEN CLI_Parser 遇到 `key=value` 格式的 flag 值（如 `--props name=Sword,damage=50`）且对应 Schema 属性类型为 `object` 时，THE CLI_Parser SHALL 将其解析为对象 `{"name": "Sword", "damage": 50}`
2. THE CLI_Parser SHALL 保持对现有 JSON 对象语法 `{"key":"value"}` 的完全兼容
3. FOR ALL 通过键值对简写语法解析的对象值，使用 JSON 对象语法表达相同值 SHALL 产生等价的解析结果

### 需求 15：Connection 模块测试覆盖

**用户故事：** 作为开发者，我希望 `connection.py` 的 Circuit Breaker 和重连逻辑有充分的单元测试，这样可以确保连接管理的可靠性。

#### 验收标准

1. THE Test_Suite SHALL 包含 Circuit_Breaker 状态转换的测试：CLOSED → OPEN（连续失败达到阈值）、OPEN → HALF_OPEN（恢复超时后）、HALF_OPEN → CLOSED（连续成功达到阈值）、HALF_OPEN → OPEN（探测失败）
2. THE Test_Suite SHALL 包含 Circuit_Breaker `allow_request` 方法的测试：CLOSED 状态允许请求、OPEN 状态拒绝请求、HALF_OPEN 状态允许探测请求
3. THE Test_Suite SHALL 包含 Connection 重连逻辑的测试：指数退避延迟计算、达到最大重连次数后停止重连
4. THE Test_Suite SHALL 包含 `_parse_response` 方法的测试：Format 1（success bool）正常和错误响应、Format 2（status string）正常和错误响应、未知格式响应
5. THE Test_Suite SHALL 包含超时分层（TimeoutTier）的测试：已注册命令使用对应超时、未注册命令使用 NORMAL 默认超时
6. FOR ALL Circuit_Breaker 状态转换序列，从 CLOSED 状态开始记录 N 次失败后状态 SHALL 为 OPEN，再经过恢复超时后状态 SHALL 为 HALF_OPEN

### 需求 16：Pipeline 模块测试覆盖

**用户故事：** 作为开发者，我希望 `pipeline.py` 的 BatchContext 和 AsyncSubmitter 有充分的单元测试，这样可以确保批量执行和异步提交的正确性。

#### 验收标准

1. THE Test_Suite SHALL 包含 BatchContext 的测试：通过 `add()` 方法添加命令、通过动态方法（`__getattr__`）添加命令、上下文管理器自动执行、已执行后再次添加命令抛出 RuntimeError、超过最大批量大小抛出 ValueError
2. THE Test_Suite SHALL 包含 AsyncSubmitter 的测试：`submit()` 返回 task_id、`poll()` 返回任务状态、`wait()` 在超时前轮询直到完成、`wait()` 超时返回错误
3. FOR ALL BatchContext 实例，添加 N 条命令后 `pending_count` SHALL 等于 N，执行后 `pending_count` SHALL 等于 0

### 需求 17：CommandProxy 模块测试覆盖

**用户故事：** 作为开发者，我希望 `command_proxy.py` 的动态解析逻辑有充分的单元测试，这样可以确保方法名到 Action 的路由正确性。

#### 验收标准

1. THE Test_Suite SHALL 包含 CommandProxy 方法解析的测试：C++ 命令名直接匹配（如 `create_blueprint`）、首个下划线转点号匹配（如 `blueprint_create` → `blueprint.create`）、渐进式点号替换匹配（如 `node_add_event` → `node.add_event`）
2. THE Test_Suite SHALL 包含 CommandProxy 缓存的测试：首次调用触发解析，后续调用使用缓存
3. THE Test_Suite SHALL 包含 CommandProxy 错误处理的测试：不存在的方法名抛出 AttributeError、以下划线开头的属性名抛出 AttributeError
4. FOR ALL 已注册的 ActionDef，通过 CommandProxy 使用其 C++ 命令名调用 SHALL 成功路由到对应 Action

### 需求 18：Server 模块集成测试

**用户故事：** 作为开发者，我希望 `server.py` 的工具处理逻辑有集成测试，这样可以确保 CLI 解析到命令执行的完整链路正确。

#### 验收标准

1. THE Test_Suite SHALL 包含 `_handle_cli` 的测试：单命令执行、多命令批量执行、空命令返回错误、解析错误返回错误信息
2. THE Test_Suite SHALL 包含 `_handle_query` 的测试：`help` 返回命令列表、`help <command>` 返回命令详情、`search <keyword>` 返回搜索结果、`skills` 返回技能列表、未知查询返回错误
3. THE Test_Suite SHALL 使用 Mock 替代实际 TCP 连接，确保测试可在无 UE 编辑器环境下运行

### 需求 19：UE 编辑器崩溃后的 AI 客户端通知

**用户故事：** 作为开发者，我希望当 UE 编辑器崩溃并重连后，AI 客户端能感知到这一状态变化，这样 AI 可以采取恢复措施。

#### 验收标准

1. WHEN Connection 检测到 UE 编辑器断开连接时，THE ContextStore SHALL 立即将 `ue_connection` 状态更新为 `crashed` 并持久化 crash_context（包含崩溃时间、最后操作、工作集快照）
2. WHEN Connection 成功重连到 UE 编辑器时，THE ContextStore SHALL 将 `ue_connection` 状态更新为 `alive` 并设置 `recovered_from_crash` 标志
3. WHEN AI 客户端调用 `ue_query(query="context")` 时，THE MCP_Server SHALL 在返回结果中包含 `ue_connection` 状态和 `crash_context`（如果存在），使 AI 能感知崩溃恢复事件
4. WHEN AI 客户端调用 `ue_query(query="health")` 时，THE MCP_Server SHALL 在返回结果中包含 Connection 状态、Circuit_Breaker 状态和最近一次重连信息

### 需求 20：连接状态主动诊断

**用户故事：** 作为开发者，我希望 MCP 工具能提供更丰富的连接诊断信息，这样可以快速定位连接问题。

#### 验收标准

1. WHEN AI 客户端调用 `ue_query(query="health")` 时，THE MCP_Server SHALL 返回包含以下字段的诊断信息：连接状态（connected/disconnected/reconnecting/error）、Circuit_Breaker 状态和统计、心跳最后成功时间、重连尝试次数、当前配置的 TCP 端口
2. WHEN Connection 处于 ERROR 状态时，THE MCP_Server SHALL 在 health 响应中包含最近一次错误的描述和发生时间
3. IF 连续 3 次命令执行失败，THEN THE MCP_Server SHALL 在下一次命令响应中附加连接健康警告信息

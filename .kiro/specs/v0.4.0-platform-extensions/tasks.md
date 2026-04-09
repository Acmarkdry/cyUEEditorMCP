# 实现计划：v0.4.0 平台扩展

## 概述

基于需求文档（20 条需求）和设计文档（7 大模块）的增量实现计划。遵循"零手工维护表"原则，所有新增 Action 通过 ActionRegistry 自动注册。实现语言：Python（MCP 层）+ C++（UE 插件层）。任务按模块分组，每个模块内先实现核心逻辑，再补充测试，最后集成串联。

## Tasks

- [x] 1. 实现 ProjectConfig 模块
  - [x] 1.1 创建 `Python/ue_cli_tool/config.py`，实现 `ProjectConfig` 数据类、`load_config()`、`save_config()`、`merge_config()` 函数
    - 定义 `ProjectConfig` dataclass（`engine_root`, `project_root`, `tcp_port`, `lua_script_dirs`, `extra_action_paths`）
    - `load_config(start_dir)` 从指定目录向上搜索 `ue_mcp_config.yaml`，找不到返回默认配置
    - `save_config(config, path)` 序列化为 YAML（`None` 值和空列表字段不写入）
    - `merge_config(existing, updates)` 合并配置，保留 existing 中不在 updates 中的键值
    - 非法 YAML 记录 ERROR 日志并回退默认配置；未知键记录 WARNING 日志并忽略；`tcp_port` 超范围回退默认 55558
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 2.2_

  - [x] 1.2 编写 Property 1 属性测试：ProjectConfig YAML 序列化 round-trip
    - **Property 1: ProjectConfig YAML 序列化 round-trip**
    - 使用 Hypothesis 生成任意合法 `ProjectConfig` 对象，验证 `save_config` → `load_config` round-trip 等价性
    - **Validates: Requirements 1.6**

  - [x] 1.3 编写 Property 2 属性测试：配置合并保留用户自定义项
    - **Property 2: 配置合并保留用户自定义项**
    - 使用 Hypothesis 生成任意 `ProjectConfig` 和更新字典，验证 `merge_config` 保留未更新键值且包含所有更新键值
    - **Validates: Requirements 2.2**

  - [x] 1.4 编写 `tests/test_config.py` 单元测试
    - 测试配置文件不存在时返回默认配置
    - 测试非法 YAML 回退默认配置
    - 测试未知键忽略并记录 WARNING
    - 测试 `tcp_port` 超范围回退默认值
    - 测试 `None` 值和空列表不写入 YAML
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

- [x] 2. 集成 ProjectConfig 到启动流程
  - [x] 2.1 修改 `Python/ue_cli_tool/server.py`，在 `_run()` 启动时调用 `load_config()` 加载配置，将 `tcp_port` 注入 `ConnectionConfig`，将路径信息注入 `ContextStore`
    - 在 `_handle_query` 的 `context` 分支中返回 `engine_root`、`project_root` 和编译命令模板
    - _Requirements: 1.1, 3.1, 3.2, 4.1, 4.2, 4.3_

  - [x] 2.2 修改 `setup_mcp.ps1`，在成功检测到 UE 引擎路径后写入 `ue_mcp_config.yaml`（含 `engine_root` 和 `project_root`）；已有配置文件时合并而非覆盖
    - _Requirements: 2.1, 2.2_

  - [x] 2.3 修改 `setup_mcp.bat`，执行与 `setup_mcp.ps1` 相同的配置文件写入逻辑
    - _Requirements: 2.3_

- [x] 3. Checkpoint - 确保 ProjectConfig 模块测试通过
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. CLI Parser 语法扩展
  - [x] 4.1 在 `Python/ue_cli_tool/cli_parser.py` 中新增 `_coerce_value_with_schema()` 方法
    - 接受 `val`, `param_name`, `command` 参数，从 ActionRegistry 获取 Schema 类型信息
    - 当 Schema 类型为 `array` 且值包含逗号（不以 `[` 开头）时，解析为逗号分隔数组
    - 当 Schema 类型为 `object` 且值包含 `=`（不以 `{` 开头）时，解析为键值对对象
    - 无 Schema 信息时回退到现有 `_coerce_value()` 行为
    - 修改 `parse_line()` 中 flag 参数解析，优先使用 `_coerce_value_with_schema()`
    - _Requirements: 13.1, 13.2, 13.3, 14.1, 14.2_

  - [x] 4.2 编写 Property 3 属性测试：数组语法等价性
    - **Property 3: 数组语法等价性（逗号简写 ≡ JSON）**
    - 使用 Hypothesis 生成同类型元素数组，验证逗号简写与 JSON 数组语法解析结果等价
    - **Validates: Requirements 13.4, 13.1, 13.2, 13.3**

  - [x] 4.3 编写 Property 4 属性测试：对象语法等价性
    - **Property 4: 对象语法等价性（键值对简写 ≡ JSON）**
    - 使用 Hypothesis 生成简单键值对对象，验证键值对简写与 JSON 对象语法解析结果等价
    - **Validates: Requirements 14.3, 14.1, 14.2**

  - [x] 4.4 编写 `tests/test_cli_parser_v04.py` 单元测试
    - 测试逗号分隔字符串数组解析（`a,b,c` → `["a","b","c"]`）
    - 测试逗号分隔数字数组解析（`1,2,3` → `[1,2,3]`）
    - 测试键值对简写解析（`name=Sword,damage=50` → `{"name":"Sword","damage":50}`）
    - 测试 JSON 数组/对象语法保持兼容（`[1,2,3]` 和 `{"a":1}` 不受影响）
    - 测试无 Schema 信息时回退到原有行为
    - 测试解析失败时回退到字符串
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 14.1, 14.2, 14.3_

- [x] 5. Checkpoint - 确保 CLI Parser 扩展测试通过
  - Ensure all tests pass, ask the user if questions arise.

- [x] 6. UMG Widget 分析扩展（C++ Action + Python 注册）
  - [x] 6.1 创建 `Source/UECliTool/Private/Actions/UMGWidgetAnalysisActions.cpp`，实现 7 个新 Action
    - `FDescribeWidgetBlueprintFullAction`（`describe_widget_blueprint_full`）：Widget 全景快照，返回组件层级树、事件绑定、动画、MVVM 绑定、变量列表
    - `FWidgetListAnimationsAction`（`widget_list_animations`）：列出 UMG 动画
    - `FWidgetCreateAnimationAction`（`widget_create_animation`）：创建 UMG 动画
    - `FWidgetAddAnimationTrackAction`（`widget_add_animation_track`）：添加动画属性轨道
    - `FWidgetGetReferencesAction`（`widget_get_references`）：查询子 Widget 引用
    - `FWidgetGetReferencersAction`（`widget_get_referencers`）：查询被引用关系
    - `FWidgetBatchGetStylesAction`（`widget_batch_get_styles`）：批量查询样式属性
    - 所有 Action 继承 `FEditorAction`，复用 `UMGCommonHelpers.h` 中的 `FindWidgetBlueprintByName()`
    - _Requirements: 5.1, 5.2, 5.3, 6.1, 6.2, 6.3, 6.4, 7.1, 7.2, 8.1, 8.2_

  - [x] 6.2 在 `Python/ue_cli_tool/registry/actions.py` 中添加 7 个 Widget 分析 ActionDef
    - 为每个 Action 定义完整的 `id`、`command`、`tags`、`description`、`input_schema`、`examples`
    - 将新 ActionDef 列表注册到 `register_all_actions()` 函数中
    - _Requirements: 5.1, 6.1, 7.1, 8.1_

- [x] 7. 动画蓝图分析扩展（C++ Action + Python 注册）
  - [x] 7.1 创建 `Source/UECliTool/Private/Actions/AnimAnalysisActions.cpp`，实现 7 个新 Action
    - `FDescribeAnimBlueprintFullAction`（`describe_anim_blueprint_full`）：动画蓝图全景快照
    - `FAnimDescribeMontageAction`（`anim_describe_montage`）：Montage 结构查询
    - `FAnimDescribeBlendSpaceAction`（`anim_describe_blendspace`）：BlendSpace 结构查询
    - `FAnimListNotifiesAction`（`anim_list_notifies`）：列出 AnimNotify
    - `FAnimAddNotifyAction`（`anim_add_notify`）：添加 AnimNotify
    - `FAnimRemoveNotifyAction`（`anim_remove_notify`）：移除 AnimNotify
    - `FAnimGetSkeletonHierarchyAction`（`anim_get_skeleton_hierarchy`）：骨骼层级查询
    - 复用 `AnimGraphHelpers` 命名空间中的辅助函数
    - _Requirements: 9.1, 9.2, 9.3, 10.1, 10.2, 10.3, 11.1, 11.2, 11.3, 12.1, 12.2_

  - [x] 7.2 在 `Python/ue_cli_tool/registry/actions.py` 中添加 7 个动画分析 ActionDef
    - 为每个 Action 定义完整的 `id`、`command`、`tags`、`description`、`input_schema`、`examples`
    - 将新 ActionDef 列表注册到 `register_all_actions()` 函数中
    - _Requirements: 9.1, 10.1, 11.1, 12.1_

- [x] 8. 在 C++ 插件中注册新 Action 到 MCPBridge
  - 在 `Source/UECliTool/Private/MCPBridge.cpp` 的 Action 注册表中添加 14 个新 Action 的处理器映射
  - 确保 `describe_widget_blueprint_full`、`widget_list_animations` 等命令能被正确分发到对应 Action 类
  - _Requirements: 5.1, 6.1, 7.1, 8.1, 9.1, 10.1, 11.1, 12.1_

- [x] 9. Checkpoint - 确保 Widget 和动画 ActionDef 注册正确
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Connection 诊断增强与错误恢复
  - [x] 10.1 修改 `Python/ue_cli_tool/connection.py`，扩展 `get_health()` 返回值
    - 新增 `last_heartbeat_success`、`last_error`、`last_error_time`、`consecutive_failures` 字段
    - 新增连续失败计数器 `_consecutive_cmd_failures`，在 `send_command()` 中更新
    - 当连续 3 次命令失败时，在下次响应中附加 `_health_warning` 字段
    - _Requirements: 20.1, 20.2, 20.3_

  - [x] 10.2 修改 `Python/ue_cli_tool/context.py`，增强崩溃恢复上下文
    - 确保 `_on_ue_state_change("crashed", ...)` 时持久化 `crash_context`（崩溃时间、最后操作、工作集快照）
    - 确保重连后设置 `recovered_from_crash` 标志
    - 在 `get_resume_payload()` 中包含 `crash_context` 和恢复信息
    - _Requirements: 19.1, 19.2, 19.3, 19.4_

  - [x] 10.3 修改 `Python/ue_cli_tool/server.py`，在 `_handle_query` 的 `health` 分支中返回增强后的诊断信息
    - _Requirements: 20.1, 20.2_

- [x] 11. C++ 插件端口配置支持
  - 修改 `Source/UECliTool/Private/MCPServer.cpp`，支持通过 Project Settings 或命令行参数 `-MCPPort=<port>` 配置监听端口
  - 端口被占用时输出明确的冲突错误信息，包含端口号和建议解决方案
  - _Requirements: 4.4, 4.5_

- [x] 12. Connection 模块测试覆盖
  - [x] 12.1 编写 Property 5 属性测试：Circuit Breaker 状态转换不变量
    - **Property 5: Circuit Breaker 状态转换不变量**
    - 使用 Hypothesis 生成失败次数 N（N ≥ failure_threshold），验证 CLOSED → OPEN → HALF_OPEN 状态转换
    - **Validates: Requirements 15.6**

  - [x] 12.2 编写 `tests/test_connection.py` 单元测试
    - Circuit Breaker 状态转换：CLOSED → OPEN、OPEN → HALF_OPEN、HALF_OPEN → CLOSED、HALF_OPEN → OPEN
    - `allow_request` 方法：CLOSED 允许、OPEN 拒绝、HALF_OPEN 允许探测
    - 重连逻辑：指数退避延迟计算、达到最大重连次数后停止
    - `_parse_response` 方法：Format 1 正常/错误、Format 2 正常/错误、未知格式
    - 超时分层（TimeoutTier）：已注册命令使用对应超时、未注册命令使用 NORMAL 默认超时
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5, 15.6_

- [x] 13. Pipeline 模块测试覆盖
  - [x] 13.1 编写 Property 6 属性测试：BatchContext pending_count 不变量
    - **Property 6: BatchContext pending_count 不变量**
    - 使用 Hypothesis 生成正整数 N（1 ≤ N ≤ 50）和命令名列表，验证添加后 `pending_count == N`，执行后 `pending_count == 0` 且 `is_executed == True`
    - **Validates: Requirements 16.3**

  - [x] 13.2 编写 `tests/test_pipeline.py` 单元测试
    - BatchContext：`add()` 添加命令、`__getattr__` 动态方法、上下文管理器自动执行、已执行后再次添加抛出 RuntimeError、超过最大批量大小抛出 ValueError
    - AsyncSubmitter：`submit()` 返回 task_id、`poll()` 返回状态、`wait()` 超时前轮询、`wait()` 超时返回错误
    - _Requirements: 16.1, 16.2, 16.3_

- [x] 14. CommandProxy 模块测试覆盖
  - [x] 14.1 编写 Property 7 属性测试：CommandProxy 路由完整性
    - **Property 7: CommandProxy 路由完整性**
    - 使用 Hypothesis 从所有已注册 ActionDef 中采样，验证 `_resolve_action(action.command)` 解析结果的 `id` 与原始 `ActionDef.id` 一致
    - **Validates: Requirements 17.4**

  - [x] 14.2 编写 `tests/test_command_proxy.py` 单元测试
    - 方法解析：C++ 命令名直接匹配、首个下划线转点号匹配、渐进式点号替换匹配
    - 缓存：首次调用触发解析，后续调用使用缓存
    - 错误处理：不存在的方法名抛出 AttributeError、以下划线开头的属性名抛出 AttributeError
    - _Requirements: 17.1, 17.2, 17.3, 17.4_

- [x] 15. Server 模块集成测试
  - [x] 15.1 编写 `tests/test_server.py` 集成测试（Mock TCP）
    - `_handle_cli`：单命令执行、多命令批量执行、空命令返回错误、解析错误返回错误信息
    - `_handle_query`：`help` 返回命令列表、`help <command>` 返回命令详情、`search <keyword>` 返回搜索结果、`skills` 返回技能列表、未知查询返回错误
    - 使用 Mock 替代实际 TCP 连接，确保测试可在无 UE 编辑器环境下运行
    - _Requirements: 18.1, 18.2, 18.3_

- [x] 16. Checkpoint - 确保所有测试通过
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- 标记 `*` 的子任务为可选测试任务，可跳过以加速 MVP 交付
- 每个任务引用了具体的需求编号，确保需求可追溯
- Checkpoint 任务确保增量验证，避免问题累积
- 属性测试验证设计文档中定义的 7 个正确性属性，单元测试覆盖具体示例和边界条件
- C++ Action（需求 5-12）需要 UE 编辑器环境编译验证，Python 层注册和测试可离线完成
- 需求 4.4、4.5（C++ 端口配置）在任务 11 中实现，需要 UE 编辑器环境验证

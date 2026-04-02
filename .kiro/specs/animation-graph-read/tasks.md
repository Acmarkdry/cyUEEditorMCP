# 实现计划：AnimGraph MCP 完整操作扩展

## 概述

按照现有架构模式（C++ Action → Python Registry → Skill System）逐步实现 AnimGraph 完整操作能力。
每个任务均可独立编译验证，最终通过 Skill 系统集成和测试覆盖收尾。

## 任务

- [x] 1. 添加 UE Build 模块依赖
  - 在 `Source/UEEditorMCP/UEEditorMCP.Build.cs` 的 `PrivateDependencyModuleNames` 中追加 `"AnimGraph"` 和 `"AnimGraphRuntime"`
  - 确认两个模块均为 Editor-only，不引入运行时依赖
  - _需求：19.1, 19.2_

- [x] 2. 实现 C++ 头文件：AnimGraphActions.h
  - 新建 `Source/UEEditorMCP/Public/Actions/AnimGraphActions.h`
  - 声明命名空间 `AnimGraphHelpers`，包含辅助函数：`ValidateAnimBlueprint`、`FindAnimSubGraph`、`FindStateNode`、`FindTransitionNode`、`SerializeAnimNode`、`ExtractAnimAssetReferences`、`CreateAnimNodeByType`
  - 声明 5 个只读 Action 类（继承 `FBlueprintNodeAction`，`RequiresSave()` 返回 `false`）：`FListAnimGraphGraphsAction`、`FDescribeAnimGraphTopologyAction`、`FGetStateMachineStructureAction`、`FGetStateSubgraphAction`、`FGetTransitionRuleAction`
  - 声明 1 个创建 Action 类（继承 `FEditorAction`）：`FCreateAnimBlueprintAction`
  - 声明 12 个写入 Action 类（继承 `FBlueprintNodeAction`）：`FAddStateMachineAction`、`FAddStateAction`、`FRemoveStateAction`、`FAddTransitionRuleAction`、`FRemoveTransitionRuleAction`、`FAddAnimNodeAction`、`FSetAnimNodePropertyAction`、`FConnectAnimNodesAction`、`FDisconnectAnimNodeAction`、`FRenameStateAction`、`FSetTransitionPriorityAction`、`FCompileAnimBlueprintAction`
  - _需求：15.1, 15.3, 15.5_

- [x] 3. 实现 C++ 源文件：AnimGraphActions.cpp（辅助函数 + 只读 Action）
  - [x] 3.1 新建 `Source/UEEditorMCP/Private/Actions/AnimGraphActions.cpp`，添加必要的 `#include`（AnimBlueprint.h、AnimBlueprintGraph.h、AnimStateNode.h、AnimStateTransitionNode.h、AnimGraphNode_Base.h、AnimGraphNode_StateMachine.h、AnimGraphNode_SequencePlayer.h、AnimGraphNode_BlendSpacePlayer.h 等）
    - _需求：15.5_
  - [x] 3.2 实现 `AnimGraphHelpers` 命名空间中的所有辅助函数
    - `ValidateAnimBlueprint`：Cast<UAnimBlueprint> 验证，失败时设置 OutError
    - `FindAnimSubGraph`：遍历 `AnimBP->FunctionGraphs` + `AnimBP->UbergraphPages` 按名称查找子图，失败时列出可用图名
    - `FindStateNode`：在状态机图中按名称查找 `UAnimStateNode`，失败时列出可用状态名
    - `FindTransitionNode`：在状态机图中查找 `UAnimStateTransitionNode`，失败时返回描述性错误
    - `SerializeAnimNode`：复用 `FGraphDescribeEnhancedAction` 的 compact/enhanced 序列化模式
    - `ExtractAnimAssetReferences`：识别 `UAnimGraphNode_SequencePlayer`、`UAnimGraphNode_BlendSpacePlayer` 等节点的资产引用
    - `CreateAnimNodeByType`：根据 node_type 字符串（AnimSequencePlayer、BlendSpacePlayer、LayeredBlendPerBone、TwoWayBlend、BlendPosesByBool、BlendPosesByInt）创建对应节点
    - _需求：15.3_
  - [x] 3.3 实现 `FListAnimGraphGraphsAction::Validate` 和 `ExecuteInternal`
    - Validate：调用 `ValidateBlueprint` + `ValidateAnimBlueprint`
    - ExecuteInternal：遍历 `AnimBP->FunctionGraphs`、`AnimBP->UbergraphPages`，返回图名称、图类型、节点数量；附加 Skeleton 引用和父类信息
    - _需求：1.1, 1.2, 1.3, 1.4_
  - [x] 3.4 实现 `FDescribeAnimGraphTopologyAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + 图名称参数存在
    - ExecuteInternal：调用 `FindAnimSubGraph` 定位目标图，序列化节点/引脚/边（支持 compact 参数），附加动画资产引用
    - _需求：2.1, 2.2, 2.3, 2.4, 2.5_
  - [x] 3.5 实现 `FGetStateMachineStructureAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name 参数
    - ExecuteInternal：查找状态机图，遍历 `UAnimStateNode` 和 `UAnimStateTransitionNode`，识别入口状态（`UAnimStateEntryNode`），返回状态列表、转换列表、入口状态、每个状态的子图节点数
    - _需求：3.1, 3.2, 3.3, 3.4, 3.5_
  - [x] 3.6 实现 `FGetStateSubgraphAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + state_name 参数
    - ExecuteInternal：查找状态机 → 查找状态节点 → 获取状态的 `BoundGraph` → 序列化节点拓扑，标注 AnimSequence/BlendSpace 资产路径
    - _需求：4.1, 4.2, 4.3, 4.4_
  - [x] 3.7 实现 `FGetTransitionRuleAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + source_state + target_state 参数
    - ExecuteInternal：查找转换规则节点 → 获取其 `BoundGraph` → 序列化节点拓扑，标注引用的蓝图变量名称
    - _需求：5.1, 5.2, 5.3_

- [x] 4. 实现 C++ 源文件：AnimGraphActions.cpp（写入 Action）
  - [x] 4.1 实现 `FCreateAnimBlueprintAction::Validate` 和 `ExecuteInternal`
    - Validate：验证 name 和 skeleton 参数；加载 Skeleton 资产，不存在时返回错误
    - ExecuteInternal：使用 `UAnimBlueprintFactory` 创建动画蓝图，支持 parent_class（默认 AnimInstance）和 path（默认 /Game/Blueprints）参数；调用 `FAssetRegistryModule::AssetCreated`；返回蓝图名称和资产路径
    - _需求：6.1, 6.2, 6.3, 6.4, 6.5_
  - [x] 4.2 实现 `FAddStateMachineAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图为 AnimBlueprint + state_machine_name 参数；检查同名状态机是否已存在
    - ExecuteInternal：在 AnimGraph 中使用 `FEdGraphSchemaAction_K2NewNode::SpawnNode` 创建 `UAnimGraphNode_StateMachine`；设置状态机名称；调用 `MarkBlueprintModified`；返回节点 GUID
    - _需求：7.1, 7.2, 7.3, 7.4_
  - [x] 4.3 实现 `FAddStateAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + state_name；检查同名状态是否已存在
    - ExecuteInternal：查找状态机图 → 在图中创建 `UAnimStateNode`；设置状态名称；返回节点 GUID
    - _需求：8.1, 8.2, 8.5_
  - [x] 4.4 实现 `FRemoveStateAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + state_name；检查是否为入口状态（禁止删除）
    - ExecuteInternal：查找状态节点 → 断开所有关联转换规则 → 从图中移除节点；调用 `MarkBlueprintModified`
    - _需求：8.3, 8.4_
  - [x] 4.5 实现 `FAddTransitionRuleAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + source_state + target_state；检查源/目标状态是否存在；检查同方向转换是否已存在
    - ExecuteInternal：查找源/目标状态节点 → 使用图 Schema 创建转换规则连接；返回转换规则节点 GUID
    - _需求：9.1, 9.2, 9.4, 9.5_
  - [x] 4.6 实现 `FRemoveTransitionRuleAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + source_state + target_state；检查转换规则是否存在
    - ExecuteInternal：查找转换规则节点 → 断开连接 → 从图中移除；调用 `MarkBlueprintModified`
    - _需求：9.3, 9.4_
  - [x] 4.7 实现 `FAddAnimNodeAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + graph_name + node_type；验证 node_type 在支持列表中；若指定 anim_asset 则验证资产存在
    - ExecuteInternal：调用 `CreateAnimNodeByType` 创建节点；若提供 anim_asset 则绑定资产；支持 node_position 参数；返回节点 GUID
    - _需求：10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7_
  - [x] 4.8 实现 `FSetAnimNodePropertyAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + graph_name + node_guid + property_name；查找节点，不存在时返回错误
    - ExecuteInternal：通过 GUID 查找节点 → 使用 `FProperty` 反射设置属性值；支持动画资产路径绑定（LoadObject）；不存在的属性名时列出可用属性
    - _需求：11.1, 11.2, 11.3, 11.4, 11.5_
  - [x] 4.9 实现 `FConnectAnimNodesAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + graph_name + source_node_id + source_pin + target_node_id + target_pin
    - ExecuteInternal：查找源/目标节点和引脚 → 调用 `Schema->TryCreateConnection`；类型不兼容时返回描述性错误；引脚不存在时列出可用引脚
    - _需求：12.1, 12.3, 12.4_
  - [x] 4.10 实现 `FDisconnectAnimNodeAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + graph_name + node_guid + pin_name
    - ExecuteInternal：查找节点和引脚 → 调用 `Pin->BreakAllPinLinks()`；调用 `MarkBlueprintModified`
    - _需求：12.2, 12.4_
  - [x] 4.11 实现 `FRenameStateAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + old_name + new_name；检查新名称是否与已有状态冲突
    - ExecuteInternal：查找状态节点 → 修改状态名称；调用 `MarkBlueprintModified`
    - _需求：13.1, 13.3, 13.4_
  - [x] 4.12 实现 `FSetTransitionPriorityAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图 + state_machine_name + source_state + target_state + priority
    - ExecuteInternal：查找转换规则节点 → 修改优先级属性；调用 `MarkBlueprintModified`
    - _需求：13.2, 13.4_
  - [x] 4.13 实现 `FCompileAnimBlueprintAction::Validate` 和 `ExecuteInternal`
    - Validate：验证蓝图为 AnimBlueprint
    - ExecuteInternal：调用 `FKismetEditorUtilities::CompileBlueprint`；收集编译消息（复用 `FCompileBlueprintAction::CollectCompilationMessages` 模式）；编译成功后保存脏包；返回状态、错误数、警告数、错误列表
    - _需求：14.1, 14.2, 14.3, 14.4_

- [x] 5. 在 MCPBridge.cpp 中注册所有 AnimGraph Action
  - 在 `Source/UEEditorMCP/Private/MCPBridge.cpp` 中添加 `#include "Actions/AnimGraphActions.h"`
  - 在 `RegisterActions()` 中新增一个 AnimGraph Actions 注册块，按设计文档中的 C++ Command 名称注册全部 18 个 Action
  - _需求：15.2_

- [ ] 6. 检查点 — 确保 C++ 代码可编译
  - 确保所有头文件 include 路径正确，无循环依赖
  - 确保所有 Action 类的 `Validate` 和 `ExecuteInternal` 方法签名与基类一致
  - 确保 `RequiresSave()` 在只读 Action 中正确返回 `false`
  - 请向用户确认是否需要在此处执行 C++ 编译验证

- [x] 7. 实现 Python ActionDef 注册
  - 在 `Python/ue_editor_mcp/registry/actions.py` 中新增 `_ANIMGRAPH_ACTIONS` 列表
  - [x] 7.1 注册 5 个只读 ActionDef（capabilities=("read",)，risk="safe"）
    - `animgraph.list_graphs`（command: list_animgraph_graphs）
    - `animgraph.describe_topology`（command: describe_animgraph_topology）
    - `animgraph.get_state_machine`（command: get_state_machine_structure）
    - `animgraph.get_state_subgraph`（command: get_state_subgraph）
    - `animgraph.get_transition_rule`（command: get_transition_rule）
    - 每个 ActionDef 包含：id、command、tags（含 "animgraph"、"animation"）、description、input_schema（type:object）、examples（至少一个）
    - _需求：16.1, 16.2, 16.4_
  - [x] 7.2 注册 13 个写入 ActionDef
    - `animgraph.create_blueprint`（command: create_anim_blueprint，capabilities=("write",)）
    - `animgraph.add_state_machine`（command: add_state_machine，capabilities=("write",)）
    - `animgraph.add_state`（command: add_animgraph_state，capabilities=("write",)）
    - `animgraph.remove_state`（command: remove_animgraph_state，capabilities=("write", "destructive")，risk="moderate"）
    - `animgraph.add_transition`（command: add_transition_rule，capabilities=("write",)）
    - `animgraph.remove_transition`（command: remove_transition_rule，capabilities=("write", "destructive")，risk="moderate"）
    - `animgraph.add_node`（command: add_anim_node，capabilities=("write",)）
    - `animgraph.set_node_property`（command: set_anim_node_property，capabilities=("write",)）
    - `animgraph.connect_nodes`（command: connect_anim_nodes，capabilities=("write",)）
    - `animgraph.disconnect_node`（command: disconnect_anim_node，capabilities=("write",)）
    - `animgraph.rename_state`（command: rename_animgraph_state，capabilities=("write",)）
    - `animgraph.set_transition_priority`（command: set_transition_priority，capabilities=("write",)）
    - `animgraph.compile`（command: compile_anim_blueprint，capabilities=("write",)）
    - _需求：16.1, 16.2, 16.4_
  - [x] 7.3 在 `register_all_actions()` 函数中调用 `registry.register_many(_ANIMGRAPH_ACTIONS)`
    - _需求：16.3_

- [x] 8. 实现 Skill 系统集成
  - [x] 8.1 在 `Python/ue_editor_mcp/skills/__init__.py` 的 `SKILL_DEFS` 列表中新增 `SkillDef`
    - id="animgraph"，name="AnimGraph 动画图"，description 描述读取/创建/修改/编译能力
    - action_ids 包含全部 18 个 animgraph.* action_id（按读取→创建→修改→编译顺序排列）
    - workflows_file="animgraph.md"
    - _需求：17.1_
  - [x] 8.2 新建 `Python/ue_editor_mcp/skills/animgraph.md` 工作流提示文件
    - 包含典型使用流程：读取查询工作流（list_graphs → describe_topology → get_state_machine）
    - 包含创建开发工作流（create_blueprint → add_state_machine → add_state → add_transition → add_node → compile）
    - 包含修改编辑工作流（get_state_machine → rename_state / set_transition_priority → compile）
    - 包含关键模式说明（节点 GUID 的使用、compact 模式降低输出体积、编译诊断流程）
    - _需求：17.2_

- [ ] 9. 检查点 — 确保 Python 端注册正确
  - 确保所有 animgraph.* action_id 在 registry 中可通过 `get_registry().get(aid)` 查找到
  - 确保 animgraph skill 可通过 `load_skill("animgraph")` 加载，且 action_count > 0
  - 确保 animgraph.md 文件存在于 skills 目录
  - 请向用户确认是否需要在此处手动运行 test_skills.py 验证

- [x] 10. 新增测试文件 tests/test_animgraph.py
  - [x] 10.1 实现 Property 1 测试：ActionDef 结构正确性
    - 遍历所有 `animgraph.*` ActionDef，验证 id 匹配 `animgraph\.\w+` 正则、command 非空、tags 包含 "animgraph"、description 非空、input_schema 包含 `"type": "object"`、examples 非空
    - 注释：`# Feature: animation-graph-read, Property 1: ActionDef 结构正确性`
    - _需求：18.3, 16.1, 16.4_
  - [x] 10.2 实现 Property 2 测试：Capabilities 分类正确性（属性测试）
    - 遍历所有 `animgraph.*` ActionDef，根据已知的读/写/删除分类验证 capabilities 字段
    - 只读 action（list_graphs、describe_topology、get_state_machine、get_state_subgraph、get_transition_rule）的 capabilities 应为 `("read",)`
    - 删除 action（remove_state、remove_transition）的 capabilities 应包含 "write" 和 "destructive"
    - 其余写入 action 的 capabilities 应包含 "write"
    - 注释：`# Feature: animation-graph-read, Property 2: Capabilities 分类正确性`
    - _需求：18.3, 16.2_
  - [x] 10.3 实现 Property 3 测试：input_schema JSON 序列化 round-trip（属性测试）
    - 对所有 animgraph ActionDef 的 input_schema 执行 `json.loads(json.dumps(schema))`，验证结果与原始 schema 等价
    - 注释：`# Feature: animation-graph-read, Property 3: input_schema JSON 序列化 round-trip`
    - _需求：18.4_
  - [x] 10.4 实现单元测试：验证所有 animgraph action_id 在 registry 中存在
    - _需求：18.1_
  - [x] 10.5 实现单元测试：验证 animgraph skill 可加载且包含所有 animgraph action_id
    - _需求：18.2, 17.3_
  - [x] 10.6 实现单元测试：验证 animgraph.md 工作流文件存在
    - _需求：18.2_

- [x] 11. 更新 README.md 和 LICENSE 文件
  - [x] 11.1 在 README.md 的"动作域（核心）"表格中新增 `animgraph.*` 域条目（动作数量：18，说明：AnimGraph 读取/创建/修改/编译，示例 ID：animgraph.list_graphs、animgraph.create_blueprint、animgraph.compile）
    - _需求：20.1_
  - [x] 11.2 更新 README.md"功能特性"部分的总动作数量（从 141 增加到 159）
    - _需求：20.2_
  - [x] 11.3 在 README.md"开发路线"部分新增 AnimGraph 完整操作扩展阶段条目（标注完成状态 ✅）
    - _需求：20.3_
  - [x] 11.4 在 README.md 顶部保留对 lilklon/UEBlueprintMCP 的致谢声明，并新增对 Acmarkdry/UEEditorMCP 的致谢声明
    - _需求：20.4, 20.5_
  - [x] 11.5 在 README.md 底部许可证部分补充对 @Acmarkdry 的版权说明
    - _需求：20.6_
  - [x] 11.6 在 README.md"关键文件"表格中补充 AnimGraphActions.cpp/h 和 skills/animgraph.md 条目
    - _需求：20.7_
  - [x] 11.7 将 LICENSE 文件的 Copyright 行中的名称更新为 acmarkdry，保持 MIT 协议全文不变
    - _需求：20.9, 20.10_

- [x] 12. 最终检查点 — 确保所有测试通过
  - 确保 test_animgraph.py 中所有测试通过
  - 确保 test_skills.py 中 `test_all_skill_action_ids_exist_in_registry` 和 `test_all_registry_actions_covered_by_skills` 通过（animgraph skill 已覆盖所有新 action）
  - 确保 test_schema_contract.py 中新增的 C++ Action 注册和 Python ActionDef 一致性检查通过
  - 请向用户确认是否需要在此处执行完整测试套件

## 备注

- 标注 `*` 的子任务为可选项，可跳过以加快 MVP 交付
- 每个任务均引用了具体需求编号，便于追溯
- C++ 编译需要 Unreal Engine 编辑器环境，无法在离线环境中验证
- 属性测试（Property 2、Property 3）使用 Hypothesis 库，需确保 `pip install hypothesis` 已执行
- 任务 3 和任务 4 可并行实现（辅助函数 + 只读 Action 与写入 Action 相互独立）

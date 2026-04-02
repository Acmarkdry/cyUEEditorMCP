# 需求文档：动画图（Animation Graph）MCP 完整操作扩展

## 简介

为现有的 UE Editor MCP 插件添加 Unreal 动画蓝图（Animation Blueprint）中动画图（AnimGraph）的完整操作功能，包括读取查询、创建开发和修改编辑。该功能遵循现有架构模式（C++ Action → Python Registry → Skill），使 AI 能够通过 MCP 协议读取、创建、修改和编译动画蓝图的 AnimGraph 结构、状态机、节点拓扑和连接关系，从而全面辅助动画蓝图的开发、调试和迭代。

## 术语表

- **AnimGraph_System**: 负责动画图完整操作（读取、创建、修改、编译）的 C++ Action 集合
- **Animation_Blueprint**: 继承自 UAnimInstance 的特殊蓝图，包含 AnimGraph 和 EventGraph
- **AnimGraph**: 动画蓝图中的动画图，包含状态机、混合节点、动画播放节点等
- **State_Machine**: AnimGraph 中的状态机节点，包含多个状态（State）和转换规则（Transition）
- **State_Node**: 状态机中的单个状态，内部包含一个子图（sub-graph）
- **Transition_Rule**: 状态之间的转换条件，内部包含一个条件表达式子图
- **Anim_Node**: AnimGraph 中的动画节点（如 AnimSequence Player、BlendSpace Player、Blend 节点等）
- **Action_Registry**: Python 端的 ActionDef 注册表，映射 action_id 到 C++ command
- **Skill_System**: Python 端的领域技能目录系统，按领域分组 action 并提供工作流提示
- **MCP_Server**: 统一的 8-tool MCP 服务器（server_unified.py）
- **AnimBlueprint_Factory**: 用于创建动画蓝图资产的 UE 工厂类

## 需求

---

### 需求 1：列出动画蓝图的 AnimGraph 概览

**用户故事：** 作为开发者，我想获取动画蓝图中所有图（AnimGraph + EventGraph）的概览信息，以便快速了解动画蓝图的整体结构。

#### 验收标准

1. WHEN 提供一个有效的动画蓝图名称时，THE AnimGraph_System SHALL 返回该动画蓝图中所有图的列表，包含图名称、图类型（AnimGraph / EventGraph / StateMachine / TransitionGraph）和节点数量
2. WHEN 提供的蓝图名称不是动画蓝图时，THE AnimGraph_System SHALL 返回描述性错误信息，说明该蓝图不是 Animation Blueprint
3. WHEN 提供的蓝图名称不存在时，THE AnimGraph_System SHALL 返回描述性错误信息，说明蓝图未找到
4. THE AnimGraph_System SHALL 返回动画蓝图的骨骼网格体（Skeleton）引用和父类信息

---

### 需求 2：描述 AnimGraph 节点拓扑

**用户故事：** 作为开发者，我想获取 AnimGraph 中所有节点的详细拓扑信息（节点类型、引脚、连接关系），以便理解动画逻辑的数据流。

#### 验收标准

1. WHEN 提供动画蓝图名称和图名称时，THE AnimGraph_System SHALL 返回该图中所有节点的列表，包含节点 GUID、节点类名、节点标题、位置坐标
2. THE AnimGraph_System SHALL 为每个节点返回其所有引脚信息，包含引脚名称、方向（输入/输出）、类型类别和连接目标
3. THE AnimGraph_System SHALL 返回图中所有边（edge）的列表，包含源节点 ID、源引脚名、目标节点 ID、目标引脚名
4. WHEN 指定 compact 模式时，THE AnimGraph_System SHALL 省略隐藏引脚和元数据以减少输出体积
5. WHEN 指定的图名称不存在时，THE AnimGraph_System SHALL 返回描述性错误信息，并列出可用的图名称

---

### 需求 3：读取状态机结构

**用户故事：** 作为开发者，我想获取 AnimGraph 中状态机的完整结构（状态列表、转换规则、默认状态），以便理解角色动画的状态流转逻辑。

#### 验收标准

1. WHEN 提供动画蓝图名称和状态机名称时，THE AnimGraph_System SHALL 返回状态机中所有状态的列表，包含状态名称、状态类型和状态节点 GUID
2. THE AnimGraph_System SHALL 返回所有转换规则的列表，包含源状态名、目标状态名、转换规则节点 GUID 和优先级
3. THE AnimGraph_System SHALL 标识默认入口状态（Entry State）
4. WHEN 提供的状态机名称不存在时，THE AnimGraph_System SHALL 返回描述性错误信息，并列出可用的状态机名称
5. THE AnimGraph_System SHALL 为每个状态返回其内部子图的节点数量，以便评估状态复杂度

---

### 需求 4：读取状态内部子图

**用户故事：** 作为开发者，我想获取状态机中某个状态的内部子图节点拓扑，以便理解该状态下的动画混合逻辑。

#### 验收标准

1. WHEN 提供动画蓝图名称、状态机名称和状态名称时，THE AnimGraph_System SHALL 返回该状态内部子图的完整节点拓扑（节点、引脚、边）
2. THE AnimGraph_System SHALL 识别并标注动画序列播放节点（AnimSequence Player）引用的动画资产路径
3. THE AnimGraph_System SHALL 识别并标注混合空间（BlendSpace）节点引用的混合空间资产路径
4. WHEN 提供的状态名称不存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 5：读取转换规则条件

**用户故事：** 作为开发者，我想获取状态机中转换规则的条件表达式子图，以便理解状态切换的触发条件。

#### 验收标准

1. WHEN 提供动画蓝图名称、状态机名称、源状态名和目标状态名时，THE AnimGraph_System SHALL 返回对应转换规则子图的节点拓扑
2. THE AnimGraph_System SHALL 标注转换规则中引用的蓝图变量名称
3. WHEN 指定的转换规则不存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 6：创建动画蓝图

**用户故事：** 作为开发者，我想通过 MCP 创建新的动画蓝图资产，以便自动化动画蓝图的初始搭建流程。

#### 验收标准

1. WHEN 提供蓝图名称和骨骼网格体（Skeleton）资产路径时，THE AnimGraph_System SHALL 创建一个新的动画蓝图资产
2. THE AnimGraph_System SHALL 支持指定父类（默认为 UAnimInstance）
3. THE AnimGraph_System SHALL 支持指定资产保存路径（默认为 /Game/Blueprints）
4. WHEN 指定的骨骼网格体资产不存在时，THE AnimGraph_System SHALL 返回描述性错误信息
5. THE AnimGraph_System SHALL 返回创建成功后的蓝图名称和资产路径

---

### 需求 7：在 AnimGraph 中添加状态机

**用户故事：** 作为开发者，我想在动画蓝图的 AnimGraph 中添加新的状态机节点，以便构建角色动画的状态流转逻辑。

#### 验收标准

1. WHEN 提供动画蓝图名称和状态机名称时，THE AnimGraph_System SHALL 在 AnimGraph 中创建一个新的状态机节点
2. THE AnimGraph_System SHALL 返回新创建的状态机节点 GUID
3. WHEN 提供的动画蓝图不存在或不是动画蓝图时，THE AnimGraph_System SHALL 返回描述性错误信息
4. WHEN 同名状态机已存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 8：在状态机中添加和删除状态

**用户故事：** 作为开发者，我想在状态机中添加和删除状态节点，以便构建和调整动画状态流转。

#### 验收标准

1. WHEN 提供动画蓝图名称、状态机名称和状态名称时，THE AnimGraph_System SHALL 在指定状态机中添加一个新状态
2. THE AnimGraph_System SHALL 返回新创建的状态节点 GUID
3. WHEN 提供动画蓝图名称、状态机名称和要删除的状态名称时，THE AnimGraph_System SHALL 从状态机中删除该状态及其关联的转换规则
4. WHEN 要删除的状态是入口状态时，THE AnimGraph_System SHALL 返回描述性错误信息，说明不能删除入口状态
5. WHEN 同名状态已存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 9：在状态机中添加和删除转换规则

**用户故事：** 作为开发者，我想在状态机的状态之间添加和删除转换规则，以便定义状态切换的条件。

#### 验收标准

1. WHEN 提供动画蓝图名称、状态机名称、源状态名和目标状态名时，THE AnimGraph_System SHALL 在两个状态之间创建一条转换规则
2. THE AnimGraph_System SHALL 返回新创建的转换规则节点 GUID
3. WHEN 提供动画蓝图名称、状态机名称、源状态名和目标状态名时，THE AnimGraph_System SHALL 支持删除指定的转换规则
4. WHEN 源状态或目标状态不存在时，THE AnimGraph_System SHALL 返回描述性错误信息
5. WHEN 相同方向的转换规则已存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 10：在 AnimGraph 或状态子图中添加动画节点

**用户故事：** 作为开发者，我想在 AnimGraph 或状态子图中添加动画节点（如 AnimSequence Player、BlendSpace Player、Blend 节点等），以便构建动画混合逻辑。

#### 验收标准

1. WHEN 提供动画蓝图名称、目标图名称和节点类型时，THE AnimGraph_System SHALL 在指定图中添加对应的动画节点
2. THE AnimGraph_System SHALL 支持以下动画节点类型：AnimSequence Player、BlendSpace Player、Layered Blend per Bone、Two Way Blend、Blend Poses by Bool、Blend Poses by Int
3. WHEN 添加 AnimSequence Player 节点时，THE AnimGraph_System SHALL 支持通过参数绑定动画序列资产
4. WHEN 添加 BlendSpace Player 节点时，THE AnimGraph_System SHALL 支持通过参数绑定混合空间资产
5. THE AnimGraph_System SHALL 返回新创建节点的 GUID
6. WHEN 指定的动画资产不存在时，THE AnimGraph_System SHALL 返回描述性错误信息
7. THE AnimGraph_System SHALL 支持指定节点在图中的位置坐标

---

### 需求 11：设置动画节点属性

**用户故事：** 作为开发者，我想设置动画节点的属性（如绑定动画资产、设置混合参数），以便配置动画节点的行为。

#### 验收标准

1. WHEN 提供动画蓝图名称、图名称、节点 GUID 和属性名值对时，THE AnimGraph_System SHALL 设置指定节点的属性
2. THE AnimGraph_System SHALL 支持设置动画资产绑定（AnimSequence、BlendSpace 资产路径）
3. THE AnimGraph_System SHALL 支持设置混合参数（如 Alpha、Blend Weight）的引脚默认值
4. WHEN 指定的节点不存在时，THE AnimGraph_System SHALL 返回描述性错误信息
5. WHEN 指定的属性名不存在时，THE AnimGraph_System SHALL 返回描述性错误信息，并列出可用属性

---

### 需求 12：连接和断开 AnimGraph 节点

**用户故事：** 作为开发者，我想连接和断开 AnimGraph 中的节点引脚，以便构建动画数据流。

#### 验收标准

1. WHEN 提供动画蓝图名称、图名称、源节点 GUID、源引脚名、目标节点 GUID 和目标引脚名时，THE AnimGraph_System SHALL 连接两个节点的引脚
2. WHEN 提供动画蓝图名称、图名称、节点 GUID 和引脚名时，THE AnimGraph_System SHALL 断开该引脚的所有连接
3. WHEN 引脚类型不兼容时，THE AnimGraph_System SHALL 返回描述性错误信息，说明类型不匹配
4. WHEN 指定的节点或引脚不存在时，THE AnimGraph_System SHALL 返回描述性错误信息，并列出可用的引脚

---

### 需求 13：修改状态和转换规则

**用户故事：** 作为开发者，我想重命名状态和修改转换规则的优先级，以便调整动画状态机的行为。

#### 验收标准

1. WHEN 提供动画蓝图名称、状态机名称、旧状态名和新状态名时，THE AnimGraph_System SHALL 重命名指定状态
2. WHEN 提供动画蓝图名称、状态机名称、源状态名、目标状态名和新优先级时，THE AnimGraph_System SHALL 修改指定转换规则的优先级
3. WHEN 新状态名与已有状态名冲突时，THE AnimGraph_System SHALL 返回描述性错误信息
4. WHEN 指定的状态或转换规则不存在时，THE AnimGraph_System SHALL 返回描述性错误信息

---

### 需求 14：编译动画蓝图

**用户故事：** 作为开发者，我想编译动画蓝图并获取诊断信息，以便验证动画图的正确性。

#### 验收标准

1. WHEN 提供动画蓝图名称时，THE AnimGraph_System SHALL 编译该动画蓝图并返回编译结果
2. THE AnimGraph_System SHALL 返回编译状态（成功/失败）、错误数量和警告数量
3. WHEN 编译失败时，THE AnimGraph_System SHALL 返回详细的错误信息列表，包含错误节点 GUID 和错误消息
4. THE AnimGraph_System SHALL 在编译成功后自动保存修改的包

---

### 需求 15：C++ Action 注册

**用户故事：** 作为开发者，我想让新的动画图 Action 遵循现有的 C++ Action 架构模式，以便保持代码一致性和可维护性。

#### 验收标准

1. THE AnimGraph_System SHALL 将所有新 Action 类继承自 FBlueprintNodeAction 基类（或 FBlueprintAction，视具体需求）
2. THE AnimGraph_System SHALL 在 MCPBridge.cpp 的 RegisterActions() 中注册所有新 Action
3. THE AnimGraph_System SHALL 为每个 Action 实现 Validate() 和 ExecuteInternal() 方法
4. THE AnimGraph_System SHALL 将只读 Action 的 RequiresSave() 返回 false，写入/修改 Action 的 RequiresSave() 返回 true（使用基类默认值）
5. THE AnimGraph_System SHALL 将新 Action 的源文件放置在 Source/UEEditorMCP/Private/Actions/AnimGraphActions.cpp 和对应的头文件中

---

### 需求 16：Python Action Registry 注册

**用户故事：** 作为开发者，我想让新的动画图功能在 Python 端的 Action Registry 中正确注册，以便通过 ue_actions_search 和 ue_actions_schema 发现和使用。

#### 验收标准

1. THE Action_Registry SHALL 为每个新 Action 注册 ActionDef，包含 id（animgraph.* 命名空间）、command、tags、description、input_schema 和 examples
2. THE Action_Registry SHALL 将只读 Action 的 capabilities 设置为 ("read",)，写入/修改 Action 的 capabilities 设置为 ("write",) 或 ("write", "destructive")（视操作风险）
3. THE Action_Registry SHALL 在 register_all_actions() 中调用新的注册函数
4. THE Action_Registry SHALL 确保所有 action_id 遵循 dot notation 命名规范（animgraph.verb 格式）

---

### 需求 17：Skill 系统集成

**用户故事：** 作为开发者，我想让动画图功能作为一个独立的 Skill 领域注册，以便 AI 能通过 ue_skills 工具按需加载。

#### 验收标准

1. THE Skill_System SHALL 注册一个新的 SkillDef，id 为 "animgraph"，包含所有动画图 action_id（读取、写入、修改、编译）
2. THE Skill_System SHALL 提供对应的工作流提示 markdown 文件（animgraph.md），描述典型使用流程和关键模式，涵盖读取查询、创建开发和修改编辑的工作流
3. WHEN AI 调用 ue_skills(action="load", skill_id="animgraph") 时，THE Skill_System SHALL 返回完整的 action schema 列表和工作流提示

---

### 需求 18：测试覆盖

**用户故事：** 作为开发者，我想确保新功能有充分的测试覆盖，以便在后续修改中保持质量。

#### 验收标准

1. THE AnimGraph_System SHALL 通过现有的 test_schema_contract.py 测试（Python tool schema ↔ C++ Action 一致性）
2. THE AnimGraph_System SHALL 通过现有的 test_skills.py 测试（skill 中引用的 action_id 在 registry 中存在、所有 registry action 被 skill 覆盖、workflow 文件存在）
3. THE AnimGraph_System SHALL 新增专门的测试文件 tests/test_animgraph.py，验证：新 ActionDef 的 input_schema 结构正确性、action_id 命名规范符合 animgraph.* 模式、只读 action 的 capabilities 为 read-only、写入 action 的 capabilities 包含 write
4. FOR ALL 新注册的 ActionDef，解析其 input_schema 再序列化 SHALL 产生等价的 JSON 对象（round-trip 属性）

---

### 需求 19：UE Build 模块依赖

**用户故事：** 作为开发者，我想确保 C++ 端能正确编译动画图相关的代码，以便访问 AnimGraph 的 API。

#### 验收标准

1. WHEN 动画图功能需要额外的 UE 模块时，THE AnimGraph_System SHALL 在 UEEditorMCP.Build.cs 中添加必要的模块依赖（如 AnimGraph、AnimGraphRuntime 等）
2. THE AnimGraph_System SHALL 确保添加的模块依赖不会引入运行时依赖（仅 Editor 模块）

---

### 需求 20：README 文档更新与版权整理

**用户故事：** 作为开源开发者（fork 维护者），我想在完成动画图功能后更新 README 文档，使其准确反映新增功能，同时正确处理版权归属，以便社区用户了解项目能力并尊重原作者贡献。

#### 验收标准

1. THE README SHALL 在"动作域（核心）"表格中新增 `animgraph.*` 域条目，列出动作数量、说明和示例 ID
2. THE README SHALL 在"功能特性"部分更新总动作数量（从 141 增加到包含新 animgraph 动作的实际数量）
3. THE README SHALL 在"开发路线"部分新增 AnimGraph 完整操作扩展的阶段条目，标注完成状态
4. THE README SHALL 保留顶部对原项目 [lilklon/UEBlueprintMCP](https://github.com/lilklon/UEBlueprintMCP) 的致谢声明和 MIT 协议说明
5. THE README SHALL 新增对 [Acmarkdry/UEEditorMCP](https://github.com/Acmarkdry/UEEditorMCP) 的致谢声明，说明本项目基于该仓库 fork 并扩展，感谢 @Acmarkdry 在架构重构与功能增强方面的贡献
6. THE README SHALL 保留底部许可证部分对原始代码版权归属 @lilklon 的声明，并补充对 @Acmarkdry 的版权说明
7. THE README SHALL 在 server_unified.py 的"关键文件"表格中补充 AnimGraph 相关的新文件条目（如 AnimGraphActions.cpp/h、skills/animgraph.md）
8. THE README SHALL 确保所有新增文档内容使用中文，与现有 README 语言风格保持一致
9. THE LICENSE 文件 SHALL 更新版权人为 acmarkdry（当前 fork 维护者），保持 MIT 协议不变
10. THE LICENSE 文件 SHALL 保留 MIT 协议全文，仅修改 Copyright 行中的名称为 acmarkdry

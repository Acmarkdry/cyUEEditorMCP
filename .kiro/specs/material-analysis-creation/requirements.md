# 需求文档

## 简介

本功能为 UE Editor MCP 插件的材质系统增加**材质分析**与**材质制作辅助**能力。

现有材质系统（16 个 Action）已覆盖基础的创建-编辑-编译-实例化管线，但缺乏以下能力：
1. **分析能力**：对现有材质进行深度检查，识别复杂度、参数结构、依赖关系、潜在问题；
2. **制作辅助能力**：基于意图描述或模板快速生成常见材质图，减少 AI 逐节点构建的往返次数。

目标是让 AI 助手能够：
- 读懂一个陌生材质的结构和意图；
- 诊断材质的性能问题和错误配置；
- 通过高层模板一次性生成完整的常见材质图；
- 在现有材质基础上进行智能修改和扩展。

---

## 词汇表

- **Material_Analyzer**：负责材质分析的系统组件（C++ Action + Python 工具层）
- **Material_Builder**：负责材质模板制作的系统组件
- **Material_Graph**：UE 材质编辑器中的节点图，由表达式节点和连线组成
- **Expression_Node**：材质图中的单个节点（如 Multiply、ScalarParameter 等）
- **Material_Parameter**：可在材质实例中覆写的参数节点（Scalar/Vector/Texture/StaticSwitch）
- **Material_Instance**：基于父材质创建的实例，可覆写参数而无需重新编译
- **Shader_Instruction_Count**：着色器编译后的指令数，衡量材质 GPU 复杂度的关键指标
- **Material_Domain**：材质用途域（Surface / PostProcess / DeferredDecal / LightFunction / UI / Volume）
- **Blend_Mode**：材质混合模式（Opaque / Masked / Translucent / Additive 等）
- **MCP_Server**：Python 侧 MCP 协议服务器，通过 TCP 与 C++ 插件通信
- **Action_Registry**：Python 侧的 Action 元数据注册表，支持关键词搜索和 Schema 自省

---

## 需求

### 需求 1：材质复杂度与统计分析

**用户故事：** 作为 AI 助手，我希望获取一个材质的复杂度统计信息，以便评估其性能开销并决定是否需要优化。

#### 验收标准

1. WHEN 提供有效的材质名称，THE Material_Analyzer SHALL 返回该材质的节点总数、各类型节点数量分布、以及连线总数。
2. WHEN 提供有效的材质名称，THE Material_Analyzer SHALL 返回材质的 Shader_Instruction_Count（若已编译），包含 VS 和 PS 的指令数估算。
3. WHEN 材质包含 Material_Parameter 节点，THE Material_Analyzer SHALL 返回所有参数的列表，包含参数名称、类型（Scalar/Vector/Texture/StaticSwitch）和默认值。
4. WHEN 材质包含 TextureSample 或 TextureSampleParameter2D 节点，THE Material_Analyzer SHALL 返回所有纹理采样节点的数量及其引用的纹理资产路径。
5. IF 指定的材质名称不存在，THEN THE Material_Analyzer SHALL 返回包含错误描述的失败响应。

---

### 需求 2：材质依赖与引用分析

**用户故事：** 作为 AI 助手，我希望了解一个材质的外部依赖关系，以便在修改材质时评估影响范围。

#### 验收标准

1. WHEN 提供有效的材质名称，THE Material_Analyzer SHALL 返回该材质引用的所有外部资产列表，包含纹理、MaterialFunction 和其他材质资产的路径。
2. WHEN 材质包含 MaterialFunctionCall 节点，THE Material_Analyzer SHALL 在依赖列表中标注每个 MaterialFunction 的资产路径和节点名称。
3. WHEN 提供有效的材质名称，THE Material_Analyzer SHALL 返回当前关卡中使用该材质（或其实例）的 Actor 列表，包含 Actor 名称和组件名称。
4. IF 材质未被任何关卡 Actor 引用，THEN THE Material_Analyzer SHALL 在响应中明确标注引用数量为 0。

---

### 需求 3：材质问题诊断

**用户故事：** 作为 AI 助手，我希望对材质进行问题诊断，以便发现配置错误、性能警告和最佳实践违规。

#### 验收标准

1. WHEN 提供有效的材质名称，THE Material_Analyzer SHALL 检查并报告以下配置问题：Domain 与 Blend_Mode 的不兼容组合（如 PostProcess 域使用 Masked 混合模式）。
2. WHEN 材质的纹理采样数量超过平台限制（16 个），THE Material_Analyzer SHALL 在诊断结果中生成一条 warning 级别的诊断条目。
3. WHEN 材质存在未连接到任何输出的孤立节点，THE Material_Analyzer SHALL 在诊断结果中列出所有孤立节点的名称和类型。
4. WHEN 材质包含 Custom HLSL 节点，THE Material_Analyzer SHALL 在诊断结果中生成一条 info 级别的条目，提示 Custom 节点会阻止部分着色器优化。
5. THE Material_Analyzer SHALL 对每条诊断条目返回 severity 字段，值为 error、warning 或 info 之一。
6. IF 材质没有任何诊断问题，THEN THE Material_Analyzer SHALL 返回空的诊断列表和 healthy 状态。

---

### 需求 4：材质图结构对比

**用户故事：** 作为 AI 助手，我希望对比两个材质的图结构差异，以便理解材质变体之间的区别或验证修改结果。

#### 验收标准

1. WHEN 提供两个有效的材质名称，THE Material_Analyzer SHALL 返回两个材质在节点数量、连线数量和参数列表上的差异摘要。
2. WHEN 两个材质的 Material_Parameter 列表不同，THE Material_Analyzer SHALL 分别列出仅在第一个材质中存在的参数和仅在第二个材质中存在的参数。
3. WHEN 两个材质的 Domain 或 Blend_Mode 不同，THE Material_Analyzer SHALL 在对比结果中明确标注这些属性级差异。
4. IF 两个材质名称中有任意一个不存在，THEN THE Material_Analyzer SHALL 返回包含具体缺失材质名称的失败响应。

---

### 需求 5：材质参数批量提取与实例化

**用户故事：** 作为 AI 助手，我希望从现有材质中提取所有参数信息并批量创建实例，以便快速生成材质变体。

#### 验收标准

1. WHEN 提供有效的材质名称，THE Material_Builder SHALL 返回该材质所有 Material_Parameter 的完整列表，包含参数名称、类型、默认值和参数分组（Group）。
2. WHEN 提供材质名称和实例名称列表，THE Material_Builder SHALL 批量创建多个 Material_Instance，每个实例可独立指定参数覆写值。
3. WHEN 批量创建实例时，THE Material_Builder SHALL 对每个实例的参数覆写值进行类型校验，确保 Scalar 参数接收数值、Vector 参数接收 4 元素数组。
4. IF 批量创建中某个实例创建失败，THEN THE Material_Builder SHALL 继续处理剩余实例，并在响应中分别报告成功和失败的实例列表。
5. WHEN 批量创建完成，THE Material_Builder SHALL 返回成功创建的实例数量、失败数量和每个实例的资产路径。

---

### 需求 6：材质图局部替换与节点升级

**用户故事：** 作为 AI 助手，我希望对现有材质图进行局部修改（替换节点类型、升级常量为参数），以便在不重建整个材质的情况下改进材质。

#### 验收标准

1. WHEN 提供材质名称和目标节点名称及新的表达式类型，THE Material_Builder SHALL 将指定节点替换为新类型节点，并尽可能保留原有连线。
2. WHEN 将 Constant 节点替换为 ScalarParameter 节点，THE Material_Builder SHALL 将原 Constant 的值作为新 ScalarParameter 的 DefaultValue，并使用原节点名称作为 ParameterName。
3. WHEN 将 Constant3Vector 节点替换为 VectorParameter 节点，THE Material_Builder SHALL 将原 Constant3Vector 的颜色值作为新 VectorParameter 的 DefaultValue。
4. WHEN 节点替换完成，THE Material_Builder SHALL 自动执行 `material.compile` 验证替换后的材质合法性。
5. IF 指定的目标节点名称在材质中不存在，THEN THE Material_Builder SHALL 返回包含可用节点列表的失败响应。
6. IF 新节点类型与原节点的输出引脚数量不兼容导致连线无法保留，THEN THE Material_Builder SHALL 在响应中列出无法迁移的连线，并完成其余连线的迁移。

---

### 需求 7：MCP 工具注册与 Action Registry 集成

**用户故事：** 作为 AI 助手，我希望通过标准的 `ue_actions_search` 和 `ue_actions_schema` 工具发现和使用新的材质分析与制作 Action，以便与现有工作流无缝集成。

#### 验收标准

1. THE Action_Registry SHALL 为每个新增 Action 注册包含 id、tags、description、input_schema 和 examples 字段的完整 ActionDef 条目。
2. WHEN 使用关键词 "material analyze" 或 "material analysis" 搜索，THE Action_Registry SHALL 在搜索结果中返回材质分析相关 Action。
3. WHEN 使用关键词 "material template" 或 "material create" 搜索，THE Action_Registry SHALL 在搜索结果中返回材质模板制作相关 Action。
4. THE Action_Registry SHALL 为所有新增 Action 的 input_schema 提供至少一个可直接执行的 example。
5. WHEN 通过 `ue_skills(action="load", skill_id="materials")` 加载材质 skill，THE MCP_Server SHALL 在返回内容中包含新增 Action 的工作流提示和使用示例。

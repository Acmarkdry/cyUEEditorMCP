# 技术设计文档：材质分析与制作功能

## 概述

本文档描述 UE Editor MCP 插件材质分析与制作功能的技术设计。该功能在现有 16 个材质 Action 基础上，新增 6 个 C++ Action（分析类 4 个、制作辅助类 2 个）并更新 Action Registry，使 AI 助手能够深度检查材质结构、诊断问题、批量生成实例、以及对现有材质图进行局部修改。

新增 Action 一览：

| Action ID | C++ 命令 | 类别 |
|-----------|----------|------|
| `material.analyze_complexity` | `analyze_material_complexity` | 分析 |
| `material.analyze_dependencies` | `analyze_material_dependencies` | 分析 |
| `material.diagnose` | `diagnose_material` | 分析 |
| `material.diff` | `diff_materials` | 分析 |
| `material.extract_parameters` | `extract_material_parameters` | 制作辅助 |
| `material.batch_create_instances` | `batch_create_material_instances` | 制作辅助 |
| `material.replace_node` | `replace_material_node` | 制作辅助 |

---

## 架构

本功能严格遵循现有架构模式，无需引入新的通信层或模块。

```
MCP 客户端（AI 助手）
        │  ue_actions_run / ue_batch
        ▼
server_unified.py → ActionRegistry（新增 7 条 ActionDef）
        │  TCP/JSON（端口 55558）
        ▼
C++ FMCPBridge::RegisterActions()（新增 7 个处理器）
        │  游戏线程分发
        ▼
MaterialActions.h/cpp（新增 Action 类）
        │
        ▼
UE Material API（UMaterial、UMaterialExpression、IAssetRegistry、TActorIterator）
```

**关键设计决策：**
- 所有新 Action 继承 `FMaterialAction`，复用 `FindMaterial()`、`GetMaterialByNameOrCurrent()` 等基础工具
- 分析类 Action 均为只读（`RequiresSave()` 返回 `false`），不触发材质 dirty 标记
- `material.replace_node` 在替换完成后自动调用 `FCompileMaterialAction` 的执行逻辑验证合法性
- Python 层新增工具注册到 `server_materials.py` 的 `TOOL_HANDLERS`，并在 `registry/actions.py` 的 `_MATERIAL_ACTIONS` 列表末尾追加 ActionDef

---

## 组件与接口

### C++ 层（MaterialActions.h/cpp）

#### FAnalyzeMaterialComplexityAction

```cpp
class UEEDITORMCP_API FAnalyzeMaterialComplexityAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("analyze_material_complexity"); }
    virtual bool RequiresSave() const override { return false; }
    // 返回：node_count, node_type_distribution{}, connection_count,
    //       shader_instructions{vs, ps}（已编译时），parameters[], texture_samples[]
};
```

**实现要点：**
- 遍历 `Material->GetExpressions()` 统计节点类型分布
- 连线数通过遍历每个表达式的输入引脚（`FExpressionInput`）并检查 `Expression != nullptr` 计数
- Shader 指令数通过 `Material->GetMaterialResource(ERHIFeatureLevel::SM5)` 获取 `GetNumPixelShaderInstructions()` / `GetNumVertexShaderInstructions()`（仅在已编译时有效）
- 参数节点通过 `Cast<UMaterialExpressionParameter>` 识别，提取 `ParameterName`、类型、`DefaultValue`
- 纹理采样通过 `Cast<UMaterialExpressionTextureSample>` 和 `Cast<UMaterialExpressionTextureSampleParameter2D>` 识别

#### FAnalyzeMaterialDependenciesAction

```cpp
class UEEDITORMCP_API FAnalyzeMaterialDependenciesAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("analyze_material_dependencies"); }
    virtual bool RequiresSave() const override { return false; }
    // 返回：external_assets[]（含 type/path/node_name），level_references[]（含 actor_name/component_name）
};
```

**实现要点：**
- 外部资产：遍历表达式，`Cast<UMaterialExpressionTextureSample>` 取 `Texture->GetPathName()`；`Cast<UMaterialExpressionMaterialFunctionCall>` 取 `MaterialFunction->GetPathName()`
- 关卡引用：使用 `TActorIterator<AActor>` 遍历当前关卡所有 Actor，对每个 `UPrimitiveComponent` 检查其材质槽是否包含目标材质或其实例（通过 `UMaterialInstance::GetBaseMaterial()` 向上追溯）

#### FDiagnoseMaterialAction

```cpp
class UEEDITORMCP_API FDiagnoseMaterialAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("diagnose_material"); }
    virtual bool RequiresSave() const override { return false; }
    // 返回：status("healthy"|"has_issues"), diagnostics[]{severity, code, message, node_name?}
};
```

**诊断规则表：**

| 规则 | severity | 检测方式 |
|------|----------|----------|
| PostProcess + Masked/Translucent | error | `Domain == MD_PostProcess && BlendMode != BLEND_Opaque` |
| 纹理采样 > 16 | warning | 统计 TextureSample 类节点数量 |
| 孤立节点 | warning | 节点无任何输出连线且不连接到材质输出 |
| Custom HLSL 节点 | info | `Cast<UMaterialExpressionCustom>` 非空 |

孤立节点检测：构建反向连接图（哪些节点被其他节点引用），未被引用且未连接到材质输出引脚的节点即为孤立节点。

#### FDiffMaterialsAction

```cpp
class UEEDITORMCP_API FDiffMaterialsAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("diff_materials"); }
    virtual bool RequiresSave() const override { return false; }
    // 参数：material_name_a, material_name_b
    // 返回：summary{node_count_diff, connection_count_diff}, property_diffs[],
    //       parameters_only_in_a[], parameters_only_in_b[]
};
```

**实现要点：**
- 分别对两个材质调用与 `FAnalyzeMaterialComplexityAction` 相同的统计逻辑
- 参数差异通过集合差运算（以 `ParameterName` 为 key）计算
- Domain/BlendMode 差异直接比较枚举值并转换为字符串

#### FExtractMaterialParametersAction

```cpp
class UEEDITORMCP_API FExtractMaterialParametersAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("extract_material_parameters"); }
    virtual bool RequiresSave() const override { return false; }
    // 返回：parameters[]{name, type, default_value, group, sort_priority}
};
```

**实现要点：**
- 在 `FAnalyzeMaterialComplexityAction` 参数提取基础上，额外读取 `UMaterialExpressionParameter::Group` 和 `SortPriority` 字段
- 支持四种参数类型：`ScalarParameter`、`VectorParameter`、`TextureSampleParameter2D`/`TextureObjectParameter`、`StaticSwitchParameter`

#### FBatchCreateMaterialInstancesAction

```cpp
class UEEDITORMCP_API FBatchCreateMaterialInstancesAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("batch_create_material_instances"); }
    // 参数：parent_material, instances[]{name, path?, scalar_parameters?, vector_parameters?,
    //                                    texture_parameters?, static_switch_parameters?}
    // 返回：created_count, failed_count, results[]{name, path?, success, error?}
};
```

**实现要点：**
- 逐个调用与 `FCreateMaterialInstanceAction` 相同的实例创建逻辑，捕获每个实例的成功/失败状态
- 类型校验：Scalar 参数值必须为 JSON number；Vector 参数值必须为 4 元素 JSON array
- 单个实例失败不中断循环，继续处理剩余实例

#### FReplaceMaterialNodeAction

```cpp
class UEEDITORMCP_API FReplaceMaterialNodeAction : public FMaterialAction
{
    virtual FString GetActionName() const override { return TEXT("replace_material_node"); }
    // 参数：material_name, node_name, new_expression_class, new_properties?
    // 返回：replaced_node, new_node, migrated_connections[], failed_connections[], compile_result{}
};
```

**实现要点：**
1. 查找目标节点（复用 `FMCPEditorContext` 的节点名称解析逻辑）
2. 记录目标节点的所有输入连线（`FExpressionInput` 列表）和输出连线（反向扫描其他节点的输入）
3. 创建新节点（复用 `FAddMaterialExpressionAction` 的节点创建逻辑）
4. 值迁移：`Constant → ScalarParameter`（`R` → `DefaultValue`）；`Constant3Vector → VectorParameter`（`Constant` → `DefaultValue`）
5. 逐条尝试重建连线，记录成功/失败
6. 删除旧节点（复用 `FRemoveMaterialExpressionAction` 逻辑）
7. 调用 `FCompileMaterialAction::ExecuteInternal()` 验证合法性

---

### Python 层

#### tools/materials.py 新增工具

在现有 `get_tools()` 返回列表中追加 7 个 `Tool` 对象，在 `TOOL_HANDLERS` 字典中追加对应映射：

```python
TOOL_HANDLERS = {
    # ... 现有映射 ...
    "analyze_material_complexity":    "analyze_material_complexity",
    "analyze_material_dependencies":  "analyze_material_dependencies",
    "diagnose_material":              "diagnose_material",
    "diff_materials":                 "diff_materials",
    "extract_material_parameters":    "extract_material_parameters",
    "batch_create_material_instances":"batch_create_material_instances",
    "replace_material_node":          "replace_material_node",
}
```

#### registry/actions.py 新增 ActionDef

在 `_MATERIAL_ACTIONS` 列表末尾追加 7 条 `ActionDef`，格式与现有条目一致（含 `id`、`command`、`tags`、`description`、`input_schema`、`examples`）。

#### skills/materials.md 更新

在现有 skill 文档末尾追加"材质分析工作流"和"批量实例化工作流"两个示例章节。

#### MCPBridge.cpp 注册

在 Material Actions 区块末尾追加：

```cpp
ActionHandlers.Add(TEXT("analyze_material_complexity"),    MakeShared<FAnalyzeMaterialComplexityAction>());
ActionHandlers.Add(TEXT("analyze_material_dependencies"),  MakeShared<FAnalyzeMaterialDependenciesAction>());
ActionHandlers.Add(TEXT("diagnose_material"),              MakeShared<FDiagnoseMaterialAction>());
ActionHandlers.Add(TEXT("diff_materials"),                 MakeShared<FDiffMaterialsAction>());
ActionHandlers.Add(TEXT("extract_material_parameters"),    MakeShared<FExtractMaterialParametersAction>());
ActionHandlers.Add(TEXT("batch_create_material_instances"),MakeShared<FBatchCreateMaterialInstancesAction>());
ActionHandlers.Add(TEXT("replace_material_node"),          MakeShared<FReplaceMaterialNodeAction>());
```

---

## 数据模型

### analyze_complexity 响应

```json
{
  "success": true,
  "result": {
    "material_name": "M_Rock",
    "node_count": 12,
    "node_type_distribution": {
      "TextureSampleParameter2D": 3,
      "Multiply": 2,
      "Lerp": 1,
      "ScalarParameter": 2,
      "VectorParameter": 1,
      "Add": 1,
      "Constant": 2
    },
    "connection_count": 10,
    "shader_instructions": { "vs": 14, "ps": 87, "compiled": true },
    "parameters": [
      { "name": "Roughness", "type": "ScalarParameter", "default_value": 0.5 },
      { "name": "BaseColor", "type": "VectorParameter", "default_value": [1,1,1,1] }
    ],
    "texture_samples": [
      { "node_name": "albedo_tex", "type": "TextureSampleParameter2D", "texture_path": "/Game/Textures/T_Rock_D" }
    ]
  }
}
```

### analyze_dependencies 响应

```json
{
  "success": true,
  "result": {
    "material_name": "M_Rock",
    "external_assets": [
      { "type": "Texture", "path": "/Game/Textures/T_Rock_D", "node_name": "albedo_tex" },
      { "type": "MaterialFunction", "path": "/Game/Functions/MF_PBR", "node_name": "pbr_func" }
    ],
    "level_references": [
      { "actor_name": "SM_Rock_01", "component_name": "StaticMeshComponent0" }
    ],
    "level_reference_count": 1
  }
}
```

### diagnose 响应

```json
{
  "success": true,
  "result": {
    "material_name": "M_Rock",
    "status": "has_issues",
    "diagnostics": [
      { "severity": "warning", "code": "orphan_node", "message": "Node 'unused_const' is not connected to any output", "node_name": "unused_const" },
      { "severity": "info", "code": "custom_hlsl", "message": "Custom HLSL node 'custom_blend' disables some shader optimizations", "node_name": "custom_blend" }
    ]
  }
}
```

### diff 响应

```json
{
  "success": true,
  "result": {
    "material_a": "M_Rock",
    "material_b": "M_Rock_V2",
    "summary": { "node_count_a": 12, "node_count_b": 15, "node_count_diff": 3, "connection_count_diff": 2 },
    "property_diffs": [
      { "property": "blend_mode", "value_a": "Opaque", "value_b": "Masked" }
    ],
    "parameters_only_in_a": [],
    "parameters_only_in_b": [{ "name": "OpacityThreshold", "type": "ScalarParameter" }]
  }
}
```

### batch_create_instances 响应

```json
{
  "success": true,
  "result": {
    "created_count": 2,
    "failed_count": 1,
    "results": [
      { "name": "MI_Rock_Red", "path": "/Game/Materials/MI_Rock_Red", "success": true },
      { "name": "MI_Rock_Blue", "path": "/Game/Materials/MI_Rock_Blue", "success": true },
      { "name": "MI_Rock_Bad", "success": false, "error": "Invalid scalar parameter 'Roughness': expected number, got string" }
    ]
  }
}
```

### replace_node 响应

```json
{
  "success": true,
  "result": {
    "replaced_node": "base_color_const",
    "new_node": "base_color_param",
    "new_expression_class": "VectorParameter",
    "migrated_connections": [{ "target_node": "lerp_node", "target_input": "A" }],
    "failed_connections": [],
    "compile_result": { "success": true, "error_count": 0 }
  }
}
```

---

## 正确性属性

*属性（Property）是在系统所有合法执行路径上都应成立的特征或行为——本质上是对系统应做什么的形式化陈述。属性是人类可读规范与机器可验证正确性保证之间的桥梁。*

### Property 1：节点统计完整性

*对于任意* 含有 N 个表达式节点的材质，`material.analyze_complexity` 返回的 `node_count` 应等于 N，`node_type_distribution` 中所有类型的计数之和也应等于 N，`parameters` 列表长度应等于材质中参数类型节点的实际数量。

**Validates: Requirements 1.1, 1.3, 1.4, 5.1**

### Property 2：依赖列表完整性

*对于任意* 含有已知外部资产引用（纹理、MaterialFunction）的材质，`material.analyze_dependencies` 返回的 `external_assets` 列表应包含所有已知引用，且每条记录的 `path` 字段应与实际资产路径一致。

**Validates: Requirements 2.1, 2.2**

### Property 3：诊断条目 severity 合法性

*对于任意* 材质，`material.diagnose` 返回的每条诊断条目的 `severity` 字段值必须是 `"error"`、`"warning"`、`"info"` 之一；包含 Custom HLSL 节点的材质必须产生至少一条 `info` 级别条目；Domain/BlendMode 不兼容组合必须产生至少一条 `error` 级别条目。

**Validates: Requirements 3.1, 3.4, 3.5**

### Property 4：孤立节点检测完整性

*对于任意* 含有 K 个孤立节点的材质，`material.diagnose` 返回的 `diagnostics` 中 `code == "orphan_node"` 的条目数量应等于 K。

**Validates: Requirements 3.3**

### Property 5：diff 结果一致性

*对于任意* 两个材质 A 和 B，`material.diff` 返回的 `node_count_diff` 应等于分别调用 `material.analyze_complexity` 得到的两个 `node_count` 之差；`parameters_only_in_a` 与 `parameters_only_in_b` 的并集应等于两个材质参数集合的对称差。

**Validates: Requirements 4.1, 4.2, 4.3**

### Property 6：批量实例化部分失败容错

*对于任意* 包含有效实例和无效实例的批量创建请求，`material.batch_create_instances` 应成功创建所有有效实例，`created_count` 等于有效实例数，`failed_count` 等于无效实例数，且 `results` 数组长度等于请求中实例总数。

**Validates: Requirements 5.2, 5.3, 5.4, 5.5**

### Property 7：节点替换值保留

*对于任意* `Constant → ScalarParameter` 或 `Constant3Vector → VectorParameter` 的替换操作，新节点的 `DefaultValue` 应等于原节点的常量值；替换后材质中可迁移的连线数量应等于原节点连线数减去因引脚不兼容而无法迁移的连线数。

**Validates: Requirements 6.1, 6.2, 6.3, 6.6**

### Property 8：ActionDef 注册完整性

*对于任意* 新增 Action，其在 `_MATERIAL_ACTIONS` 中的 `ActionDef` 必须包含非空的 `id`、`command`、`tags`、`description`、`input_schema` 字段，且 `examples` 元组至少包含一个可直接执行的示例。

**Validates: Requirements 7.1, 7.4**

---

## 错误处理

| 场景 | error_type | 处理方式 |
|------|-----------|----------|
| 材质名称不存在 | `material_not_found` | 返回失败响应，message 含材质名称 |
| diff 时任一材质不存在 | `material_not_found` | 返回失败响应，message 明确指出哪个材质缺失 |
| replace_node 目标节点不存在 | `node_not_found` | 返回失败响应，附带 `available_nodes[]` 列表 |
| 批量创建单个实例失败 | 不中断 | 记录到 `results[i].error`，继续处理 |
| 类型校验失败（batch_create） | `type_validation_error` | 记录到对应实例的 `error` 字段 |
| Shader 指令数不可用（未编译） | 不报错 | `shader_instructions.compiled = false`，vs/ps 字段为 0 |
| 关卡未加载（analyze_dependencies） | 不报错 | `level_references = []`，`level_reference_count = 0` |

所有 Action 均通过 `FEditorAction::ExecuteWithCrashProtection()` 包裹，C++ 异常由 `FMCPBridge::ExecuteCommandSafe()` 捕获。

---

## 测试策略

### 单元测试（Python 层）

文件：`tests/test_materials_analysis.py`

- **示例测试**：验证 `material.analyze_complexity` 对已编译材质返回非零 `shader_instructions`（Requirements 1.2）
- **示例测试**：验证 `material.analyze_dependencies` 对有关卡引用的材质返回正确的 Actor 列表（Requirements 2.3）
- **边界情况**：不存在的材质名称返回 `success: false`（Requirements 1.5, 4.4）
- **边界情况**：无诊断问题的材质返回 `status: "healthy"` 和空列表（Requirements 3.6）
- **边界情况**：未被任何 Actor 引用的材质返回 `level_reference_count: 0`（Requirements 2.4）
- **边界情况**：replace_node 目标节点不存在时返回 `available_nodes[]`（Requirements 6.5）
- **示例测试**：`ue_actions_search("material analyze")` 结果包含 `material.analyze_complexity`（Requirements 7.2, 7.3）
- **示例测试**：`ue_skills(action="load", skill_id="materials")` 返回内容包含新 Action 关键词（Requirements 7.5）

### 属性测试（Property-Based Testing）

使用 `hypothesis` 库，最少 100 次迭代。文件：`tests/test_materials_analysis_properties.py`

每个属性测试通过 `ue_batch` 在真实 UE 编辑器中执行，使用 `@given` + `@settings(max_examples=100)` 配置。

```python
# Feature: material-analysis-creation, Property 1: 节点统计完整性
@given(node_specs=st.lists(st.sampled_from(EXPRESSION_TYPES), min_size=1, max_size=20))
@settings(max_examples=100)
def test_node_count_completeness(node_specs): ...

# Feature: material-analysis-creation, Property 3: 诊断条目 severity 合法性
@given(domain=st.sampled_from(DOMAINS), blend_mode=st.sampled_from(BLEND_MODES))
@settings(max_examples=100)
def test_diagnostic_severity_validity(domain, blend_mode): ...

# Feature: material-analysis-creation, Property 4: 孤立节点检测完整性
@given(orphan_count=st.integers(min_value=0, max_value=5))
@settings(max_examples=100)
def test_orphan_node_detection(orphan_count): ...

# Feature: material-analysis-creation, Property 5: diff 结果一致性
@given(nodes_a=st.lists(...), nodes_b=st.lists(...))
@settings(max_examples=100)
def test_diff_consistency(nodes_a, nodes_b): ...

# Feature: material-analysis-creation, Property 6: 批量实例化部分失败容错
@given(valid_count=st.integers(1, 5), invalid_count=st.integers(0, 3))
@settings(max_examples=100)
def test_batch_create_partial_failure(valid_count, invalid_count): ...

# Feature: material-analysis-creation, Property 7: 节点替换值保留
@given(const_value=st.floats(min_value=-1000, max_value=1000, allow_nan=False))
@settings(max_examples=100)
def test_replace_node_value_preservation(const_value): ...

# Feature: material-analysis-creation, Property 8: ActionDef 注册完整性
@given(action_id=st.sampled_from(NEW_ACTION_IDS))
@settings(max_examples=100)
def test_actiondef_completeness(action_id): ...
```

**测试覆盖说明：**
- 属性测试覆盖通用规则（Properties 1–8），通过随机生成材质图结构验证
- 单元测试覆盖具体示例、边界情况和错误条件
- 两者互补：属性测试发现通用逻辑缺陷，单元测试验证具体行为

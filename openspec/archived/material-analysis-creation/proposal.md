## Why

现有材质系统（16 个 Action）已覆盖基础的创建-编辑-编译-实例化管线，但缺乏以下能力：
1. **分析能力**：对现有材质进行深度检查，识别复杂度、参数结构、依赖关系、潜在问题；
2. **制作辅助能力**：批量参数提取与实例化、节点局部替换升级，减少 AI 逐节点构建的往返次数。

目标是让 AI 助手能够：
- 读懂一个陌生材质的结构和意图；
- 诊断材质的性能问题和错误配置；
- 批量生成材质变体实例；
- 在现有材质基础上进行智能节点替换和扩展。

## What Changes

### 新增 7 个 C++ Action

- **分析类（4 个，只读）**：
  - `material.analyze_complexity`：节点统计、类型分布、连线数、Shader 指令数、参数列表、纹理采样
  - `material.analyze_dependencies`：外部资产依赖（纹理、MaterialFunction）、关卡 Actor 引用
  - `material.diagnose`：Domain/BlendMode 不兼容、纹理采样超限、孤立节点、Custom HLSL 检测
  - `material.diff`：两个材质的节点数、连线数、属性、参数集合差异对比

- **制作辅助类（3 个）**：
  - `material.extract_parameters`：完整参数元数据（含 Group、SortPriority）
  - `material.batch_create_instances`：批量创建 Material Instance，单个失败不中断
  - `material.replace_node`：节点类型替换，保留连线，Constant→Parameter 值迁移，自动编译验证

### 联动更新
- `Python/ue_editor_mcp/tools/materials.py`：新增 7 个 Tool 定义和 TOOL_HANDLERS 映射
- `Python/ue_editor_mcp/registry/actions.py`：新增 7 条 ActionDef（含 analyze/analysis/batch/create tags）
- `Python/ue_editor_mcp/skills/materials.md`：新增材质分析、批量实例化、节点替换三个工作流章节
- `Source/UEEditorMCP/Private/MCPBridge.cpp`：注册 7 个新 Action

## Capabilities

### New Capabilities
- `material-analysis`: 材质复杂度分析、依赖分析、问题诊断、图结构对比
- `material-batch-creation`: 批量参数提取与实例化
- `material-node-replacement`: 节点类型替换与连线迁移

## Impact

- **C++ 端**：`MaterialActions.h/cpp` 新增 7 个 Action 类，`MCPBridge.cpp` 新增 7 行注册
- **Python 端**：`tools/materials.py`、`registry/actions.py`、`skills/materials.md` 各追加新内容
- **Tests**：新增 `tests/test_materials_analysis.py`（6 个单元测试）和 `tests/test_materials_analysis_properties.py`（4 个属性测试）

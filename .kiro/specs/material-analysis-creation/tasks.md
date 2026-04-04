# Implementation Plan: Material Analysis and Creation Feature

## Overview

Add 7 new C++ Actions (4 analysis, 3 creation helpers) on top of the existing 16 material Actions, and update the Python tool layer, Action Registry, and Skill documentation.

## Tasks

- [x] 1. Declare 7 new Action classes in MaterialActions.h
  - Append 7 class declarations after `FRefreshMaterialEditorAction`, all inheriting `FMaterialAction`
  - Analysis classes (`RequiresSave()` returns `false`): `FAnalyzeMaterialComplexityAction`, `FAnalyzeMaterialDependenciesAction`, `FDiagnoseMaterialAction`, `FDiffMaterialsAction`
  - Creation helpers: `FExtractMaterialParametersAction` (read-only), `FBatchCreateMaterialInstancesAction`, `FReplaceMaterialNodeAction`
  - Each class declares `ExecuteInternal`, `Validate`, `GetActionName`; `FDiffMaterialsAction` additionally documents `material_name_a` / `material_name_b` parameters
  - _Requirements: 1.1-1.5, 2.1-2.4, 3.1-3.6, 4.1-4.4, 5.1-5.5, 6.1-6.6_

- [x] 2. Implement FAnalyzeMaterialComplexityAction (analyze_material_complexity)
  - [x] 2.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - `Validate`: verify `material_name` is required
    - Iterate `Material->GetExpressions()` to count `node_count` and `node_type_distribution`
    - Count connections: iterate each expression's `FExpressionInput` fields, count where `Expression != nullptr`
    - Get shader instruction count via `Material->GetMaterialResource(ERHIFeatureLevel::SM5)`; when not compiled, `compiled=false`, vs/ps = 0
    - Extract parameter list via `Cast<UMaterialExpressionParameter>` (name, type, default_value)
    - Extract texture samples via `Cast<UMaterialExpressionTextureSample>` and `Cast<UMaterialExpressionTextureSampleParameter2D>`
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

  - [ ]* 2.2 Write property test for Property 1 (node count completeness)
    - **Property 1: Node Count Completeness**
    - **Validates: Requirements 1.1, 1.3, 1.4, 5.1**

- [x] 3. Implement FAnalyzeMaterialDependenciesAction (analyze_material_dependencies)
  - [x] 3.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - Iterate expressions: `Cast<UMaterialExpressionTextureSample>` for `Texture->GetPathName()`, `Cast<UMaterialExpressionMaterialFunctionCall>` for `MaterialFunction->GetPathName()`, build `external_assets[]`
    - Use `TActorIterator<AActor>` to iterate current level, check material slots on each `UPrimitiveComponent`, trace up via `UMaterialInstance::GetBaseMaterial()`, build `level_references[]`
    - When level not loaded: `level_references=[]`, `level_reference_count=0`, no error
    - _Requirements: 2.1, 2.2, 2.3, 2.4_

  - [ ]* 3.2 Write property test for Property 2 (dependency list completeness)
    - **Property 2: Dependency List Completeness**
    - **Validates: Requirements 2.1, 2.2**

- [x] 4. Implement FDiagnoseMaterialAction (diagnose_material)
  - [x] 4.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - Rule 1: `Domain == MD_PostProcess && BlendMode != BLEND_Opaque` -> error, code `domain_blend_incompatible`
    - Rule 2: TextureSample node count > 16 -> warning, code `texture_sample_limit`
    - Rule 3: Build reverse connection graph; nodes not referenced and not connected to material output -> warning, code `orphan_node`, with `node_name`
    - Rule 4: `Cast<UMaterialExpressionCustom>` non-null -> info, code `custom_hlsl`, with `node_name`
    - Each diagnostic entry contains `severity` (error/warning/info), `code`, `message`, optional `node_name`
    - No issues: return `status: "healthy"` and empty `diagnostics[]`
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6_

  - [ ]* 4.2 Write property test for Property 3 (diagnostic severity validity)
    - **Property 3: Diagnostic Severity Validity**
    - **Validates: Requirements 3.1, 3.4, 3.5**

  - [ ]* 4.3 Write property test for Property 4 (orphan node detection completeness)
    - **Property 4: Orphan Node Detection Completeness**
    - **Validates: Requirements 3.3**

- [x] 5. Implement FDiffMaterialsAction (diff_materials)
  - [x] 5.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - `Validate`: verify both `material_name_a` and `material_name_b` are required
    - Reuse same statistics logic as `FAnalyzeMaterialComplexityAction` for both materials
    - Compute `node_count_diff`, `connection_count_diff`; compare Domain/BlendMode enums to generate `property_diffs[]`
    - Set difference on `ParameterName` key to generate `parameters_only_in_a[]` and `parameters_only_in_b[]`
    - If either material missing, return failure response naming the missing material
    - _Requirements: 4.1, 4.2, 4.3, 4.4_

  - [ ]* 5.2 Write property test for Property 5 (diff result consistency)
    - **Property 5: Diff Result Consistency**
    - **Validates: Requirements 4.1, 4.2, 4.3**

- [x] 6. Checkpoint - ensure analysis Actions compile successfully
  - Ensure all tests pass; report any issues.

- [x] 7. Implement FExtractMaterialParametersAction (extract_material_parameters)
  - [x] 7.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - Build on `FAnalyzeMaterialComplexityAction` parameter extraction, additionally read `UMaterialExpressionParameter::Group` and `SortPriority`
    - Support four types: ScalarParameter, VectorParameter, TextureSampleParameter2D/TextureObjectParameter, StaticSwitchParameter
    - Return `parameters[]{name, type, default_value, group, sort_priority}`
    - _Requirements: 5.1_

- [x] 8. Implement FBatchCreateMaterialInstancesAction (batch_create_material_instances)
  - [x] 8.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - Parse `instances[]` array, call same instance creation logic as `FCreateMaterialInstanceAction` for each
    - Type validation: Scalar param value must be JSON number; Vector param value must be 4-element JSON array; validation failure recorded in `results[i].error`
    - Single instance failure does not break loop; continue processing remaining instances
    - Return `created_count`, `failed_count`, `results[]{name, path?, success, error?}`
    - _Requirements: 5.2, 5.3, 5.4, 5.5_

  - [ ]* 8.2 Write property test for Property 6 (batch create partial failure tolerance)
    - **Property 6: Batch Create Partial Failure Tolerance**
    - **Validates: Requirements 5.2, 5.3, 5.4, 5.5**

- [x] 9. Implement FReplaceMaterialNodeAction (replace_material_node)
  - [x] 9.1 Implement `Validate` and `ExecuteInternal` in MaterialActions.cpp
    - Find target node (reuse `FMCPEditorContext` node name resolution); if not found, return failure with `available_nodes[]`
    - Record all input connections (`FExpressionInput` list) and output connections (reverse scan other nodes' inputs)
    - Create new node (reuse `FAddMaterialExpressionAction` node creation logic)
    - Value migration: `Constant -> ScalarParameter` (`R` -> `DefaultValue`, original node name -> `ParameterName`); `Constant3Vector -> VectorParameter` (`Constant` -> `DefaultValue`)
    - Attempt to rebuild each connection, record `migrated_connections[]` and `failed_connections[]`
    - Delete old node (reuse `FRemoveMaterialExpressionAction` logic)
    - Call `FCompileMaterialAction::ExecuteInternal()` to validate, write result to `compile_result{}`
    - _Requirements: 6.1, 6.2, 6.3, 6.4, 6.5, 6.6_

  - [ ]* 9.2 Write property test for Property 7 (node replacement value preservation)
    - **Property 7: Node Replacement Value Preservation**
    - **Validates: Requirements 6.1, 6.2, 6.3, 6.6**

- [x] 10. Register 7 new Actions in MCPBridge.cpp
  - Append after Material Actions block (after `refresh_material_editor`):
    ```cpp
    ActionHandlers.Add(TEXT("analyze_material_complexity"),    MakeShared<FAnalyzeMaterialComplexityAction>());
    ActionHandlers.Add(TEXT("analyze_material_dependencies"),  MakeShared<FAnalyzeMaterialDependenciesAction>());
    ActionHandlers.Add(TEXT("diagnose_material"),              MakeShared<FDiagnoseMaterialAction>());
    ActionHandlers.Add(TEXT("diff_materials"),                 MakeShared<FDiffMaterialsAction>());
    ActionHandlers.Add(TEXT("extract_material_parameters"),    MakeShared<FExtractMaterialParametersAction>());
    ActionHandlers.Add(TEXT("batch_create_material_instances"),MakeShared<FBatchCreateMaterialInstancesAction>());
    ActionHandlers.Add(TEXT("replace_material_node"),          MakeShared<FReplaceMaterialNodeAction>());
    ```
  - _Requirements: 7.1_

- [x] 11. Register 7 new Python tools in tools/materials.py
  - Append 7 `Tool` objects with complete `inputSchema` to `get_tools()` return list
  - Append corresponding mappings to `TOOL_HANDLERS` dict (tool name -> command type)
  - _Requirements: 7.1_

- [x] 12. Append 7 ActionDefs in registry/actions.py
  - Append 7 `ActionDef` entries at end of `_MATERIAL_ACTIONS` list (after `material.get_selected_nodes`, before `]`)
  - Each contains `id`, `command`, `tags`, `description`, `input_schema`, `examples` (at least 1 directly executable example)
  - Tags include "analyze"/"analysis" keywords (analysis) or "create"/"template" keywords (creation helpers) for discoverability
  - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [ ]* 12.1 Write property test for Property 8 (ActionDef registration completeness)
    - **Property 8: ActionDef Registration Completeness**
    - **Validates: Requirements 7.1, 7.4**

- [x] 13. Update skills/materials.md
  - Append "Material Analysis Workflow" section with usage examples for `material.analyze_complexity`, `material.analyze_dependencies`, `material.diagnose`, `material.diff`
  - Append "Batch Instantiation Workflow" section with combined `material.extract_parameters` + `material.batch_create_instances` example
  - Append "Node Replacement Workflow" section with `material.replace_node` usage example
  - _Requirements: 7.5_

- [x] 14. Create tests/test_materials_analysis.py (unit tests)
  - Non-existent material name returns `success: false` (Requirements 1.5, 4.4)
  - Material with no issues returns `status: "healthy"` and empty list (Requirements 3.6)
  - Material not referenced by any Actor returns `level_reference_count: 0` (Requirements 2.4)
  - `replace_node` with non-existent target node returns `available_nodes[]` (Requirements 6.5)
  - `ue_actions_search("material analyze")` result contains `material.analyze_complexity` (Requirements 7.2)
  - `ue_actions_search("material analysis")` result contains analysis Actions (Requirements 7.2)
  - `ue_actions_search("material create")` result contains creation helper Actions (Requirements 7.3)
  - `ue_skills(action="load", skill_id="materials")` return contains new Action keywords (Requirements 7.5)
  - _Requirements: 1.5, 2.4, 3.6, 4.4, 6.5, 7.2, 7.3, 7.5_

- [x] 15. Create tests/test_materials_analysis_properties.py (property tests)
  - Use `hypothesis` library, `@settings(max_examples=100)`
  - [ ]* 15.1 Property 1: Node Count Completeness
    - `@given(node_specs=st.lists(st.sampled_from(EXPRESSION_TYPES), min_size=1, max_size=20))`
    - Verify `node_count == len(node_specs)`, `sum(node_type_distribution.values()) == node_count`
    - **Validates: Requirements 1.1, 1.3, 1.4, 5.1**
  - [ ]* 15.2 Property 3: Diagnostic Severity Validity
    - `@given(domain=st.sampled_from(DOMAINS), blend_mode=st.sampled_from(BLEND_MODES))`
    - Verify each `severity` value is in `{"error", "warning", "info"}`
    - **Validates: Requirements 3.1, 3.4, 3.5**
  - [ ]* 15.3 Property 4: Orphan Node Detection Completeness
    - `@given(orphan_count=st.integers(min_value=0, max_value=5))`
    - Verify `orphan_node` entry count == actual orphan node count
    - **Validates: Requirements 3.3**
  - [ ]* 15.4 Property 5: Diff Result Consistency
    - `@given(nodes_a=st.lists(...), nodes_b=st.lists(...))`
    - Verify `node_count_diff == analyze_a.node_count - analyze_b.node_count`
    - **Validates: Requirements 4.1, 4.2, 4.3**
  - [ ]* 15.5 Property 6: Batch Create Partial Failure Tolerance
    - `@given(valid_count=st.integers(1, 5), invalid_count=st.integers(0, 3))`
    - Verify `created_count + failed_count == len(instances)`, `len(results) == len(instances)`
    - **Validates: Requirements 5.2, 5.3, 5.4, 5.5**
  - [ ]* 15.6 Property 7: Node Replacement Value Preservation
    - `@given(const_value=st.floats(min_value=-1000, max_value=1000, allow_nan=False))`
    - Verify `Constant -> ScalarParameter` new node `DefaultValue == const_value`
    - **Validates: Requirements 6.1, 6.2, 6.3, 6.6**
  - [ ]* 15.7 Property 8: ActionDef Registration Completeness
    - `@given(action_id=st.sampled_from(NEW_ACTION_IDS))`
    - Verify each ActionDef contains non-empty `id`, `command`, `tags`, `description`, `input_schema`, and non-empty `examples`
    - **Validates: Requirements 7.1, 7.4**

- [x] 16. Final checkpoint - ensure all tests pass
  - Ensure all tests pass; report any issues.

## Notes

- Tasks marked `*` are optional and can be skipped for faster MVP delivery
- Analysis Actions (tasks 2-5, 7) are all read-only, `RequiresSave()` returns `false`, no material dirty mark triggered
- Property tests execute via `ue_batch` in real UE editor, requires editor runtime environment
- Each task references specific requirement clauses for traceability

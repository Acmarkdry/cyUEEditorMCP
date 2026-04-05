// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPBridge.h"
#include "MCPServer.h"
#include "Actions/EditorAction.h"
#include "Actions/BlueprintActions.h"
#include "Actions/EditorActions.h"
#include "Actions/NodeActions.h"
#include "Actions/GraphActions.h"
#include "Actions/ProjectActions.h"
#include "Actions/UMGActions.h"
#include "Actions/MaterialActions.h"
#include "Actions/LayoutActions.h"
#include "Actions/EditorDiffActions.h"
#include "Actions/AnimGraphActions.h"
#include "Actions/PythonActions.h"
#include "Actions/NiagaraActions.h"
#include "Actions/DataTableActions.h"
#include "Actions/SequencerActions.h"
#include "Actions/ExtendedActions.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"

UMCPBridge::UMCPBridge()
	: Server(nullptr)
{
}

void UMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Bridge initializing"));

	// Register action handlers
	RegisterActions();

	// Start the TCP server
	Server = new FMCPServer(this, DefaultPort);
	if (Server->Start())
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server started on port %d"), DefaultPort);
	}
	else
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to start server"));
	}
}

void UMCPBridge::Deinitialize()
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Bridge deinitializing"));

	// Stop the server
	if (Server)
	{
		Server->Stop();
		delete Server;
		Server = nullptr;
	}

	// Clear action handlers
	ActionHandlers.Empty();

	Super::Deinitialize();
}

void UMCPBridge::RegisterActions()
{
	// =========================================================================
	// Editor Actions (retained: summary, logs, diagnostics, thumbnails)
	// =========================================================================
	ActionHandlers.Add(TEXT("get_selected_asset_thumbnail"), MakeShared<FGetSelectedAssetThumbnailAction>());
	ActionHandlers.Add(TEXT("get_blueprint_summary"), MakeShared<FGetBlueprintSummaryAction>());
	ActionHandlers.Add(TEXT("describe_blueprint_full"), MakeShared<FDescribeFullAction>());
	ActionHandlers.Add(TEXT("get_editor_logs"), MakeShared<FGetEditorLogsAction>());
	ActionHandlers.Add(TEXT("get_unreal_logs"), MakeShared<FGetUnrealLogsAction>());
	ActionHandlers.Add(TEXT("is_ready"), MakeShared<FEditorIsReadyAction>());
	ActionHandlers.Add(TEXT("request_shutdown"), MakeShared<FRequestEditorShutdownAction>());

	// =========================================================================
	// Layout Actions - Auto-arrange Blueprint graph nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("auto_layout_selected"), MakeShared<FAutoLayoutSelectedAction>());
	ActionHandlers.Add(TEXT("auto_layout_subtree"), MakeShared<FAutoLayoutSubtreeAction>());
	ActionHandlers.Add(TEXT("auto_layout_blueprint"), MakeShared<FAutoLayoutBlueprintAction>());
	ActionHandlers.Add(TEXT("layout_and_comment"), MakeShared<FLayoutAndCommentAction>());

	// =========================================================================
	// Node Actions - Graph Operations
	// =========================================================================
	ActionHandlers.Add(TEXT("connect_blueprint_nodes"), MakeShared<FConnectBlueprintNodesAction>());
	ActionHandlers.Add(TEXT("find_blueprint_nodes"), MakeShared<FFindBlueprintNodesAction>());
	ActionHandlers.Add(TEXT("delete_blueprint_node"), MakeShared<FDeleteBlueprintNodeAction>());
	ActionHandlers.Add(TEXT("get_node_pins"), MakeShared<FGetNodePinsAction>());
	ActionHandlers.Add(TEXT("describe_graph"), MakeShared<FDescribeGraphAction>());
	ActionHandlers.Add(TEXT("get_selected_nodes"), MakeShared<FGetSelectedNodesAction>());
	ActionHandlers.Add(TEXT("collapse_selection_to_function"), MakeShared<FCollapseSelectionToFunctionAction>());
	ActionHandlers.Add(TEXT("collapse_selection_to_macro"), MakeShared<FCollapseSelectionToMacroAction>());
	ActionHandlers.Add(TEXT("set_selected_nodes"), MakeShared<FSetSelectedNodesAction>());
	ActionHandlers.Add(TEXT("batch_select_and_act"), MakeShared<FBatchSelectAndActAction>());

	// =========================================================================
	// Node Actions - Event Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_event_node"), MakeShared<FAddBlueprintEventNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_input_action_node"), MakeShared<FAddBlueprintInputActionNodeAction>());
	ActionHandlers.Add(TEXT("add_enhanced_input_action_node"), MakeShared<FAddEnhancedInputActionNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_custom_event"), MakeShared<FAddBlueprintCustomEventAction>());
	ActionHandlers.Add(TEXT("add_custom_event_for_delegate"), MakeShared<FAddCustomEventForDelegateAction>());

	// =========================================================================
	// Node Actions - Variable Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_variable"), MakeShared<FAddBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("add_blueprint_variable_get"), MakeShared<FAddBlueprintVariableGetAction>());
	ActionHandlers.Add(TEXT("add_blueprint_variable_set"), MakeShared<FAddBlueprintVariableSetAction>());
	ActionHandlers.Add(TEXT("set_node_pin_default"), MakeShared<FSetNodePinDefaultAction>());

	// =========================================================================
	// Node Actions - Function Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_blueprint_function_node"), MakeShared<FAddBlueprintFunctionNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_self_reference"), MakeShared<FAddBlueprintSelfReferenceAction>());
	ActionHandlers.Add(TEXT("add_blueprint_get_self_component_reference"), MakeShared<FAddBlueprintGetSelfComponentReferenceAction>());
	ActionHandlers.Add(TEXT("add_blueprint_branch_node"), MakeShared<FAddBlueprintBranchNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_cast_node"), MakeShared<FAddBlueprintCastNodeAction>());
	ActionHandlers.Add(TEXT("add_blueprint_get_subsystem_node"), MakeShared<FAddBlueprintGetSubsystemNodeAction>());

	// =========================================================================
	// Node Actions - Blueprint Function Graph
	// =========================================================================
	ActionHandlers.Add(TEXT("create_blueprint_function"), MakeShared<FCreateBlueprintFunctionAction>());

	// =========================================================================
	// Node Actions - Event Dispatchers
	// =========================================================================
	ActionHandlers.Add(TEXT("add_event_dispatcher"), MakeShared<FAddEventDispatcherAction>());
	ActionHandlers.Add(TEXT("call_event_dispatcher"), MakeShared<FCallEventDispatcherAction>());
	ActionHandlers.Add(TEXT("bind_event_dispatcher"), MakeShared<FBindEventDispatcherAction>());
	ActionHandlers.Add(TEXT("create_event_delegate"), MakeShared<FCreateEventDelegateAction>());
	ActionHandlers.Add(TEXT("bind_component_event"), MakeShared<FBindComponentEventAction>());

	// =========================================================================
	// Node Actions - Spawn Actor Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_spawn_actor_from_class_node"), MakeShared<FAddSpawnActorFromClassNodeAction>());
	ActionHandlers.Add(TEXT("call_blueprint_function"), MakeShared<FCallBlueprintFunctionAction>());

	// =========================================================================
	// Node Actions - External Object Property Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("set_object_property"), MakeShared<FSetObjectPropertyAction>());

	// =========================================================================
	// Node Actions - Sequence Node
	// =========================================================================
	ActionHandlers.Add(TEXT("add_sequence_node"), MakeShared<FAddSequenceNodeAction>());

	// =========================================================================
	// Node Actions - Macro Instance Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_macro_instance_node"), MakeShared<FAddMacroInstanceNodeAction>());

	// =========================================================================
	// Node Actions - Struct Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_make_struct_node"), MakeShared<FAddMakeStructNodeAction>());
	ActionHandlers.Add(TEXT("add_break_struct_node"), MakeShared<FAddBreakStructNodeAction>());

	// =========================================================================
	// Node Actions - Switch Nodes
	// =========================================================================
	ActionHandlers.Add(TEXT("add_switch_on_string_node"), MakeShared<FAddSwitchOnStringNodeAction>());
	ActionHandlers.Add(TEXT("add_switch_on_int_node"), MakeShared<FAddSwitchOnIntNodeAction>());
	ActionHandlers.Add(TEXT("add_function_local_variable"), MakeShared<FAddFunctionLocalVariableAction>());
	ActionHandlers.Add(TEXT("set_blueprint_variable_default"), MakeShared<FSetBlueprintVariableDefaultAction>());
	ActionHandlers.Add(TEXT("add_blueprint_comment"), MakeShared<FAddBlueprintCommentAction>());
	ActionHandlers.Add(TEXT("auto_comment"), MakeShared<FAutoCommentAction>());

	// =========================================================================
	// Node Actions - P1: Variable & Function Management
	// =========================================================================
	ActionHandlers.Add(TEXT("delete_blueprint_variable"), MakeShared<FDeleteBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_variable"), MakeShared<FRenameBlueprintVariableAction>());
	ActionHandlers.Add(TEXT("set_variable_metadata"), MakeShared<FSetVariableMetadataAction>());
	ActionHandlers.Add(TEXT("delete_blueprint_function"), MakeShared<FDeleteBlueprintFunctionAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_function"), MakeShared<FRenameBlueprintFunctionAction>());
	ActionHandlers.Add(TEXT("rename_blueprint_macro"), MakeShared<FRenameBlueprintMacroAction>());

	// =========================================================================
	// Node Actions - P2: Graph Operation Enhancements
	// =========================================================================
	ActionHandlers.Add(TEXT("disconnect_blueprint_pin"), MakeShared<FDisconnectBlueprintPinAction>());
	ActionHandlers.Add(TEXT("move_node"), MakeShared<FMoveNodeAction>());
	ActionHandlers.Add(TEXT("add_reroute_node"), MakeShared<FAddRerouteNodeAction>());

	// =========================================================================
	// P3: Graph Patch System
	// =========================================================================
	ActionHandlers.Add(TEXT("describe_graph_enhanced"), MakeShared<FGraphDescribeEnhancedAction>());
	ActionHandlers.Add(TEXT("apply_graph_patch"), MakeShared<FApplyPatchAction>());
	ActionHandlers.Add(TEXT("validate_graph_patch"), MakeShared<FValidatePatchAction>());

	// =========================================================================
	// P4: Cross-Graph Node Transfer
	// =========================================================================
	ActionHandlers.Add(TEXT("export_nodes_to_text"), MakeShared<FExportNodesToTextAction>());
	ActionHandlers.Add(TEXT("import_nodes_from_text"), MakeShared<FImportNodesFromTextAction>());

	// =========================================================================
	// Project Actions (Input Mappings, Enhanced Input)
	// =========================================================================
	ActionHandlers.Add(TEXT("create_input_mapping"), MakeShared<FCreateInputMappingAction>());
	ActionHandlers.Add(TEXT("create_input_action"), MakeShared<FCreateInputActionAction>());
	ActionHandlers.Add(TEXT("create_input_mapping_context"), MakeShared<FCreateInputMappingContextAction>());
	ActionHandlers.Add(TEXT("add_key_mapping_to_context"), MakeShared<FAddKeyMappingToContextAction>());

	// =========================================================================
	// UMG Actions (Widget Blueprints)
	// =========================================================================
	ActionHandlers.Add(TEXT("create_umg_widget_blueprint"), MakeShared<FCreateUMGWidgetBlueprintAction>());
	ActionHandlers.Add(TEXT("add_text_block_to_widget"), MakeShared<FAddTextBlockToWidgetAction>());
	ActionHandlers.Add(TEXT("add_button_to_widget"), MakeShared<FAddButtonToWidgetAction>());
	ActionHandlers.Add(TEXT("add_image_to_widget"), MakeShared<FAddImageToWidgetAction>());
	ActionHandlers.Add(TEXT("add_border_to_widget"), MakeShared<FAddBorderToWidgetAction>());
	ActionHandlers.Add(TEXT("add_overlay_to_widget"), MakeShared<FAddOverlayToWidgetAction>());
	ActionHandlers.Add(TEXT("add_horizontal_box_to_widget"), MakeShared<FAddHorizontalBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_vertical_box_to_widget"), MakeShared<FAddVerticalBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_slider_to_widget"), MakeShared<FAddSliderToWidgetAction>());
	ActionHandlers.Add(TEXT("add_progress_bar_to_widget"), MakeShared<FAddProgressBarToWidgetAction>());
	ActionHandlers.Add(TEXT("add_size_box_to_widget"), MakeShared<FAddSizeBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_scale_box_to_widget"), MakeShared<FAddScaleBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_canvas_panel_to_widget"), MakeShared<FAddCanvasPanelToWidgetAction>());
	ActionHandlers.Add(TEXT("add_combo_box_to_widget"), MakeShared<FAddComboBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_check_box_to_widget"), MakeShared<FAddCheckBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_spin_box_to_widget"), MakeShared<FAddSpinBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("add_editable_text_box_to_widget"), MakeShared<FAddEditableTextBoxToWidgetAction>());
	ActionHandlers.Add(TEXT("bind_widget_event"), MakeShared<FBindWidgetEventAction>());
	ActionHandlers.Add(TEXT("add_widget_to_viewport"), MakeShared<FAddWidgetToViewportAction>());
	ActionHandlers.Add(TEXT("set_text_block_binding"), MakeShared<FSetTextBlockBindingAction>());
	ActionHandlers.Add(TEXT("list_widget_components"), MakeShared<FListWidgetComponentsAction>());
	ActionHandlers.Add(TEXT("reparent_widgets"), MakeShared<FReparentWidgetsAction>());
	ActionHandlers.Add(TEXT("set_widget_properties"), MakeShared<FSetWidgetPropertiesAction>());
	ActionHandlers.Add(TEXT("get_widget_tree"), MakeShared<FGetWidgetTreeAction>());
	ActionHandlers.Add(TEXT("delete_widget_from_blueprint"), MakeShared<FDeleteWidgetFromBlueprintAction>());
	ActionHandlers.Add(TEXT("rename_widget_in_blueprint"), MakeShared<FRenameWidgetInBlueprintAction>());
	ActionHandlers.Add(TEXT("add_widget_child"), MakeShared<FAddWidgetChildAction>());
	ActionHandlers.Add(TEXT("delete_umg_widget_blueprint"), MakeShared<FDeleteUMGWidgetBlueprintAction>());
	ActionHandlers.Add(TEXT("set_combo_box_options"), MakeShared<FSetComboBoxOptionsAction>());
	ActionHandlers.Add(TEXT("set_widget_text"), MakeShared<FSetWidgetTextAction>());
	ActionHandlers.Add(TEXT("set_slider_properties"), MakeShared<FSetSliderPropertiesAction>());
	ActionHandlers.Add(TEXT("add_generic_widget_to_widget"), MakeShared<FAddGenericWidgetAction>());

	// MVVM Actions
	ActionHandlers.Add(TEXT("mvvm_add_viewmodel"), MakeShared<FMVVMAddViewModelAction>());
	ActionHandlers.Add(TEXT("mvvm_add_binding"), MakeShared<FMVVMAddBindingAction>());
	ActionHandlers.Add(TEXT("mvvm_get_bindings"), MakeShared<FMVVMGetBindingsAction>());
	ActionHandlers.Add(TEXT("mvvm_remove_binding"), MakeShared<FMVVMRemoveBindingAction>());
	ActionHandlers.Add(TEXT("mvvm_remove_viewmodel"), MakeShared<FMVVMRemoveViewModelAction>());

	// =========================================================================
	// Material Actions (retained: analysis, diagnostics, layout, summary)
	// =========================================================================
	ActionHandlers.Add(TEXT("set_material_property"), MakeShared<FSetMaterialPropertyAction>());
	ActionHandlers.Add(TEXT("get_material_summary"), MakeShared<FGetMaterialSummaryAction>());
	ActionHandlers.Add(TEXT("remove_material_expression"), MakeShared<FRemoveMaterialExpressionAction>());
	ActionHandlers.Add(TEXT("auto_layout_material"), MakeShared<FAutoLayoutMaterialAction>());
	ActionHandlers.Add(TEXT("auto_comment_material"), MakeShared<FAutoCommentMaterialAction>());
	ActionHandlers.Add(TEXT("get_material_selected_nodes"), MakeShared<FGetMaterialSelectedNodesAction>());
	ActionHandlers.Add(TEXT("refresh_material_editor"), MakeShared<FRefreshMaterialEditorAction>());
	ActionHandlers.Add(TEXT("analyze_material_complexity"),    MakeShared<FAnalyzeMaterialComplexityAction>());
	ActionHandlers.Add(TEXT("analyze_material_dependencies"),  MakeShared<FAnalyzeMaterialDependenciesAction>());
	ActionHandlers.Add(TEXT("diagnose_material"),              MakeShared<FDiagnoseMaterialAction>());
	ActionHandlers.Add(TEXT("diff_materials"),                 MakeShared<FDiffMaterialsAction>());
	ActionHandlers.Add(TEXT("extract_material_parameters"),    MakeShared<FExtractMaterialParametersAction>());
	ActionHandlers.Add(TEXT("batch_create_material_instances"),MakeShared<FBatchCreateMaterialInstancesAction>());
	ActionHandlers.Add(TEXT("replace_material_node"),          MakeShared<FReplaceMaterialNodeAction>());

	// =========================================================================
	// Diff Actions (Source Control)
	// =========================================================================
	ActionHandlers.Add(TEXT("diff_against_depot"), MakeShared<FDiffAgainstDepotAction>());
	ActionHandlers.Add(TEXT("get_asset_history"), MakeShared<FGetAssetHistoryAction>());

	// =========================================================================
	// P6: Log Enhancement Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("clear_logs"), MakeShared<FClearLogsAction>());
	ActionHandlers.Add(TEXT("assert_log"), MakeShared<FAssertLogAction>());

	// =========================================================================
	// AnimGraph Actions (Animation Blueprint read/create/modify/compile)
	// =========================================================================
	ActionHandlers.Add(TEXT("list_animgraph_graphs"),        MakeShared<FListAnimGraphGraphsAction>());
	ActionHandlers.Add(TEXT("describe_animgraph_topology"),  MakeShared<FDescribeAnimGraphTopologyAction>());
	ActionHandlers.Add(TEXT("get_state_machine_structure"),  MakeShared<FGetStateMachineStructureAction>());
	ActionHandlers.Add(TEXT("get_state_subgraph"),           MakeShared<FGetStateSubgraphAction>());
	ActionHandlers.Add(TEXT("get_transition_rule"),          MakeShared<FGetTransitionRuleAction>());
	ActionHandlers.Add(TEXT("create_anim_blueprint"),        MakeShared<FCreateAnimBlueprintAction>());
	ActionHandlers.Add(TEXT("add_state_machine"),            MakeShared<FAddStateMachineAction>());
	ActionHandlers.Add(TEXT("add_animgraph_state"),          MakeShared<FAddStateAction>());
	ActionHandlers.Add(TEXT("remove_animgraph_state"),       MakeShared<FRemoveStateAction>());
	ActionHandlers.Add(TEXT("add_transition_rule"),          MakeShared<FAddTransitionRuleAction>());
	ActionHandlers.Add(TEXT("remove_transition_rule"),       MakeShared<FRemoveTransitionRuleAction>());
	ActionHandlers.Add(TEXT("add_anim_node"),                MakeShared<FAddAnimNodeAction>());
	ActionHandlers.Add(TEXT("set_anim_node_property"),       MakeShared<FSetAnimNodePropertyAction>());
	ActionHandlers.Add(TEXT("connect_anim_nodes"),           MakeShared<FConnectAnimNodesAction>());
	ActionHandlers.Add(TEXT("disconnect_anim_node"),         MakeShared<FDisconnectAnimNodeAction>());
	ActionHandlers.Add(TEXT("rename_animgraph_state"),       MakeShared<FRenameStateAction>());
	ActionHandlers.Add(TEXT("set_transition_priority"),      MakeShared<FSetTransitionPriorityAction>());
	ActionHandlers.Add(TEXT("compile_anim_blueprint"),       MakeShared<FCompileAnimBlueprintAction>());

	// =========================================================================
	// Python Execution Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("exec_python"), MakeShared<FExecPythonAction>());

	// =========================================================================
	// P7: Undo/Redo Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("undo"), MakeShared<FUndoAction>());
	ActionHandlers.Add(TEXT("redo"), MakeShared<FRedoAction>());
	ActionHandlers.Add(TEXT("get_undo_history"), MakeShared<FGetUndoHistoryAction>());

	// =========================================================================
	// P7: Viewport Screenshot Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("take_screenshot"), MakeShared<FTakeViewportScreenshotAction>());
	ActionHandlers.Add(TEXT("take_pie_screenshot"), MakeShared<FTakePIEScreenshotAction>());

	// =========================================================================
	// P8: Niagara Particle System Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("create_niagara_system"), MakeShared<FCreateNiagaraSystemAction>());
	ActionHandlers.Add(TEXT("describe_niagara_system"), MakeShared<FDescribeNiagaraSystemAction>());
	ActionHandlers.Add(TEXT("add_niagara_emitter"), MakeShared<FAddNiagaraEmitterAction>());
	ActionHandlers.Add(TEXT("remove_niagara_emitter"), MakeShared<FRemoveNiagaraEmitterAction>());
	ActionHandlers.Add(TEXT("set_niagara_module_param"), MakeShared<FSetNiagaraModuleParamAction>());
	ActionHandlers.Add(TEXT("compile_niagara_system"), MakeShared<FCompileNiagaraSystemAction>());
	ActionHandlers.Add(TEXT("get_niagara_modules"), MakeShared<FGetNiagaraModulesAction>());

	// =========================================================================
	// P8: DataTable Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("create_datatable"), MakeShared<FCreateDataTableAction>());
	ActionHandlers.Add(TEXT("describe_datatable"), MakeShared<FDescribeDataTableAction>());
	ActionHandlers.Add(TEXT("add_datatable_row"), MakeShared<FAddDataTableRowAction>());
	ActionHandlers.Add(TEXT("delete_datatable_row"), MakeShared<FDeleteDataTableRowAction>());
	ActionHandlers.Add(TEXT("export_datatable_json"), MakeShared<FExportDataTableJsonAction>());

	// =========================================================================
	// P8: Sequencer Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("create_level_sequence"), MakeShared<FCreateLevelSequenceAction>());
	ActionHandlers.Add(TEXT("describe_level_sequence"), MakeShared<FDescribeLevelSequenceAction>());
	ActionHandlers.Add(TEXT("add_sequencer_possessable"), MakeShared<FAddSequencerPossessableAction>());
	ActionHandlers.Add(TEXT("add_sequencer_track"), MakeShared<FAddSequencerTrackAction>());
	ActionHandlers.Add(TEXT("set_sequencer_range"), MakeShared<FSetSequencerRangeAction>());

	// =========================================================================
	// P10: Testing Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("run_automation_test"), MakeShared<FRunAutomationTestAction>());
	ActionHandlers.Add(TEXT("list_automation_tests"), MakeShared<FListAutomationTestsAction>());

	// =========================================================================
	// P10: Level Design Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("list_sublevels"), MakeShared<FListSublevelsAction>());
	ActionHandlers.Add(TEXT("get_world_settings"), MakeShared<FGetWorldSettingsAction>());

	// =========================================================================
	// P10: Profiler Actions
	// =========================================================================
	ActionHandlers.Add(TEXT("get_frame_stats"), MakeShared<FGetFrameStatsAction>());
	ActionHandlers.Add(TEXT("get_memory_stats"), MakeShared<FGetMemoryStatsAction>());

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Registered %d action handlers"), ActionHandlers.Num());
}

TSharedRef<FEditorAction>* UMCPBridge::FindAction(const FString& CommandType)
{
	return ActionHandlers.Find(CommandType);
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// =========================================================================
	// Action Handlers (modular actions - check these first)
	// =========================================================================
	TSharedRef<FEditorAction>* ActionPtr = FindAction(CommandType);
	if (ActionPtr)
	{
		return (*ActionPtr)->Execute(Params, Context);
	}

	// =========================================================================
	// Unknown Command (all handlers should be registered as actions now)
	// =========================================================================
	return CreateErrorResponse(
		FString::Printf(TEXT("Unknown command type: %s"), *CommandType),
		TEXT("unknown_command")
	);
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommandSafe(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// Phase 2: Top-level C++ exception guard around command execution.
	// SEH is handled per-action inside FEditorAction::ExecuteWithCrashProtection.
	try
	{
		return ExecuteCommandInternal(CommandType, Params);
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogMCP, Error, TEXT("C++ exception in command '%s': %hs"), *CommandType, Ex.what());
		return CreateErrorResponse(
			FString::Printf(TEXT("C++ exception: %hs"), Ex.what()),
			TEXT("cpp_exception")
		);
	}
	catch (...)
	{
		UE_LOG(LogMCP, Error, TEXT("Unknown C++ exception in command '%s'"), *CommandType);
		return CreateErrorResponse(TEXT("Unknown C++ exception"), TEXT("cpp_exception"));
	}
}

TSharedPtr<FJsonObject> UMCPBridge::ExecuteCommandInternal(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	return ExecuteCommand(CommandType, Params);
}

TSharedPtr<FJsonObject> UMCPBridge::CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("success"));
	Response->SetBoolField(TEXT("success"), true);

	if (ResultData.IsValid())
	{
		Response->SetObjectField(TEXT("result"), ResultData);
	}
	else
	{
		Response->SetObjectField(TEXT("result"), MakeShared<FJsonObject>());
	}

	return Response;
}

TSharedPtr<FJsonObject> UMCPBridge::CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("error"));
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	Response->SetStringField(TEXT("error_type"), ErrorType);

	return Response;
}

// =========================================================================
// Async Task Management
// =========================================================================

FString UMCPBridge::SubmitAsyncTask(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	// Cleanup expired tasks opportunistically
	CleanupExpiredTasks();

	// Generate unique task ID
	FString TaskId = FGuid::NewGuid().ToString();

	// Register pending task
	{
		FScopeLock Lock(&AsyncTasksLock);
		FAsyncTaskEntry Entry;
		Entry.TaskId = TaskId;
		Entry.Status = TEXT("pending");
		AsyncTasks.Add(TaskId, Entry);
	}

	// Submit for game thread execution
	// Capture by value to avoid dangling references
	TWeakObjectPtr<UMCPBridge> WeakThis(this);
	FString CapturedTaskId = TaskId;
	FString CapturedCommand = CommandType;
	TSharedPtr<FJsonObject> CapturedParams = Params;

	AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedTaskId, CapturedCommand, CapturedParams]()
	{
		UMCPBridge* Bridge = WeakThis.Get();
		if (!Bridge)
		{
			return;
		}

		TSharedPtr<FJsonObject> Result;
		FString Status;

		try
		{
			Result = Bridge->ExecuteCommandSafe(CapturedCommand, CapturedParams);
			if (Result.IsValid() && Result->GetBoolField(TEXT("success")))
			{
				Status = TEXT("success");
			}
			else
			{
				Status = TEXT("error");
			}
		}
		catch (...)
		{
			Status = TEXT("error");
			Result = UMCPBridge::CreateErrorResponse(
				TEXT("Unhandled exception during async execution"),
				TEXT("cpp_exception")
			);
		}

		// Write back result
		FScopeLock Lock(&Bridge->AsyncTasksLock);
		FAsyncTaskEntry* Entry = Bridge->AsyncTasks.Find(CapturedTaskId);
		if (Entry)
		{
			Entry->Status = Status;
			Entry->Result = Result;
		}
	});

	return TaskId;
}

TSharedPtr<FJsonObject> UMCPBridge::GetTaskResult(const FString& TaskId)
{
	// Cleanup expired tasks opportunistically
	CleanupExpiredTasks();

	FScopeLock Lock(&AsyncTasksLock);

	FAsyncTaskEntry* Entry = AsyncTasks.Find(TaskId);
	if (!Entry)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Unknown or expired task_id: %s"), *TaskId),
			TEXT("unknown_task")
		);
	}

	if (Entry->Status == TEXT("pending"))
	{
		TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();
		ResultData->SetStringField(TEXT("task_id"), TaskId);
		ResultData->SetStringField(TEXT("status"), TEXT("pending"));
		return CreateSuccessResponse(ResultData);
	}

	// Task is complete - return result and remove entry
	TSharedPtr<FJsonObject> Result = Entry->Result;
	AsyncTasks.Remove(TaskId);

	// Add task_id to the result for reference
	if (Result.IsValid())
	{
		Result->SetStringField(TEXT("task_id"), TaskId);
	}

	return Result;
}

void UMCPBridge::CleanupExpiredTasks()
{
	FScopeLock Lock(&AsyncTasksLock);

	double CurrentTime = FPlatformTime::Seconds();
	TArray<FString> ExpiredKeys;

	for (auto& Pair : AsyncTasks)
	{
		if (CurrentTime - Pair.Value.CreatedTime > AsyncTaskTTL)
		{
			ExpiredKeys.Add(Pair.Key);
		}
	}

	for (const FString& Key : ExpiredKeys)
	{
		UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Cleaning up expired async task: %s"), *Key);
		AsyncTasks.Remove(Key);
	}
}

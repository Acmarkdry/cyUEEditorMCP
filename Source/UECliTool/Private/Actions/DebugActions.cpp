// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/DebugActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// ============================================================================
// set_breakpoint
// ============================================================================

bool FSetBreakpointAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BPName, NodeId;
	if (!GetRequiredString(Params, TEXT("blueprint_name"), BPName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("node_id"), NodeId, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FSetBreakpointAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BPName = Params->GetStringField(TEXT("blueprint_name"));
	FString NodeIdStr = Params->GetStringField(TEXT("node_id"));
	bool bEnabled = GetOptionalBool(Params, TEXT("enabled"), true);
	bool bRemove = GetOptionalBool(Params, TEXT("remove"), false);
	FString GraphName = GetOptionalString(Params, TEXT("graph_name"));

	FString Error;
	UBlueprint* Blueprint = FindBlueprint(BPName, Error);
	if (!Blueprint) return CreateErrorResponse(Error);

	// Parse GUID
	FGuid NodeGuid;
	if (!FGuid::Parse(NodeIdStr, NodeGuid))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Invalid node GUID: %s"), *NodeIdStr));
	}

	// Find the node across all graphs
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		if (!GraphName.IsEmpty() && Graph->GetName() != GraphName) continue;

		FoundNode = FindNode(Graph, NodeGuid, Error);
		if (FoundNode) break;
	}

	if (!FoundNode)
	{
		// Also check function graphs
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (!Graph) continue;
			FoundNode = FindNode(Graph, NodeGuid, Error);
			if (FoundNode) break;
		}
	}

	if (!FoundNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node '%s' not found in Blueprint '%s'"), *NodeIdStr, *BPName));
	}

	if (bRemove)
	{
		FKismetDebugUtilities::RemoveBreakpointFromNode(FoundNode, Blueprint);
	}
	else
	{
		// Create if not exists, then enable/disable
		FBlueprintBreakpoint* Existing = FKismetDebugUtilities::FindBreakpointForNode(FoundNode, Blueprint);
		if (!Existing)
		{
			FKismetDebugUtilities::CreateBreakpoint(Blueprint, FoundNode, bEnabled);
		}
		else
		{
			FKismetDebugUtilities::SetBreakpointEnabled(FoundNode, Blueprint, bEnabled);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BPName);
	Result->SetStringField(TEXT("node_id"), NodeIdStr);
	Result->SetStringField(TEXT("node_title"), FoundNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Result->SetStringField(TEXT("action"), bRemove ? TEXT("removed") : (bEnabled ? TEXT("enabled") : TEXT("disabled")));

	return CreateSuccessResponse(Result);
}

// ============================================================================
// list_breakpoints
// ============================================================================

TSharedPtr<FJsonObject> FListBreakpointsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BPName = GetOptionalString(Params, TEXT("blueprint_name"));

	TArray<TSharedPtr<FJsonValue>> BreakpointsArray;

	auto CollectBreakpoints = [&](UBlueprint* Blueprint)
	{
		if (!Blueprint) return;

		auto ScanGraph = [&](UEdGraph* Graph)
		{
			if (!Graph) return;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;

				// Check if this node has a breakpoint
				FBlueprintBreakpoint* BPBreak = FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
				if (BPBreak)
				{
					TSharedPtr<FJsonObject> BPObj = MakeShared<FJsonObject>();
					BPObj->SetStringField(TEXT("blueprint"), Blueprint->GetName());
					BPObj->SetStringField(TEXT("graph"), Graph->GetName());
					BPObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
					BPObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					BPObj->SetBoolField(TEXT("enabled"), FKismetDebugUtilities::IsBreakpointValid(*BPBreak));
					BreakpointsArray.Add(MakeShared<FJsonValueObject>(BPObj));
				}
			}
		};

		for (UEdGraph* Graph : Blueprint->UbergraphPages) ScanGraph(Graph);
		for (UEdGraph* Graph : Blueprint->FunctionGraphs) ScanGraph(Graph);
	};

	if (!BPName.IsEmpty())
	{
		FString Error;
		UBlueprint* Blueprint = FindBlueprint(BPName, Error);
		if (!Blueprint) return CreateErrorResponse(Error);
		CollectBreakpoints(Blueprint);
	}
	else
	{
		// Scan all loaded blueprints
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			CollectBreakpoints(*It);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("breakpoints"), BreakpointsArray);
	Result->SetNumberField(TEXT("count"), BreakpointsArray.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// get_watch_values
// ============================================================================

bool FGetWatchValuesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BPName;
	return GetRequiredString(Params, TEXT("blueprint_name"), BPName, OutError);
}

TSharedPtr<FJsonObject> FGetWatchValuesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BPName = Params->GetStringField(TEXT("blueprint_name"));

	// Check if PIE is running
	if (!GEditor || !GEditor->IsPlaySessionInProgress())
	{
		return CreateErrorResponse(TEXT("PIE not running — start with: start_pie"));
	}

	FString Error;
	UBlueprint* Blueprint = FindBlueprint(BPName, Error);
	if (!Blueprint) return CreateErrorResponse(Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BPName);
	Result->SetBoolField(TEXT("pie_running"), true);
	Result->SetStringField(TEXT("status"), TEXT("watch_values_retrieved"));

	// Collect variable values from the CDO
	if (Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		TArray<TSharedPtr<FJsonValue>> VarsArray;

		for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass, EFieldIterationFlags::None); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;

			TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
			VarObj->SetStringField(TEXT("name"), Prop->GetName());
			VarObj->SetStringField(TEXT("type"), Prop->GetCPPType());

			// Get default value as string
			FString DefaultValue;
			if (CDO)
			{
				const void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(CDO);
				Prop->ExportTextItem_Direct(DefaultValue, ValueAddr, nullptr, nullptr, PPF_None);
			}
			VarObj->SetStringField(TEXT("default_value"), DefaultValue);

			VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
		}
		Result->SetArrayField(TEXT("variables"), VarsArray);
		Result->SetNumberField(TEXT("variable_count"), VarsArray.Num());
	}

	return CreateSuccessResponse(Result);
}

// ============================================================================
// debug_step (EXPERIMENTAL)
// ============================================================================

bool FDebugStepAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Action;
	return GetRequiredString(Params, TEXT("action"), Action, OutError);
}

TSharedPtr<FJsonObject> FDebugStepAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Action = Params->GetStringField(TEXT("action"));

	if (!GEditor || !GEditor->IsPlaySessionInProgress())
	{
		return CreateErrorResponse(TEXT("PIE not running — start with: start_pie"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("status"), TEXT("experimental — debug step sent"));
	Result->SetStringField(TEXT("warning"), TEXT("debug_step is experimental and may cause instability"));

	return CreateSuccessResponse(Result);
}

// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/AnimGraphActions.h"
#include "MCPCommonUtils.h"
#include "MCPContext.h"

// Core UE
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"
#include "UObject/SavePackage.h"

// AnimGraph / AnimBlueprint
#include "Animation/AnimBlueprint.h"
#include "AnimBlueprintGraph.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_Root.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

// ============================================================================
// AnimGraphHelpers
// ============================================================================

namespace AnimGraphHelpers
{

bool ValidateAnimBlueprint(UBlueprint* Blueprint, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return false;
	}
	if (!Cast<UAnimBlueprint>(Blueprint))
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}
	return true;
}

UEdGraph* FindAnimSubGraph(UAnimBlueprint* AnimBP, const FString& GraphName, FString& OutError)
{
	if (!AnimBP)
	{
		OutError = TEXT("AnimBlueprint is null");
		return nullptr;
	}

	// Search FunctionGraphs (includes AnimGraph, state machine sub-graphs, etc.)
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	// Search UbergraphPages (EventGraph)
	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	// Recursively search sub-graphs of all nodes
	auto SearchSubGraphs = [&](UEdGraph* ParentGraph, auto& SelfRef) -> UEdGraph*
	{
		if (!ParentGraph) return nullptr;
		for (UEdGraphNode* Node : ParentGraph->Nodes)
		{
			if (!Node) continue;
			TArray<UEdGraph*> SubGraphs;
			Node->GetBoundGraphs(SubGraphs);
			for (UEdGraph* SubGraph : SubGraphs)
			{
				if (SubGraph && SubGraph->GetName() == GraphName)
				{
					return SubGraph;
				}
				UEdGraph* Found = SelfRef(SubGraph, SelfRef);
				if (Found) return Found;
			}
		}
		return nullptr;
	};

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		UEdGraph* Found = SearchSubGraphs(Graph, SearchSubGraphs);
		if (Found) return Found;
	}

	// Build available names for error message
	TArray<FString> AvailableNames;
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (Graph) AvailableNames.Add(Graph->GetName());
	}
	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		if (Graph) AvailableNames.Add(Graph->GetName());
	}

	OutError = FString::Printf(TEXT("Graph '%s' not found in '%s'. Available: [%s]"),
		*GraphName, *AnimBP->GetName(), *FString::Join(AvailableNames, TEXT(", ")));
	return nullptr;
}

UAnimStateNodeBase* FindStateNode(UEdGraph* StateMachineGraph, const FString& StateName, FString& OutError)
{
	if (!StateMachineGraph)
	{
		OutError = TEXT("State machine graph is null");
		return nullptr;
	}

	TArray<FString> AvailableNames;
	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
		if (StateNode)
		{
			FString NodeStateName = StateNode->GetStateName();
			AvailableNames.Add(NodeStateName);
			if (NodeStateName == StateName)
			{
				return StateNode;
			}
		}
	}

	OutError = FString::Printf(TEXT("State '%s' not found in state machine '%s'. Available: [%s]"),
		*StateName, *StateMachineGraph->GetName(), *FString::Join(AvailableNames, TEXT(", ")));
	return nullptr;
}

UAnimStateTransitionNode* FindTransitionNode(UEdGraph* StateMachineGraph,
	const FString& SourceState, const FString& TargetState, FString& OutError)
{
	if (!StateMachineGraph)
	{
		OutError = TEXT("State machine graph is null");
		return nullptr;
	}

	for (UEdGraphNode* Node : StateMachineGraph->Nodes)
	{
		UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
		if (!TransNode) continue;

		UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
		UAnimStateNodeBase* NextState = TransNode->GetNextState();

		FString PrevName = PrevState ? PrevState->GetStateName() : TEXT("");
		FString NextName = NextState ? NextState->GetStateName() : TEXT("");

		if (PrevName == SourceState && NextName == TargetState)
		{
			return TransNode;
		}
	}

	OutError = FString::Printf(TEXT("Transition from '%s' to '%s' not found in state machine '%s'"),
		*SourceState, *TargetState, *StateMachineGraph->GetName());
	return nullptr;
}

TSharedPtr<FJsonObject> SerializeAnimNode(const UEdGraphNode* Node, bool bCompact)
{
	if (!Node) return nullptr;

	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
	NodeObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
	NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

	if (!bCompact && !Node->NodeComment.IsEmpty())
	{
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
	}

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		if (bCompact && Pin->bHidden) continue;

		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
		PinObj->SetBoolField(TEXT("is_connected"), Pin->LinkedTo.Num() > 0);

		if (!bCompact)
		{
			PinObj->SetBoolField(TEXT("is_hidden"), Pin->bHidden);
			if (Pin->PinType.PinSubCategoryObject.IsValid())
			{
				PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
			}
		}

		if (Pin->LinkedTo.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> LinkedArray;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				TSharedPtr<FJsonObject> LinkedObj = MakeShared<FJsonObject>();
				LinkedObj->SetStringField(TEXT("node_id"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
				LinkedObj->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());
				LinkedArray.Add(MakeShared<FJsonValueObject>(LinkedObj));
			}
			PinObj->SetArrayField(TEXT("linked_to"), LinkedArray);
		}

		if (!Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		}

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	NodeObj->SetArrayField(TEXT("pins"), PinsArray);

	return NodeObj;
}

void ExtractAnimAssetReferences(const UEdGraphNode* Node, TSharedPtr<FJsonObject>& OutNodeObj)
{
	if (!Node || !OutNodeObj.IsValid()) return;

	// AnimSequence Player
	if (const UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
	{
		if (SeqPlayer->Node.Sequence)
		{
			OutNodeObj->SetStringField(TEXT("anim_sequence"), SeqPlayer->Node.Sequence->GetPathName());
		}
	}

	// BlendSpace Player
	if (const UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
	{
		if (BSPlayer->Node.BlendSpace)
		{
			OutNodeObj->SetStringField(TEXT("blend_space"), BSPlayer->Node.BlendSpace->GetPathName());
		}
	}
}

UAnimGraphNode_Base* CreateAnimNodeByType(UEdGraph* Graph, const FString& NodeType,
	FVector2D Position, const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("Graph is null");
		return nullptr;
	}

	UAnimGraphNode_Base* CreatedNode = nullptr;

	if (NodeType.Equals(TEXT("AnimSequencePlayer"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_SequencePlayer>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_SequencePlayer*) {});
	}
	else if (NodeType.Equals(TEXT("BlendSpacePlayer"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_BlendSpacePlayer>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_BlendSpacePlayer*) {});
	}
	else if (NodeType.Equals(TEXT("LayeredBlendPerBone"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_LayeredBoneBlend>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_LayeredBoneBlend*) {});
	}
	else if (NodeType.Equals(TEXT("TwoWayBlend"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_TwoWayBlend>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_TwoWayBlend*) {});
	}
	else if (NodeType.Equals(TEXT("BlendPosesByBool"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_BlendListByBool>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_BlendListByBool*) {});
	}
	else if (NodeType.Equals(TEXT("BlendPosesByInt"), ESearchCase::IgnoreCase))
	{
		CreatedNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_BlendListByInt>(
			Graph, Position, EK2NewNodeFlags::None, [](UAnimGraphNode_BlendListByInt*) {});
	}
	else
	{
		OutError = FString::Printf(
			TEXT("Unsupported anim node type: '%s'. Supported: [AnimSequencePlayer, BlendSpacePlayer, LayeredBlendPerBone, TwoWayBlend, BlendPosesByBool, BlendPosesByInt]"),
			*NodeType);
		return nullptr;
	}

	if (!CreatedNode)
	{
		OutError = FString::Printf(TEXT("Failed to create anim node of type '%s'"), *NodeType);
	}

	return CreatedNode;
}

} // namespace AnimGraphHelpers


// ============================================================================
// Helper: collect all sub-graphs recursively from a root graph
// ============================================================================

static void CollectAllSubGraphs(UEdGraph* RootGraph, TArray<UEdGraph*>& OutGraphs)
{
	if (!RootGraph) return;
	OutGraphs.Add(RootGraph);
	for (UEdGraphNode* Node : RootGraph->Nodes)
	{
		if (!Node) continue;
		TArray<UEdGraph*> SubGraphs;
		Node->GetBoundGraphs(SubGraphs);
		for (UEdGraph* Sub : SubGraphs)
		{
			if (Sub && !OutGraphs.Contains(Sub))
			{
				CollectAllSubGraphs(Sub, OutGraphs);
			}
		}
	}
}

// Helper: determine graph type string from graph class name
static FString GetGraphTypeString(UEdGraph* Graph)
{
	if (!Graph) return TEXT("Unknown");
	FString ClassName = Graph->GetClass()->GetName();
	if (ClassName.Contains(TEXT("AnimBlueprintGraph"))) return TEXT("AnimGraph");
	if (ClassName.Contains(TEXT("AnimationStateMachineGraph"))) return TEXT("StateMachine");
	if (ClassName.Contains(TEXT("AnimationStateGraph"))) return TEXT("StateGraph");
	if (ClassName.Contains(TEXT("AnimationTransitionGraph"))) return TEXT("TransitionGraph");
	if (ClassName.Contains(TEXT("UbergraphPage")) || Graph->GetName().Contains(TEXT("EventGraph"))) return TEXT("EventGraph");
	return TEXT("Graph");
}


// ============================================================================
// 3.3 — FListAnimGraphGraphsAction
// ============================================================================

bool FListAnimGraphGraphsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	return AnimGraphHelpers::ValidateAnimBlueprint(Blueprint, OutError);
}

TSharedPtr<FJsonObject> FListAnimGraphGraphsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		return CreateErrorResponse(TEXT("Blueprint is not an Animation Blueprint"), TEXT("invalid_type"));
	}

	TArray<TSharedPtr<FJsonValue>> GraphsArray;

	// Collect FunctionGraphs (AnimGraph, state machines, etc.)
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("graph_type"), GetGraphTypeString(Graph));
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	// Collect UbergraphPages (EventGraph)
	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("graph_name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("graph_type"), TEXT("EventGraph"));
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	// Skeleton reference
	FString SkeletonPath;
	if (AnimBP->TargetSkeleton)
	{
		SkeletonPath = AnimBP->TargetSkeleton->GetPathName();
	}

	// Parent class
	FString ParentClassName;
	if (AnimBP->ParentClass)
	{
		ParentClassName = AnimBP->ParentClass->GetName();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_name"), AnimBP->GetName());
	Result->SetStringField(TEXT("skeleton"), SkeletonPath);
	Result->SetStringField(TEXT("parent_class"), ParentClassName);
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// 3.4 — FDescribeAnimGraphTopologyAction
// ============================================================================

bool FDescribeAnimGraphTopologyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString GraphName;
	if (!GetRequiredString(Params, TEXT("graph_name"), GraphName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FDescribeAnimGraphTopologyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		return CreateErrorResponse(TEXT("Blueprint is not an Animation Blueprint"), TEXT("invalid_type"));
	}

	FString GraphName;
	FString Error;
	GetRequiredString(Params, TEXT("graph_name"), GraphName, Error);

	UEdGraph* TargetGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, GraphName, Error);
	if (!TargetGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	const bool bCompact = GetOptionalBool(Params, TEXT("compact"), false);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = AnimGraphHelpers::SerializeAnimNode(Node, bCompact);
		if (!NodeObj.IsValid()) continue;

		// Attach animation asset references
		AnimGraphHelpers::ExtractAnimAssetReferences(Node, NodeObj);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));

		// Collect edges from output pins
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
					*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
					*LinkedPin->GetOwningNode()->NodeGuid.ToString(), *LinkedPin->PinName.ToString());
				if (!SeenEdges.Contains(EdgeKey))
				{
					SeenEdges.Add(EdgeKey);
					TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
					EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
					EdgeObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
					EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
	Result->SetBoolField(TEXT("compact"), bCompact);
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());
	Result->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("edges"), EdgesArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// 3.5 — FGetStateMachineStructureAction
// ============================================================================

bool FGetStateMachineStructureAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FGetStateMachineStructureAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		return CreateErrorResponse(TEXT("Blueprint is not an Animation Blueprint"), TEXT("invalid_type"));
	}

	FString StateMachineName;
	FString Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	TArray<TSharedPtr<FJsonValue>> StatesArray;
	TArray<TSharedPtr<FJsonValue>> TransitionsArray;
	FString EntryStateName;

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (!Node) continue;

		// Entry node — identifies the default state
		if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(Node))
		{
			// The entry node connects to the default state via its output pin
			for (UEdGraphPin* Pin : EntryNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
				{
					UEdGraphNode* LinkedNode = Pin->LinkedTo[0]->GetOwningNode();
					if (UAnimStateNode* DefaultState = Cast<UAnimStateNode>(LinkedNode))
					{
						EntryStateName = DefaultState->GetStateName();
					}
				}
			}
			continue;
		}

		// State node
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			int32 SubgraphNodeCount = 0;
			if (StateNode->BoundGraph)
			{
				SubgraphNodeCount = StateNode->BoundGraph->Nodes.Num();
			}

			TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
			StateObj->SetStringField(TEXT("state_name"), StateNode->GetStateName());
			StateObj->SetStringField(TEXT("state_type"), TEXT("State"));
			StateObj->SetStringField(TEXT("node_guid"), StateNode->NodeGuid.ToString());
			StateObj->SetNumberField(TEXT("subgraph_node_count"), SubgraphNodeCount);
			StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
			continue;
		}

		// Transition node
		if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
		{
			UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransNode->GetNextState();

			TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
			TransObj->SetStringField(TEXT("source_state"), PrevState ? PrevState->GetStateName() : TEXT(""));
			TransObj->SetStringField(TEXT("target_state"), NextState ? NextState->GetStateName() : TEXT(""));
			TransObj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
			TransObj->SetNumberField(TEXT("priority"), TransNode->PriorityOrder);
			TransitionsArray.Add(MakeShared<FJsonValueObject>(TransObj));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("entry_state"), EntryStateName);
	Result->SetArrayField(TEXT("states"), StatesArray);
	Result->SetArrayField(TEXT("transitions"), TransitionsArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// 3.6 — FGetStateSubgraphAction
// ============================================================================

bool FGetStateSubgraphAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	FString StateName;
	if (!GetRequiredString(Params, TEXT("state_name"), StateName, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FGetStateSubgraphAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		return CreateErrorResponse(TEXT("Blueprint is not an Animation Blueprint"), TEXT("invalid_type"));
	}

	FString StateMachineName, StateName, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("state_name"), StateName, Error);

	// Find the state machine graph
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Find the state node
	UAnimStateNodeBase* StateNodeBase = AnimGraphHelpers::FindStateNode(SMGraph, StateName, Error);
	if (!StateNodeBase)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateNode* StateNode = Cast<UAnimStateNode>(StateNodeBase);
	if (!StateNode || !StateNode->BoundGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("State '%s' has no bound sub-graph"), *StateName),
			TEXT("not_found"));
	}

	UEdGraph* SubGraph = StateNode->BoundGraph;
	const bool bCompact = GetOptionalBool(Params, TEXT("compact"), false);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	for (UEdGraphNode* Node : SubGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = AnimGraphHelpers::SerializeAnimNode(Node, bCompact);
		if (!NodeObj.IsValid()) continue;

		// Annotate animation asset references (AnimSequence, BlendSpace)
		AnimGraphHelpers::ExtractAnimAssetReferences(Node, NodeObj);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));

		// Collect edges
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
					*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
					*LinkedPin->GetOwningNode()->NodeGuid.ToString(), *LinkedPin->PinName.ToString());
				if (!SeenEdges.Contains(EdgeKey))
				{
					SeenEdges.Add(EdgeKey);
					TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
					EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
					EdgeObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
					EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("subgraph_name"), SubGraph->GetName());
	Result->SetBoolField(TEXT("compact"), bCompact);
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());
	Result->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("edges"), EdgesArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// 3.7 — FGetTransitionRuleAction
// ============================================================================

bool FGetTransitionRuleAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	FString SourceState;
	if (!GetRequiredString(Params, TEXT("source_state"), SourceState, OutError))
	{
		return false;
	}

	FString TargetState;
	if (!GetRequiredString(Params, TEXT("target_state"), TargetState, OutError))
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FGetTransitionRuleAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		return CreateErrorResponse(TEXT("Blueprint is not an Animation Blueprint"), TEXT("invalid_type"));
	}

	FString StateMachineName, SourceState, TargetState, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("source_state"), SourceState, Error);
	GetRequiredString(Params, TEXT("target_state"), TargetState, Error);

	// Find the state machine graph
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Find the transition node
	UAnimStateTransitionNode* TransNode = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, Error);
	if (!TransNode)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	if (!TransNode->BoundGraph)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Transition '%s' -> '%s' has no bound condition graph"), *SourceState, *TargetState),
			TEXT("not_found"));
	}

	UEdGraph* CondGraph = TransNode->BoundGraph;
	const bool bCompact = GetOptionalBool(Params, TEXT("compact"), false);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TSet<FString> SeenEdges;

	// Collect variable references used in the condition graph
	TArray<FString> ReferencedVariables;

	for (UEdGraphNode* Node : CondGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = AnimGraphHelpers::SerializeAnimNode(Node, bCompact);
		if (!NodeObj.IsValid()) continue;

		// Annotate blueprint variable references (VariableGet nodes)
		FString NodeClass = Node->GetClass()->GetName();
		if (NodeClass.Contains(TEXT("VariableGet")) || NodeClass.Contains(TEXT("K2Node_VariableGet")))
		{
			// Try to get variable name from node title
			FString VarName = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (!VarName.IsEmpty() && !ReferencedVariables.Contains(VarName))
			{
				ReferencedVariables.Add(VarName);
			}
			NodeObj->SetStringField(TEXT("variable_name"), VarName);
		}

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));

		// Collect edges
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				FString EdgeKey = FString::Printf(TEXT("%s:%s->%s:%s"),
					*Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
					*LinkedPin->GetOwningNode()->NodeGuid.ToString(), *LinkedPin->PinName.ToString());
				if (!SeenEdges.Contains(EdgeKey))
				{
					SeenEdges.Add(EdgeKey);
					TSharedPtr<FJsonObject> EdgeObj = MakeShared<FJsonObject>();
					EdgeObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
					EdgeObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
					EdgeObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
					EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeObj));
				}
			}
		}
	}

	// Build referenced_variables array
	TArray<TSharedPtr<FJsonValue>> VarRefsArray;
	for (const FString& VarName : ReferencedVariables)
	{
		VarRefsArray.Add(MakeShared<FJsonValueString>(VarName));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("source_state"), SourceState);
	Result->SetStringField(TEXT("target_state"), TargetState);
	Result->SetStringField(TEXT("condition_graph_name"), CondGraph->GetName());
	Result->SetBoolField(TEXT("compact"), bCompact);
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());
	Result->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	Result->SetArrayField(TEXT("edges"), EdgesArray);
	Result->SetArrayField(TEXT("referenced_variables"), VarRefsArray);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.1 — FCreateAnimBlueprintAction
// ============================================================================

bool FCreateAnimBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError))
	{
		return false;
	}

	FString SkeletonPath;
	if (!GetRequiredString(Params, TEXT("skeleton"), SkeletonPath, OutError))
	{
		return false;
	}

	// Validate skeleton asset exists
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Skeleton asset not found: '%s'"), *SkeletonPath);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FCreateAnimBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name, SkeletonPath, Error;
	GetRequiredString(Params, TEXT("name"), Name, Error);
	GetRequiredString(Params, TEXT("skeleton"), SkeletonPath, Error);

	FString PackagePath = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Blueprints"));
	if (!PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath += TEXT("/");
	}

	FString ParentClassName = GetOptionalString(Params, TEXT("parent_class"), TEXT("AnimInstance"));

	// Load skeleton
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Skeleton asset not found: '%s'"), *SkeletonPath),
			TEXT("not_found"));
	}

	// Resolve parent class (default AnimInstance)
	UClass* ParentClass = UAnimInstance::StaticClass();
	if (!ParentClassName.IsEmpty() && !ParentClassName.Equals(TEXT("AnimInstance"), ESearchCase::IgnoreCase))
	{
		UClass* ResolvedClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
		if (!ResolvedClass)
		{
			ResolvedClass = LoadClass<UAnimInstance>(nullptr,
				*FString::Printf(TEXT("/Script/Engine.%s"), *ParentClassName));
		}
		if (ResolvedClass)
		{
			ParentClass = ResolvedClass;
		}
	}

	// Create factory and package
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = ParentClass;

	UPackage* Package = CreatePackage(*(PackagePath + Name));
	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(),
		Package,
		*Name,
		RF_Standalone | RF_Public,
		nullptr,
		GWarn
	));

	if (!NewAnimBP)
	{
		return CreateErrorResponse(TEXT("Failed to create Animation Blueprint"), TEXT("creation_failed"));
	}

	FAssetRegistryModule::AssetCreated(NewAnimBP);
	Package->MarkPackageDirty();
	Context.MarkPackageDirty(Package);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Created AnimBlueprint '%s' with skeleton '%s'"),
		*Name, *Skeleton->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("path"), PackagePath + Name);
	Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.2 — FAddStateMachineAction
// ============================================================================

bool FAddStateMachineAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	if (!AnimGraphHelpers::ValidateAnimBlueprint(Blueprint, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	// Check for duplicate state machine name
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	FString DummyError;
	UEdGraph* Existing = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (Existing)
	{
		OutError = FString::Printf(TEXT("State machine '%s' already exists"), *StateMachineName);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FAddStateMachineAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);

	FVector2D Position = GetNodePosition(Params);

	// Find the AnimGraph (first FunctionGraph of type AnimBlueprintGraph)
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (Graph && Graph->GetClass()->GetName().Contains(TEXT("AnimBlueprintGraph")))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (!AnimGraph && AnimBP->FunctionGraphs.Num() > 0)
	{
		AnimGraph = AnimBP->FunctionGraphs[0];
	}

	if (!AnimGraph)
	{
		return CreateErrorResponse(TEXT("Could not find AnimGraph in Animation Blueprint"), TEXT("not_found"));
	}

	// Spawn the state machine node
	UAnimGraphNode_StateMachine* SMNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimGraphNode_StateMachine>(
		AnimGraph, Position, EK2NewNodeFlags::None,
		[&StateMachineName](UAnimGraphNode_StateMachine* Node)
		{
			// The state machine name is set on the bound graph after creation
		}
	);

	if (!SMNode)
	{
		return CreateErrorResponse(TEXT("Failed to create state machine node"), TEXT("creation_failed"));
	}

	// Rename the bound state machine graph
	if (SMNode->EditorStateMachineGraph)
	{
		SMNode->EditorStateMachineGraph->Rename(*StateMachineName, nullptr,
			REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
	}

	MarkBlueprintModified(Blueprint, Context);

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Added state machine '%s' to AnimBlueprint '%s'"),
		*StateMachineName, *Blueprint->GetName());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	Result->SetStringField(TEXT("node_guid"), SMNode->NodeGuid.ToString());
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.3 — FAddStateAction
// ============================================================================

bool FAddStateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	FString StateName;
	if (!GetRequiredString(Params, TEXT("state_name"), StateName, OutError))
	{
		return false;
	}

	// Check for duplicate state name
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (SMGraph)
	{
		UAnimStateNodeBase* Existing = AnimGraphHelpers::FindStateNode(SMGraph, StateName, DummyError);
		if (Existing)
		{
			OutError = FString::Printf(TEXT("State '%s' already exists in state machine '%s'"),
				*StateName, *StateMachineName);
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FAddStateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, StateName, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("state_name"), StateName, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	FVector2D Position = GetNodePosition(Params);

	UAnimStateNode* NewState = FEdGraphSchemaAction_K2NewNode::SpawnNode<UAnimStateNode>(
		SMGraph, Position, EK2NewNodeFlags::None,
		[&StateName](UAnimStateNode* Node)
		{
			Node->SetStateName(FName(*StateName));
		}
	);

	if (!NewState)
	{
		return CreateErrorResponse(TEXT("Failed to create state node"), TEXT("creation_failed"));
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("node_guid"), NewState->NodeGuid.ToString());
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.4 — FRemoveStateAction
// ============================================================================

bool FRemoveStateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError))
	{
		return false;
	}

	FString StateName;
	if (!GetRequiredString(Params, TEXT("state_name"), StateName, OutError))
	{
		return false;
	}

	// Check if it's the entry state (cannot remove)
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (SMGraph)
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(Node))
			{
				for (UEdGraphPin* Pin : EntryNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
					{
						UEdGraphNode* LinkedNode = Pin->LinkedTo[0]->GetOwningNode();
						if (UAnimStateNode* DefaultState = Cast<UAnimStateNode>(LinkedNode))
						{
							if (DefaultState->GetStateName() == StateName)
							{
								OutError = FString::Printf(TEXT("Cannot remove entry state '%s'"), *StateName);
								return false;
							}
						}
					}
				}
			}
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FRemoveStateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, StateName, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("state_name"), StateName, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateNodeBase* StateNodeBase = AnimGraphHelpers::FindStateNode(SMGraph, StateName, Error);
	if (!StateNodeBase)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Break all pin links (disconnects associated transitions)
	StateNodeBase->BreakAllNodeLinks();

	// Remove from graph
	SMGraph->RemoveNode(StateNodeBase);

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("state_machine_name"), StateMachineName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.5 — FAddTransitionRuleAction
// ============================================================================

bool FAddTransitionRuleAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName, SourceState, TargetState;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_state"), SourceState, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_state"), TargetState, OutError)) return false;

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (!SMGraph)
	{
		OutError = FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName);
		return false;
	}

	// Verify source and target states exist
	if (!AnimGraphHelpers::FindStateNode(SMGraph, SourceState, OutError)) return false;
	if (!AnimGraphHelpers::FindStateNode(SMGraph, TargetState, OutError)) return false;

	// Check for duplicate transition
	UAnimStateTransitionNode* Existing = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, DummyError);
	if (Existing)
	{
		OutError = FString::Printf(TEXT("Transition from '%s' to '%s' already exists"), *SourceState, *TargetState);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FAddTransitionRuleAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, SourceState, TargetState, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("source_state"), SourceState, Error);
	GetRequiredString(Params, TEXT("target_state"), TargetState, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateNodeBase* SrcNode = AnimGraphHelpers::FindStateNode(SMGraph, SourceState, Error);
	UAnimStateNodeBase* DstNode = AnimGraphHelpers::FindStateNode(SMGraph, TargetState, Error);
	if (!SrcNode || !DstNode)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Use the graph schema to create the transition connection
	const UEdGraphSchema* Schema = SMGraph->GetSchema();
	if (!Schema)
	{
		return CreateErrorResponse(TEXT("State machine graph has no schema"), TEXT("error"));
	}

	// Find output pin on source and input pin on destination
	UEdGraphPin* SrcOutputPin = nullptr;
	UEdGraphPin* DstInputPin = nullptr;

	for (UEdGraphPin* Pin : SrcNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			SrcOutputPin = Pin;
			break;
		}
	}
	for (UEdGraphPin* Pin : DstNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			DstInputPin = Pin;
			break;
		}
	}

	if (!SrcOutputPin || !DstInputPin)
	{
		return CreateErrorResponse(TEXT("Could not find connection pins on state nodes"), TEXT("error"));
	}

	FPinConnectionResponse Response = Schema->CanCreateConnection(SrcOutputPin, DstInputPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Cannot create transition: %s"), *Response.Message.ToString()),
			TEXT("error"));
	}

	Schema->TryCreateConnection(SrcOutputPin, DstInputPin);

	// Find the newly created transition node
	UAnimStateTransitionNode* NewTransition = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, Error);

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_state"), SourceState);
	Result->SetStringField(TEXT("target_state"), TargetState);
	if (NewTransition)
	{
		Result->SetStringField(TEXT("transition_guid"), NewTransition->NodeGuid.ToString());
	}
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.6 — FRemoveTransitionRuleAction
// ============================================================================

bool FRemoveTransitionRuleAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName, SourceState, TargetState;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_state"), SourceState, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_state"), TargetState, OutError)) return false;

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (!SMGraph)
	{
		OutError = FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName);
		return false;
	}

	UAnimStateTransitionNode* TransNode = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, OutError);
	if (!TransNode)
	{
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FRemoveTransitionRuleAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, SourceState, TargetState, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("source_state"), SourceState, Error);
	GetRequiredString(Params, TEXT("target_state"), TargetState, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateTransitionNode* TransNode = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, Error);
	if (!TransNode)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	TransNode->BreakAllNodeLinks();
	SMGraph->RemoveNode(TransNode);

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_state"), SourceState);
	Result->SetStringField(TEXT("target_state"), TargetState);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.7 — FAddAnimNodeAction
// ============================================================================

static const TArray<FString>& GetSupportedAnimNodeTypes()
{
	static TArray<FString> Types = {
		TEXT("AnimSequencePlayer"),
		TEXT("BlendSpacePlayer"),
		TEXT("LayeredBlendPerBone"),
		TEXT("TwoWayBlend"),
		TEXT("BlendPosesByBool"),
		TEXT("BlendPosesByInt")
	};
	return Types;
}

bool FAddAnimNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString GraphName;
	if (!GetRequiredString(Params, TEXT("graph_name"), GraphName, OutError)) return false;

	FString NodeType;
	if (!GetRequiredString(Params, TEXT("node_type"), NodeType, OutError)) return false;

	// Validate node type
	const TArray<FString>& Supported = GetSupportedAnimNodeTypes();
	bool bFound = false;
	for (const FString& T : Supported)
	{
		if (T.Equals(NodeType, ESearchCase::IgnoreCase)) { bFound = true; break; }
	}
	if (!bFound)
	{
		OutError = FString::Printf(
			TEXT("Unsupported anim node type: '%s'. Supported: [%s]"),
			*NodeType, *FString::Join(Supported, TEXT(", ")));
		return false;
	}

	// Validate anim_asset if provided
	FString AnimAsset = GetOptionalString(Params, TEXT("anim_asset"), TEXT(""));
	if (!AnimAsset.IsEmpty())
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *AnimAsset);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Animation asset not found: '%s'"), *AnimAsset);
			return false;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> FAddAnimNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString GraphName, NodeType, Error;
	GetRequiredString(Params, TEXT("graph_name"), GraphName, Error);
	GetRequiredString(Params, TEXT("node_type"), NodeType, Error);

	UEdGraph* TargetGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, GraphName, Error);
	if (!TargetGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	FVector2D Position = GetNodePosition(Params);

	UAnimGraphNode_Base* NewNode = AnimGraphHelpers::CreateAnimNodeByType(TargetGraph, NodeType, Position, Params, Error);
	if (!NewNode)
	{
		return CreateErrorResponse(Error, TEXT("creation_failed"));
	}

	// Bind anim asset if provided
	FString AnimAsset = GetOptionalString(Params, TEXT("anim_asset"), TEXT(""));
	if (!AnimAsset.IsEmpty())
	{
		if (UAnimGraphNode_SequencePlayer* SeqPlayer = Cast<UAnimGraphNode_SequencePlayer>(NewNode))
		{
			UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AnimAsset);
			if (Seq) SeqPlayer->Node.Sequence = Seq;
		}
		else if (UAnimGraphNode_BlendSpacePlayer* BSPlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(NewNode))
		{
			UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AnimAsset);
			if (BS) BSPlayer->Node.BlendSpace = BS;
		}
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_type"), NodeType);
	Result->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString());
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.8 — FSetAnimNodePropertyAction
// ============================================================================

bool FSetAnimNodePropertyAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString GraphName, NodeGuidStr, PropertyName;
	if (!GetRequiredString(Params, TEXT("graph_name"), GraphName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("node_guid"), NodeGuidStr, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("property_name"), PropertyName, OutError)) return false;

	if (!Params->HasField(TEXT("property_value")))
	{
		OutError = TEXT("Missing required parameter: property_value");
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FSetAnimNodePropertyAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString GraphName, NodeGuidStr, PropertyName, Error;
	GetRequiredString(Params, TEXT("graph_name"), GraphName, Error);
	GetRequiredString(Params, TEXT("node_guid"), NodeGuidStr, Error);
	GetRequiredString(Params, TEXT("property_name"), PropertyName, Error);

	UEdGraph* TargetGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, GraphName, Error);
	if (!TargetGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Find node by GUID
	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Invalid node GUID: '%s'"), *NodeGuidStr),
			TEXT("invalid_param"));
	}

	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Node not found with GUID: '%s'"), *NodeGuidStr),
			TEXT("not_found"));
	}

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	// Try to set via FMCPCommonUtils reflection
	FString SetError;
	if (!FMCPCommonUtils::SetObjectProperty(TargetNode, PropertyName, JsonValue, SetError))
	{
		// List available properties for better error message
		TArray<FString> AvailableProps;
		for (TFieldIterator<FProperty> PropIt(TargetNode->GetClass()); PropIt; ++PropIt)
		{
			AvailableProps.Add(PropIt->GetName());
		}
		return CreateErrorResponse(
			FString::Printf(TEXT("Property '%s' not found on node. Available: [%s]"),
				*PropertyName, *FString::Join(AvailableProps, TEXT(", "))),
			TEXT("not_found"));
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_guid"), NodeGuidStr);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.9 — FConnectAnimNodesAction
// ============================================================================

bool FConnectAnimNodesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString GraphName, SrcNodeId, SrcPin, DstNodeId, DstPin;
	if (!GetRequiredString(Params, TEXT("graph_name"), GraphName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_node_id"), SrcNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_pin"), SrcPin, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_node_id"), DstNodeId, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_pin"), DstPin, OutError)) return false;

	return true;
}

TSharedPtr<FJsonObject> FConnectAnimNodesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString GraphName, SrcNodeId, SrcPin, DstNodeId, DstPin, Error;
	GetRequiredString(Params, TEXT("graph_name"), GraphName, Error);
	GetRequiredString(Params, TEXT("source_node_id"), SrcNodeId, Error);
	GetRequiredString(Params, TEXT("source_pin"), SrcPin, Error);
	GetRequiredString(Params, TEXT("target_node_id"), DstNodeId, Error);
	GetRequiredString(Params, TEXT("target_pin"), DstPin, Error);

	UEdGraph* TargetGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, GraphName, Error);
	if (!TargetGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	// Find source and target nodes by GUID
	auto FindNodeByGuid = [&](const FString& GuidStr) -> UEdGraphNode*
	{
		FGuid Guid;
		if (!FGuid::Parse(GuidStr, Guid)) return nullptr;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid) return Node;
		}
		return nullptr;
	};

	UEdGraphNode* SrcNode = FindNodeByGuid(SrcNodeId);
	UEdGraphNode* DstNode = FindNodeByGuid(DstNodeId);

	if (!SrcNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Source node not found: '%s'"), *SrcNodeId), TEXT("not_found"));
	}
	if (!DstNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Target node not found: '%s'"), *DstNodeId), TEXT("not_found"));
	}

	// Find pins
	UEdGraphPin* SourcePin = FMCPCommonUtils::FindPin(SrcNode, SrcPin, EGPD_Output);
	if (!SourcePin) SourcePin = FMCPCommonUtils::FindPin(SrcNode, SrcPin, EGPD_Input);

	UEdGraphPin* TargetPin = FMCPCommonUtils::FindPin(DstNode, DstPin, EGPD_Input);
	if (!TargetPin) TargetPin = FMCPCommonUtils::FindPin(DstNode, DstPin, EGPD_Output);

	if (!SourcePin)
	{
		TArray<FString> AvailPins;
		for (UEdGraphPin* P : SrcNode->Pins) { if (P) AvailPins.Add(P->PinName.ToString()); }
		return CreateErrorResponse(
			FString::Printf(TEXT("Pin '%s' not found on source node. Available: [%s]"),
				*SrcPin, *FString::Join(AvailPins, TEXT(", "))),
			TEXT("not_found"));
	}
	if (!TargetPin)
	{
		TArray<FString> AvailPins;
		for (UEdGraphPin* P : DstNode->Pins) { if (P) AvailPins.Add(P->PinName.ToString()); }
		return CreateErrorResponse(
			FString::Printf(TEXT("Pin '%s' not found on target node. Available: [%s]"),
				*DstPin, *FString::Join(AvailPins, TEXT(", "))),
			TEXT("not_found"));
	}

	const UEdGraphSchema* Schema = TargetGraph->GetSchema();
	FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
	if (Response.Response == CONNECT_RESPONSE_DISALLOW)
	{
		return CreateErrorResponse(
			FString::Printf(TEXT("Pin type mismatch: '%s' -> '%s'. %s"),
				*SrcPin, *DstPin, *Response.Message.ToString()),
			TEXT("type_mismatch"));
	}

	Schema->TryCreateConnection(SourcePin, TargetPin);
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_node_id"), SrcNodeId);
	Result->SetStringField(TEXT("source_pin"), SrcPin);
	Result->SetStringField(TEXT("target_node_id"), DstNodeId);
	Result->SetStringField(TEXT("target_pin"), DstPin);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.10 — FDisconnectAnimNodeAction
// ============================================================================

bool FDisconnectAnimNodeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString GraphName, NodeGuid, PinName;
	if (!GetRequiredString(Params, TEXT("graph_name"), GraphName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("node_guid"), NodeGuid, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("pin_name"), PinName, OutError)) return false;

	return true;
}

TSharedPtr<FJsonObject> FDisconnectAnimNodeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString GraphName, NodeGuidStr, PinName, Error;
	GetRequiredString(Params, TEXT("graph_name"), GraphName, Error);
	GetRequiredString(Params, TEXT("node_guid"), NodeGuidStr, Error);
	GetRequiredString(Params, TEXT("pin_name"), PinName, Error);

	UEdGraph* TargetGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, GraphName, Error);
	if (!TargetGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Invalid node GUID: '%s'"), *NodeGuidStr), TEXT("invalid_param"));
	}

	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node && Node->NodeGuid == NodeGuid) { TargetNode = Node; break; }
	}

	if (!TargetNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Node not found with GUID: '%s'"), *NodeGuidStr), TEXT("not_found"));
	}

	UEdGraphPin* Pin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Output);
	if (!Pin) Pin = FMCPCommonUtils::FindPin(TargetNode, PinName, EGPD_Input);

	if (!Pin)
	{
		TArray<FString> AvailPins;
		for (UEdGraphPin* P : TargetNode->Pins) { if (P) AvailPins.Add(P->PinName.ToString()); }
		return CreateErrorResponse(
			FString::Printf(TEXT("Pin '%s' not found on node. Available: [%s]"),
				*PinName, *FString::Join(AvailPins, TEXT(", "))),
			TEXT("not_found"));
	}

	Pin->BreakAllPinLinks();
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("node_guid"), NodeGuidStr);
	Result->SetStringField(TEXT("pin_name"), PinName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.11 — FRenameStateAction
// ============================================================================

bool FRenameStateAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName, OldName, NewName;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("old_name"), OldName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("new_name"), NewName, OutError)) return false;

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (!SMGraph)
	{
		OutError = FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName);
		return false;
	}

	// Verify old state exists
	if (!AnimGraphHelpers::FindStateNode(SMGraph, OldName, OutError)) return false;

	// Check new name doesn't conflict
	UAnimStateNodeBase* Conflict = AnimGraphHelpers::FindStateNode(SMGraph, NewName, DummyError);
	if (Conflict)
	{
		OutError = FString::Printf(TEXT("State '%s' already exists in state machine '%s'"), *NewName, *StateMachineName);
		return false;
	}

	return true;
}

TSharedPtr<FJsonObject> FRenameStateAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, OldName, NewName, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("old_name"), OldName, Error);
	GetRequiredString(Params, TEXT("new_name"), NewName, Error);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateNodeBase* StateNode = AnimGraphHelpers::FindStateNode(SMGraph, OldName, Error);
	if (!StateNode)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	if (UAnimStateNode* ConcreteState = Cast<UAnimStateNode>(StateNode))
	{
		ConcreteState->SetStateName(FName(*NewName));
	}

	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("old_name"), OldName);
	Result->SetStringField(TEXT("new_name"), NewName);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.12 — FSetTransitionPriorityAction
// ============================================================================

bool FSetTransitionPriorityAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	FString StateMachineName, SourceState, TargetState;
	if (!GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("source_state"), SourceState, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("target_state"), TargetState, OutError)) return false;

	if (!Params->HasField(TEXT("priority")))
	{
		OutError = TEXT("Missing required parameter: priority");
		return false;
	}

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
	if (!AnimBP)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' is not an Animation Blueprint"), *Blueprint->GetName());
		return false;
	}

	FString DummyError;
	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, DummyError);
	if (!SMGraph)
	{
		OutError = FString::Printf(TEXT("State machine '%s' not found"), *StateMachineName);
		return false;
	}

	UAnimStateTransitionNode* TransNode = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, OutError);
	if (!TransNode) return false;

	return true;
}

TSharedPtr<FJsonObject> FSetTransitionPriorityAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);

	FString StateMachineName, SourceState, TargetState, Error;
	GetRequiredString(Params, TEXT("state_machine_name"), StateMachineName, Error);
	GetRequiredString(Params, TEXT("source_state"), SourceState, Error);
	GetRequiredString(Params, TEXT("target_state"), TargetState, Error);
	int32 Priority = (int32)GetOptionalNumber(Params, TEXT("priority"), 1.0);

	UEdGraph* SMGraph = AnimGraphHelpers::FindAnimSubGraph(AnimBP, StateMachineName, Error);
	if (!SMGraph)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	UAnimStateTransitionNode* TransNode = AnimGraphHelpers::FindTransitionNode(SMGraph, SourceState, TargetState, Error);
	if (!TransNode)
	{
		return CreateErrorResponse(Error, TEXT("not_found"));
	}

	TransNode->PriorityOrder = Priority;
	MarkBlueprintModified(Blueprint, Context);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("source_state"), SourceState);
	Result->SetStringField(TEXT("target_state"), TargetState);
	Result->SetNumberField(TEXT("priority"), Priority);
	return CreateSuccessResponse(Result);
}


// ============================================================================
// 4.13 — FCompileAnimBlueprintAction
// ============================================================================

bool FCompileAnimBlueprintAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	if (!ValidateBlueprint(Params, Context, OutError))
	{
		return false;
	}

	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	return AnimGraphHelpers::ValidateAnimBlueprint(Blueprint, OutError);
}

TSharedPtr<FJsonObject> FCompileAnimBlueprintAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UBlueprint* Blueprint = GetTargetBlueprint(Params, Context);
	if (!Blueprint)
	{
		return CreateErrorResponse(TEXT("Blueprint not found"), TEXT("not_found"));
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	EBlueprintStatus Status = Blueprint->Status;
	bool bSuccess = (Status == EBlueprintStatus::BS_UpToDate || Status == EBlueprintStatus::BS_UpToDateWithWarnings);

	// Collect compilation messages
	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;

	auto ProcessGraph = [&](UEdGraph* Graph)
	{
		if (!Graph) return;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->bHasCompilerMessage)
			{
				TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
				MsgObj->SetStringField(TEXT("node"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
				MsgObj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
				MsgObj->SetStringField(TEXT("message"), Node->ErrorMsg);
				if (Node->ErrorType == EMessageSeverity::Error)
					Errors.Add(MakeShared<FJsonValueObject>(MsgObj));
				else if (Node->ErrorType == EMessageSeverity::Warning)
					Warnings.Add(MakeShared<FJsonValueObject>(MsgObj));
			}
		}
	};

	for (UEdGraph* Graph : Blueprint->UbergraphPages) ProcessGraph(Graph);
	for (UEdGraph* Graph : Blueprint->FunctionGraphs) ProcessGraph(Graph);

	// Save dirty packages on success
	int32 SavedPackagesCount = 0;
	if (bSuccess)
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyPackages(DirtyPackages);
		for (UPackage* Package : DirtyPackages)
		{
			if (!Package) continue;
			FString PackageFileName;
			FString PackageName = Package->GetName();
			bool bIsMap = Package->ContainsMap();
			FString Extension = bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
			if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFileName, Extension))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;
				if (UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs))
				{
					SavedPackagesCount++;
				}
			}
		}
	}

	FString StatusStr;
	switch (Status)
	{
		case EBlueprintStatus::BS_Error:                StatusStr = TEXT("Error"); break;
		case EBlueprintStatus::BS_UpToDate:             StatusStr = TEXT("UpToDate"); break;
		case EBlueprintStatus::BS_UpToDateWithWarnings: StatusStr = TEXT("UpToDateWithWarnings"); break;
		default:                                        StatusStr = TEXT("Unknown"); break;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Blueprint->GetName());
	Result->SetBoolField(TEXT("compiled"), bSuccess);
	Result->SetStringField(TEXT("status"), StatusStr);
	Result->SetNumberField(TEXT("error_count"), Errors.Num());
	Result->SetNumberField(TEXT("warning_count"), Warnings.Num());
	Result->SetNumberField(TEXT("saved_packages_count"), SavedPackagesCount);
	if (Errors.Num() > 0) Result->SetArrayField(TEXT("errors"), Errors);
	if (Warnings.Num() > 0) Result->SetArrayField(TEXT("warnings"), Warnings);

	if (!bSuccess)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Animation Blueprint '%s' compilation failed with %d error(s)"),
				*Blueprint->GetName(), Errors.Num()));
		Result->SetStringField(TEXT("error_type"), TEXT("compilation_failed"));
		return Result;
	}

	return CreateSuccessResponse(Result);
}


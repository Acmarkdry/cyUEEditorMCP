// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPEventHub.h"
#include "Actions/EditorAction.h"  // LogMCP
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Engine includes for editor delegates
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectGlobals.h"

// ─── Static members ────────────────────────────────────────────────────

TAtomic<int32> FMCPEventHub::NextClientId(1);

// ─── FMCPEvent ─────────────────────────────────────────────────────────

FString FMCPEvent::ToJsonString() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("event"));
	Root->SetStringField(TEXT("event_type"), EventName);
	Root->SetNumberField(TEXT("timestamp"), Timestamp);
	Root->SetObjectField(TEXT("data"), Data.IsValid() ? Data : MakeShared<FJsonObject>());

	FString OutStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutStr;
}

// ─── FMCPEventHub ──────────────────────────────────────────────────────

FMCPEventHub::FMCPEventHub()
{
}

FMCPEventHub::~FMCPEventHub()
{
	StopListening();
}

void FMCPEventHub::StartListening()
{
	if (bIsListening)
	{
		return;
	}

	BindEditorDelegates();
	bIsListening = true;
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: EventHub started listening to editor events"));
}

void FMCPEventHub::StopListening()
{
	if (!bIsListening)
	{
		return;
	}

	UnbindEditorDelegates();
	bIsListening = false;

	// Clear all client queues
	FScopeLock Lock(&QueueLock);
	ClientQueues.Empty();

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: EventHub stopped"));
}

void FMCPEventHub::EnqueueEvent(const FMCPEvent& Event)
{
	FScopeLock Lock(&QueueLock);

	for (auto& Pair : ClientQueues)
	{
		FClientEventQueue& Queue = Pair.Value;
		if (Queue.Subscription.Matches(Event.EventName))
		{
			if (Queue.PendingEvents.Num() < FClientEventQueue::MaxQueueSize)
			{
				Queue.PendingEvents.Add(Event);
			}
			else
			{
				// Drop oldest event to make room
				Queue.PendingEvents.RemoveAt(0);
				Queue.PendingEvents.Add(Event);
			}
		}
	}
}

void FMCPEventHub::EnqueueCustomEvent(const FString& EventName, TSharedPtr<FJsonObject> Data)
{
	EnqueueEvent(FMCPEvent(EMCPEventType::Custom, EventName, Data));
}

// ─── Client subscription management ───────────────────────────────────

void FMCPEventHub::Subscribe(int32 ClientId, const TArray<FString>& EventTypes)
{
	FScopeLock Lock(&QueueLock);

	FClientEventQueue& Queue = ClientQueues.FindOrAdd(ClientId);
	Queue.Subscription.bActive = true;
	Queue.Subscription.SubscribedEvents.Empty();
	for (const FString& Type : EventTypes)
	{
		Queue.Subscription.SubscribedEvents.Add(Type);
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client %d subscribed to %d event types (empty=all)"),
		ClientId, EventTypes.Num());
}

void FMCPEventHub::Unsubscribe(int32 ClientId)
{
	FScopeLock Lock(&QueueLock);
	ClientQueues.Remove(ClientId);
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client %d unsubscribed from events"), ClientId);
}

TArray<FMCPEvent> FMCPEventHub::DrainEventsForClient(int32 ClientId)
{
	FScopeLock Lock(&QueueLock);

	FClientEventQueue* Queue = ClientQueues.Find(ClientId);
	if (!Queue)
	{
		return TArray<FMCPEvent>();
	}

	TArray<FMCPEvent> Events = MoveTemp(Queue->PendingEvents);
	Queue->PendingEvents.Empty();
	return Events;
}

int32 FMCPEventHub::GetPendingEventCount(int32 ClientId) const
{
	FScopeLock Lock(&QueueLock);

	const FClientEventQueue* Queue = ClientQueues.Find(ClientId);
	return Queue ? Queue->PendingEvents.Num() : 0;
}

// ─── Editor delegate binding ──────────────────────────────────────────

void FMCPEventHub::BindEditorDelegates()
{
	// Blueprint compiled
	if (GEditor)
	{
		BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FMCPEventHub::OnBlueprintCompiled);
	}

	// Asset events via AssetRegistry
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &FMCPEventHub::OnAssetRemoved);
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FMCPEventHub::OnAssetRenamed);

	// Map/Level changes
	if (GEditor)
	{
		// Use FEditorDelegates for map change events
		MapChangedHandle = FEditorDelegates::MapChange.AddRaw(this, &FMCPEventHub::OnMapChanged);
	}

	// PIE start/end
	if (GEditor)
	{
		PIEStartedHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FMCPEventHub::OnPIEStarted);
		PIEEndedHandle = FEditorDelegates::EndPIE.AddRaw(this, &FMCPEventHub::OnPIEEnded);
	}

	// Undo/Redo
	if (GEditor && GEditor->Trans)
	{
		PostUndoHandle = GEditor->Trans->OnUndo().AddRaw(this, &FMCPEventHub::OnPostUndo);
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: EventHub bound editor delegates"));
}

void FMCPEventHub::UnbindEditorDelegates()
{
	if (GEditor && BlueprintCompiledHandle.IsValid())
	{
		GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
		BlueprintCompiledHandle.Reset();
	}

	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (AssetRemovedHandle.IsValid())
		{
			AssetRegistry.OnAssetRemoved().Remove(AssetRemovedHandle);
			AssetRemovedHandle.Reset();
		}
		if (AssetRenamedHandle.IsValid())
		{
			AssetRegistry.OnAssetRenamed().Remove(AssetRenamedHandle);
			AssetRenamedHandle.Reset();
		}
	}

	if (MapChangedHandle.IsValid())
	{
		FEditorDelegates::MapChange.Remove(MapChangedHandle);
		MapChangedHandle.Reset();
	}

	if (PIEStartedHandle.IsValid())
	{
		FEditorDelegates::BeginPIE.Remove(PIEStartedHandle);
		PIEStartedHandle.Reset();
	}

	if (PIEEndedHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(PIEEndedHandle);
		PIEEndedHandle.Reset();
	}

	if (GEditor && GEditor->Trans && PostUndoHandle.IsValid())
	{
		GEditor->Trans->OnUndo().Remove(PostUndoHandle);
		PostUndoHandle.Reset();
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: EventHub unbound editor delegates"));
}

// ─── Editor event handlers ────────────────────────────────────────────

void FMCPEventHub::OnBlueprintCompiled(UBlueprint* Blueprint)
{
	if (!Blueprint || !bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	Data->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("status"),
		Blueprint->Status == BS_Error ? TEXT("error") :
		Blueprint->Status == BS_UpToDate ? TEXT("up_to_date") :
		Blueprint->Status == BS_UpToDateWithWarnings ? TEXT("up_to_date_with_warnings") :
		TEXT("unknown"));
	Data->SetBoolField(TEXT("has_errors"), Blueprint->Status == BS_Error);

	EnqueueEvent(FMCPEvent(EMCPEventType::BlueprintCompiled, TEXT("blueprint_compiled"), Data));
}

void FMCPEventHub::OnAssetSaved(const FString& PackageName, UObject* Asset)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("package_name"), PackageName);
	if (Asset)
	{
		Data->SetStringField(TEXT("asset_name"), Asset->GetName());
		Data->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetName());
	}

	EnqueueEvent(FMCPEvent(EMCPEventType::AssetSaved, TEXT("asset_saved"), Data));
}

void FMCPEventHub::OnAssetRemoved(const FAssetData& AssetData)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	Data->SetStringField(TEXT("asset_class"), AssetData.AssetClassPath.GetAssetName().ToString());

	EnqueueEvent(FMCPEvent(EMCPEventType::AssetDeleted, TEXT("asset_deleted"), Data));
}

void FMCPEventHub::OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("new_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("new_path"), AssetData.GetObjectPathString());
	Data->SetStringField(TEXT("old_path"), OldPath);

	EnqueueEvent(FMCPEvent(EMCPEventType::AssetRenamed, TEXT("asset_renamed"), Data));
}

void FMCPEventHub::OnMapChanged(uint32 MapChangeFlags)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("change_flags"), static_cast<double>(MapChangeFlags));

	FString Description;
	if (MapChangeFlags & 1) Description = TEXT("new_map");
	else if (MapChangeFlags & 2) Description = TEXT("map_loaded");
	else Description = TEXT("map_changed");
	Data->SetStringField(TEXT("change_type"), Description);

	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		Data->SetStringField(TEXT("map_name"), GEditor->GetEditorWorldContext().World()->GetMapName());
	}

	EnqueueEvent(FMCPEvent(EMCPEventType::LevelChanged, TEXT("level_changed"), Data));
}

void FMCPEventHub::OnPIEStarted(bool bIsSimulating)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), bIsSimulating ? TEXT("simulating") : TEXT("playing"));
	Data->SetBoolField(TEXT("is_simulating"), bIsSimulating);

	EnqueueEvent(FMCPEvent(EMCPEventType::PIEStateChanged, TEXT("pie_started"), Data));
}

void FMCPEventHub::OnPIEEnded(bool bIsSimulating)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("state"), TEXT("stopped"));
	Data->SetBoolField(TEXT("was_simulating"), bIsSimulating);

	EnqueueEvent(FMCPEvent(EMCPEventType::PIEStateChanged, TEXT("pie_ended"), Data));
}

void FMCPEventHub::OnSelectionChanged(UObject* SelectedObject)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (SelectedObject)
	{
		Data->SetStringField(TEXT("selected_object"), SelectedObject->GetName());
		Data->SetStringField(TEXT("selected_class"), SelectedObject->GetClass()->GetName());
		Data->SetStringField(TEXT("selected_path"), SelectedObject->GetPathName());
	}
	else
	{
		Data->SetStringField(TEXT("selected_object"), TEXT(""));
	}

	EnqueueEvent(FMCPEvent(EMCPEventType::SelectionChanged, TEXT("selection_changed"), Data));
}

void FMCPEventHub::OnPostUndo(bool bSuccess)
{
	if (!bIsListening) return;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("success"), bSuccess);
	Data->SetStringField(TEXT("action"), TEXT("undo"));

	EnqueueEvent(FMCPEvent(EMCPEventType::UndoRedo, TEXT("undo_performed"), Data));
}

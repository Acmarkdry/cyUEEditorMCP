// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Delegates/IDelegateInstance.h"

// Forward declarations
class FMCPServer;
class FSocket;

/**
 * EMCPEventType
 *
 * Types of events the MCP server can push to connected clients.
 */
enum class EMCPEventType : uint8
{
	/** Blueprint compiled (success or failure) */
	BlueprintCompiled,

	/** Asset saved */
	AssetSaved,

	/** Asset deleted */
	AssetDeleted,

	/** Asset renamed */
	AssetRenamed,

	/** PIE state changed (started, stopped, paused) */
	PIEStateChanged,

	/** Level changed (map opened, sublevel loaded/unloaded) */
	LevelChanged,

	/** Selection changed in editor */
	SelectionChanged,

	/** Undo/Redo performed */
	UndoRedo,

	/** Log message (filtered by severity) */
	LogMessage,

	/** Generic custom event */
	Custom,
};


/**
 * FMCPEvent
 *
 * A single event to be pushed to subscribed clients.
 * Wire format:  { "type": "event", "event_type": "...", "data": {...}, "timestamp": ... }
 */
struct FMCPEvent
{
	EMCPEventType Type;
	FString EventName;     // String name for wire format: "blueprint_compiled", "asset_saved", etc.
	TSharedPtr<FJsonObject> Data;
	double Timestamp;

	FMCPEvent()
		: Type(EMCPEventType::Custom)
		, Timestamp(FPlatformTime::Seconds())
	{}

	FMCPEvent(EMCPEventType InType, const FString& InName, TSharedPtr<FJsonObject> InData = nullptr)
		: Type(InType)
		, EventName(InName)
		, Data(InData ? InData : MakeShared<FJsonObject>())
		, Timestamp(FPlatformTime::Seconds())
	{}

	/** Serialize to JSON wire format */
	FString ToJsonString() const;
};


/**
 * FMCPEventSubscription
 *
 * Tracks which event types a client has subscribed to.
 */
struct FMCPEventSubscription
{
	/** Set of subscribed event type names (empty = subscribe to all) */
	TSet<FString> SubscribedEvents;

	/** Whether this subscription is active */
	bool bActive = false;

	/** Check if a given event name matches this subscription */
	bool Matches(const FString& EventName) const
	{
		return bActive && (SubscribedEvents.Num() == 0 || SubscribedEvents.Contains(EventName));
	}
};


/**
 * FMCPEventHub
 *
 * Central event dispatcher for the MCP system.
 * Collects editor events and dispatches them to subscribed clients.
 *
 * Architecture:
 * - Editor delegates → FMCPEventHub::EnqueueEvent (game thread)
 * - Client handlers poll their event queues periodically
 * - Events are serialized as JSON and sent via the existing TCP connection
 *
 * Thread safety:
 * - EnqueueEvent is thread-safe (uses a lock-free queue)
 * - Client subscriptions are protected by a critical section
 */
class UEEDITORMCP_API FMCPEventHub
{
public:
	FMCPEventHub();
	~FMCPEventHub();

	/** Start listening to editor events */
	void StartListening();

	/** Stop listening to editor events */
	void StopListening();

	/** Enqueue an event for all subscribed clients (thread-safe) */
	void EnqueueEvent(const FMCPEvent& Event);

	/** Enqueue a custom event by name and data */
	void EnqueueCustomEvent(const FString& EventName, TSharedPtr<FJsonObject> Data = nullptr);

	// ─── Client subscription management ────────────────────────────

	/** Subscribe a client (by ID) to specific events. Empty array = all events. */
	void Subscribe(int32 ClientId, const TArray<FString>& EventTypes);

	/** Unsubscribe a client */
	void Unsubscribe(int32 ClientId);

	/** Get pending events for a client and clear them */
	TArray<FMCPEvent> DrainEventsForClient(int32 ClientId);

	/** Get the number of pending events for a client */
	int32 GetPendingEventCount(int32 ClientId) const;

private:
	/** Register UE editor delegates */
	void BindEditorDelegates();

	/** Unregister UE editor delegates */
	void UnbindEditorDelegates();

	// ─── Editor event handlers ─────────────────────────────────────

	void OnBlueprintCompiled(UBlueprint* Blueprint);
	void OnAssetSaved(const FString& PackageName, UObject* Asset);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath);
	void OnMapChanged(uint32 MapChangeFlags);
	void OnPIEStarted(bool bIsSimulating);
	void OnPIEEnded(bool bIsSimulating);
	void OnSelectionChanged(UObject* SelectedObject);
	void OnPostUndo(bool bSuccess);

	// ─── Per-client event queues ───────────────────────────────────

	struct FClientEventQueue
	{
		FMCPEventSubscription Subscription;
		TArray<FMCPEvent> PendingEvents;
		static constexpr int32 MaxQueueSize = 500;
	};

	/** Client queues (ClientId → queue) */
	TMap<int32, FClientEventQueue> ClientQueues;
	mutable FCriticalSection QueueLock;

	/** Whether we're actively listening */
	bool bIsListening = false;

	/** Delegate handles for cleanup */
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle AssetSavedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle MapChangedHandle;
	FDelegateHandle PIEStartedHandle;
	FDelegateHandle PIEEndedHandle;
	FDelegateHandle SelectionChangedHandle;
	FDelegateHandle PostUndoHandle;

	/** Monotonic client ID counter */
	static TAtomic<int32> NextClientId;

public:
	/** Allocate a unique client ID */
	static int32 AllocateClientId() { return NextClientId++; }
};

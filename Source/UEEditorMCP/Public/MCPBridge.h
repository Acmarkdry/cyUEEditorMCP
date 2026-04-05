// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "MCPContext.h"
#include "MCPEventHub.h"
#include "MCPBridge.generated.h"

// Forward declarations
class FMCPServer;
class FEditorAction;

/**
 * FAsyncTaskEntry
 * Represents an async task submitted for execution on the game thread.
 */
struct FAsyncTaskEntry
{
	FString TaskId;
	FString Status;  // "pending", "success", "error"
	TSharedPtr<FJsonObject> Result;
	double CreatedTime;

	FAsyncTaskEntry()
		: Status(TEXT("pending"))
		, CreatedTime(FPlatformTime::Seconds())
	{}
};

/**
 * UMCPBridge
 *
 * Editor subsystem that manages the MCP server and routes commands
 * to appropriate action handlers.
 */
UCLASS()
class UEEDITORMCP_API UMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UMCPBridge();

	// =========================================================================
	// UEditorSubsystem Interface
	// =========================================================================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// =========================================================================
	// Command Execution
	// =========================================================================

	/**
	 * Execute a command received from the MCP server.
	 * Routes to appropriate action handler based on command type.
	 *
	 * @param CommandType The type of command (e.g., "create_blueprint")
	 * @param Params The command parameters
	 * @return JSON response with success/failure and result/error
	 */
	TSharedPtr<FJsonObject> ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/**
	 * Execute a command with crash protection (SEH on Windows, signal handler on Unix).
	 * If execution crashes, returns an error response instead of crashing the editor.
	 */
	TSharedPtr<FJsonObject> ExecuteCommandSafe(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	// =========================================================================
	// Context Access
	// =========================================================================

	/** Get the current editor context */
	FMCPEditorContext& GetContext() { return Context; }
	const FMCPEditorContext& GetContext() const { return Context; }

	// =========================================================================
	// Response Helpers
	// =========================================================================

	/** Create a success response */
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& ResultData = nullptr);

	/** Create an error response */
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& ErrorMessage, const FString& ErrorType = TEXT("error"));

	// =========================================================================
	// Async Task Management
	// =========================================================================

	/**
	 * Submit a command for async execution on the game thread.
	 * Returns immediately with a task_id.
	 */
	FString SubmitAsyncTask(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/**
	 * Get the result of an async task.
	 * Returns the result if complete, or a pending status.
	 * Removes completed tasks from the map after retrieval.
	 */
	TSharedPtr<FJsonObject> GetTaskResult(const FString& TaskId);

	/**
	 * Clean up expired tasks (older than TTL).
	 * Called automatically during Submit/Get operations.
	 */
	void CleanupExpiredTasks();

private:
	/** Register all action handlers */
	void RegisterActions();

	/** Find action handler for a command type */
	TSharedRef<FEditorAction>* FindAction(const FString& CommandType);

	/** Execute internal command (called after validation) */
	TSharedPtr<FJsonObject> ExecuteCommandInternal(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/** The MCP TCP server (raw pointer - cleanup in Deinitialize) */
	FMCPServer* Server;

	/** Editor context (persists across commands) */
	FMCPEditorContext Context;

	/** Map of command types to action handlers */
	TMap<FString, TSharedRef<FEditorAction>> ActionHandlers;

	/** Async task storage (thread-safe) */
	TMap<FString, FAsyncTaskEntry> AsyncTasks;
	FCriticalSection AsyncTasksLock;

	/** Async task TTL in seconds */
	static constexpr double AsyncTaskTTL = 300.0;

	/** Port to listen on (55558 during development to avoid conflict with old plugin) */
	static constexpr int32 DefaultPort = 55558;

public:
	// =========================================================================
	// Event Hub Access (P9: Event Push System)
	// =========================================================================

	/** Get the event hub for event subscription and polling */
	FMCPEventHub& GetEventHub() { return EventHub; }
	const FMCPEventHub& GetEventHub() const { return EventHub; }

private:
	/** Event hub for editor event push (P9) */
	FMCPEventHub EventHub;
};

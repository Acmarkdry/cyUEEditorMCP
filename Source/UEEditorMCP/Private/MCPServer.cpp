// Copyright (c) 2025 zolnoor. All rights reserved.

#include "MCPServer.h"
#include "MCPBridge.h"
#include "MCPEventHub.h"
#include "Actions/EditorAction.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

// =============================================================================
// FMCPClientHandler - per-client thread
// =============================================================================

FMCPClientHandler::FMCPClientHandler(FSocket* InClientSocket, UMCPBridge* InBridge, TAtomic<bool>& InServerStopping)
	: ClientSocket(InClientSocket)
	, Bridge(InBridge)
	, Thread(nullptr)
	, bServerStopping(InServerStopping)
	, bShouldStop(false)
	, bIsFinished(false)
	, EventClientId(FMCPEventHub::AllocateClientId())
{
	// Start the handler thread
	Thread = FRunnableThread::Create(this, TEXT("UEEditorMCP Client Handler"));
}

FMCPClientHandler::~FMCPClientHandler()
{
	bShouldStop = true;

	// Unsubscribe from events on cleanup
	if (Bridge)
	{
		Bridge->GetEventHub().Unsubscribe(EventClientId);
	}

	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	if (ClientSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ClientSocket);
		}
		ClientSocket = nullptr;
	}
}

uint32 FMCPClientHandler::Run()
{
	// Set socket options
	ClientSocket->SetNonBlocking(false);
	ClientSocket->SetNoDelay(true);

	float LastActivityTime = FPlatformTime::Seconds();

	while (!bShouldStop && !bServerStopping)
	{
		// Check for timeout
		float CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - LastActivityTime > ConnectionTimeout)
		{
			UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Client connection timed out"));
			break;
		}

		// Wait for socket to become readable (data available OR connection closed)
		// This uses select() internally �?reliable and avoids non-blocking toggle issues
		if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(0.5)))
		{
			// Timeout �?no data yet, loop to check stop flags and timeout
			continue;
		}

		// Socket is readable. Either data is available or the connection was closed (EOF).
		// Try to receive the length-prefixed message. If the connection was closed,
		// ReceiveMessage will fail when trying to read the 4-byte length header.
		FString Message;
		if (!ReceiveMessage(Message))
		{
			UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client disconnected or receive failed"));
			break;
		}

		LastActivityTime = FPlatformTime::Seconds();

		// Parse JSON
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Invalid JSON\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		// Get command type
		FString CommandType;
		if (!JsonObj->TryGetStringField(TEXT("type"), CommandType))
		{
			FString ErrorResponse = TEXT("{\"status\":\"error\",\"error\":\"Missing 'type' field\"}");
			SendResponse(ErrorResponse);
			continue;
		}

		// Handle special commands that don't need game thread
		if (CommandType == TEXT("ping"))
		{
			SendResponse(HandlePing());
			continue;
		}

		if (CommandType == TEXT("close"))
		{
			HandleClose();
			break;
		}

		if (CommandType == TEXT("get_context"))
		{
			SendResponse(HandleGetContext());
			continue;
		}

		// Handle async commands (no game thread blocking)
		if (CommandType == TEXT("async_execute"))
		{
			SendResponse(HandleAsyncExecute(JsonObj));
			continue;
		}

		if (CommandType == TEXT("get_task_result"))
		{
			SendResponse(HandleGetTaskResult(JsonObj));
			continue;
		}

		// P9: Event push protocol commands (no game thread needed)
		if (CommandType == TEXT("subscribe_events"))
		{
			SendResponse(HandleSubscribeEvents(JsonObj));
			continue;
		}

		if (CommandType == TEXT("poll_events"))
		{
			SendResponse(HandlePollEvents(JsonObj));
			continue;
		}

		if (CommandType == TEXT("unsubscribe_events"))
		{
			SendResponse(HandleUnsubscribeEvents());
			continue;
		}

		// Get params (optional)
		TSharedPtr<FJsonObject> Params;
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			Params = *ParamsPtr;
		}
		else
		{
			Params = MakeShared<FJsonObject>();
		}

		// Execute on game thread and get response
		FString Response = ExecuteOnGameThread(CommandType, Params);
		SendResponse(Response);
	}

	bIsFinished = true;
	return 0;
}

void FMCPClientHandler::Exit()
{
	bIsFinished = true;
}

bool FMCPClientHandler::ReceiveMessage(FString& OutMessage)
{
	// Receive length prefix (4 bytes, big endian)
	uint8 LengthBytes[4];
	int32 BytesRead = 0;
	if (!ClientSocket->Recv(LengthBytes, 4, BytesRead) || BytesRead != 4)
	{
		return false;
	}

	int32 Length = (LengthBytes[0] << 24) | (LengthBytes[1] << 16) | (LengthBytes[2] << 8) | LengthBytes[3];

	// Sanity check
	if (Length <= 0 || Length > RecvBufferSize)
	{
		UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Invalid message length: %d"), Length);
		return false;
	}

	// Receive message
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(Length);

	int32 TotalReceived = 0;
	while (TotalReceived < Length)
	{
		int32 Received = 0;
		if (!ClientSocket->Recv(Buffer.GetData() + TotalReceived, Length - TotalReceived, Received) || Received <= 0)
		{
			return false;
		}
		TotalReceived += Received;
	}

	// Convert to string
	OutMessage = FString(Length, UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
	return true;
}

bool FMCPClientHandler::SendResponse(const FString& Response)
{
	// Convert to UTF-8
	FTCHARToUTF8 Converter(*Response);
	int32 Length = Converter.Length();

	// Send length prefix (4 bytes, big endian)
	uint8 LengthBytes[4] = {
		static_cast<uint8>((Length >> 24) & 0xFF),
		static_cast<uint8>((Length >> 16) & 0xFF),
		static_cast<uint8>((Length >> 8) & 0xFF),
		static_cast<uint8>(Length & 0xFF)
	};

	int32 BytesSent = 0;
	if (!ClientSocket->Send(LengthBytes, 4, BytesSent) || BytesSent != 4)
	{
		return false;
	}

	// Send message
	int32 TotalSent = 0;
	while (TotalSent < Length)
	{
		int32 Sent = 0;
		if (!ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()) + TotalSent, Length - TotalSent, Sent) || Sent <= 0)
		{
			return false;
		}
		TotalSent += Sent;
	}

	return true;
}

FString FMCPClientHandler::HandlePing()
{
	return TEXT("{\"status\":\"success\",\"result\":{\"pong\":true}}");
}

void FMCPClientHandler::HandleClose()
{
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client requested disconnect"));
	SendResponse(TEXT("{\"status\":\"success\",\"result\":{\"closed\":true}}"));
}

FString FMCPClientHandler::HandleGetContext()
{
	FString Result;
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);

	AsyncTask(ENamedThreads::GameThread, [this, &Result, DoneEvent]()
	{
		// Scope guard: ensure DoneEvent->Trigger() is always called,
		// even if an unhandled exception occurs in the lambda body.
		struct FTriggerOnExit
		{
			FEvent* Event;
			~FTriggerOnExit() { Event->Trigger(); }
		} TriggerGuard{DoneEvent};

		if (!Bridge)
		{
			Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		try
		{
			TSharedPtr<FJsonObject> ContextJson = Bridge->GetContext().ToJson();

			TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("status"), TEXT("success"));
			Response->SetObjectField(TEXT("result"), ContextJson);

			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			Result = ResponseStr;
		}
		catch (...)
		{
			UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Exception in HandleGetContext"));
			Result = TEXT("{\"status\":\"error\",\"error\":\"Exception during get_context\"}");
		}
	});

	DoneEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return Result;
}

FString FMCPClientHandler::HandleAsyncExecute(const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Bridge not available\"}");
	}

	// Extract inner command and params
	const TSharedPtr<FJsonObject>* ParamsPtr;
	TSharedPtr<FJsonObject> Params;
	if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
	{
		Params = *ParamsPtr;
	}

	if (!Params.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Missing 'params' field in async_execute\"}");
	}

	FString InnerCommand;
	if (!Params->TryGetStringField(TEXT("command"), InnerCommand) || InnerCommand.IsEmpty())
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Missing 'command' field in async_execute params\"}");
	}

	// Get inner params (optional)
	TSharedPtr<FJsonObject> InnerParams;
	const TSharedPtr<FJsonObject>* InnerParamsPtr;
	if (Params->TryGetObjectField(TEXT("params"), InnerParamsPtr))
	{
		InnerParams = *InnerParamsPtr;
	}
	else
	{
		InnerParams = MakeShared<FJsonObject>();
	}

	// Submit async task (returns immediately)
	FString TaskId = Bridge->SubmitAsyncTask(InnerCommand, InnerParams);

	// Build response
	FString Response = FString::Printf(
		TEXT("{\"status\":\"success\",\"success\":true,\"result\":{\"task_id\":\"%s\",\"status\":\"submitted\"}}"),
		*TaskId
	);
	return Response;
}

FString FMCPClientHandler::HandleGetTaskResult(const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Bridge not available\"}");
	}

	// Extract task_id
	const TSharedPtr<FJsonObject>* ParamsPtr;
	TSharedPtr<FJsonObject> Params;
	if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
	{
		Params = *ParamsPtr;
	}

	if (!Params.IsValid())
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Missing 'params' field in get_task_result\"}");
	}

	FString TaskId;
	if (!Params->TryGetStringField(TEXT("task_id"), TaskId) || TaskId.IsEmpty())
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Missing 'task_id' field in get_task_result params\"}");
	}

	// Get task result
	TSharedPtr<FJsonObject> Result = Bridge->GetTaskResult(TaskId);

	// Serialize to string
	FString ResponseStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return ResponseStr;
}

// ─── P9: Event Push Protocol ────────────────────────────────────────────────

FString FMCPClientHandler::HandleSubscribeEvents(const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Bridge not available\"}");
	}

	TArray<FString> EventTypes;

	// Extract event types from params
	const TSharedPtr<FJsonObject>* ParamsPtr;
	if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
	{
		const TArray<TSharedPtr<FJsonValue>>* TypesArray;
		if ((*ParamsPtr)->TryGetArrayField(TEXT("event_types"), TypesArray))
		{
			for (const auto& Val : *TypesArray)
			{
				FString TypeStr;
				if (Val->TryGetString(TypeStr))
				{
					EventTypes.Add(TypeStr);
				}
			}
		}
	}

	Bridge->GetEventHub().Subscribe(EventClientId, EventTypes);

	FString EventTypesStr = EventTypes.Num() > 0 ? FString::Join(EventTypes, TEXT(", ")) : TEXT("all");
	return FString::Printf(
		TEXT("{\"status\":\"success\",\"success\":true,\"result\":{\"subscribed\":true,\"client_id\":%d,\"event_types\":\"%s\"}}"),
		EventClientId, *EventTypesStr
	);
}

FString FMCPClientHandler::HandlePollEvents(const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Bridge not available\"}");
	}

	int32 MaxEvents = 50;  // Default

	const TSharedPtr<FJsonObject>* ParamsPtr;
	if (JsonObj->TryGetObjectField(TEXT("params"), ParamsPtr))
	{
		double MaxVal = 0;
		if ((*ParamsPtr)->TryGetNumberField(TEXT("max_events"), MaxVal))
		{
			MaxEvents = FMath::Clamp(static_cast<int32>(MaxVal), 1, 500);
		}
	}

	TArray<FMCPEvent> Events = Bridge->GetEventHub().DrainEventsForClient(EventClientId);

	// Build JSON array of events
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("success"));
	Response->SetBoolField(TEXT("success"), true);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetNumberField(TEXT("count"), FMath::Min(Events.Num(), MaxEvents));
	ResultObj->SetNumberField(TEXT("total_pending"), Events.Num());

	TArray<TSharedPtr<FJsonValue>> EventArray;
	int32 Count = 0;
	for (const FMCPEvent& Evt : Events)
	{
		if (Count >= MaxEvents) break;

		TSharedPtr<FJsonObject> EvtObj = MakeShared<FJsonObject>();
		EvtObj->SetStringField(TEXT("event_type"), Evt.EventName);
		EvtObj->SetNumberField(TEXT("timestamp"), Evt.Timestamp);
		EvtObj->SetObjectField(TEXT("data"), Evt.Data.IsValid() ? Evt.Data : MakeShared<FJsonObject>());
		EventArray.Add(MakeShared<FJsonValueObject>(EvtObj));
		Count++;
	}

	ResultObj->SetArrayField(TEXT("events"), EventArray);
	Response->SetObjectField(TEXT("result"), ResultObj);

	FString ResponseStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
	return ResponseStr;
}

FString FMCPClientHandler::HandleUnsubscribeEvents()
{
	if (!Bridge)
	{
		return TEXT("{\"status\":\"error\",\"success\":false,\"error\":\"Bridge not available\"}");
	}

	Bridge->GetEventHub().Unsubscribe(EventClientId);

	return FString::Printf(
		TEXT("{\"status\":\"success\",\"success\":true,\"result\":{\"unsubscribed\":true,\"client_id\":%d}}"),
		EventClientId
	);
}

FString FMCPClientHandler::ExecuteOnGameThread(const FString& CommandType, TSharedPtr<FJsonObject> Params)
{
	// Heap-allocate Result so the lambda never writes to a dangling stack variable on timeout.
	auto Result = MakeShared<FString>();
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);

	// Shared flag: when the caller times out it sets this to false,
	// telling the lambda to return DoneEvent to the pool.
	auto bCallerWaiting = MakeShared<TAtomic<bool>>(true);

	// Capture CommandType by value to avoid dangling reference on timeout.
	AsyncTask(ENamedThreads::GameThread, [this, CmdType = CommandType, Params, Result, DoneEvent, bCallerWaiting]()
	{
		// Scope guard: always trigger DoneEvent even if an exception propagates.
		// If the caller already timed out, also return the event to the pool.
		struct FCleanupGuard
		{
			FEvent* Event;
			TSharedPtr<TAtomic<bool>> CallerWaiting;
			~FCleanupGuard()
			{
				Event->Trigger();
				if (!CallerWaiting->Load())
				{
					FPlatformProcess::ReturnSynchEventToPool(Event);
				}
			}
		} Guard{DoneEvent, bCallerWaiting};

		if (!Bridge)
		{
			*Result = TEXT("{\"status\":\"error\",\"error\":\"Bridge not available\"}");
			return;
		}

		TSharedPtr<FJsonObject> Response;
		try
		{
			Response = Bridge->ExecuteCommandSafe(CmdType, Params);
		}
		catch (...)
		{
			UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Unhandled exception in ExecuteCommandSafe for '%s'"), *CmdType);
		}

		if (Response.IsValid())
		{
			FString ResponseStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseStr);
			FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			*Result = ResponseStr;
		}
		else
		{
			*Result = FString::Printf(
				TEXT("{\"status\":\"error\",\"error\":\"Command '%s' returned null response\"}"),
				*CmdType);
		}
	});

	// Wait with timeout protection (240s). If the game thread is blocked for
	// longer than this (e.g., during modal dialogs or heavy compilation),
	// return a timeout error rather than hanging forever.
	static constexpr uint32 GameThreadTimeoutMs = 240000;
	if (!DoneEvent->Wait(GameThreadTimeoutMs))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Game thread execution timed out after %ds for command '%s'"),
			GameThreadTimeoutMs / 1000, *CommandType);
		// Signal lambda to return the event to pool when it eventually runs.
		// Do NOT return to pool here — the lambda still holds a pointer to it.
		bCallerWaiting->Store(false);
		return FString::Printf(
			TEXT("{\"status\":\"error\",\"error\":\"Game thread execution timed out after %ds for command: %s\"}"),
			GameThreadTimeoutMs / 1000, *CommandType);
	}

	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	return MoveTemp(*Result);
}

// =============================================================================
// FMCPServer - accept loop, spawns FMCPClientHandler per connection
// =============================================================================

FMCPServer::FMCPServer(UMCPBridge* InBridge, int32 InPort)
	: Bridge(InBridge)
	, ListenerSocket(nullptr)
	, Port(InPort)
	, Thread(nullptr)
	, bShouldStop(false)
	, bIsRunning(false)
	, bIsStopping(false)
{
}

FMCPServer::~FMCPServer()
{
	Stop();
}

bool FMCPServer::Start()
{
	if (bIsRunning)
	{
		return true;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to get socket subsystem"));
		return false;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UEEditorMCP Listener"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to create listener socket"));
		return false;
	}

	// Allow address reuse to avoid TIME_WAIT issues on restart
	ListenerSocket->SetReuseAddr(true);

	// Bind to localhost only (127.0.0.1) - NOT exposed to network
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
	Addr->SetPort(Port);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to bind to port %d"), Port);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	if (!ListenerSocket->Listen(MaxClients))
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	bShouldStop = false;
	Thread = FRunnableThread::Create(this, TEXT("UEEditorMCP Server Thread"));
	if (!Thread)
	{
		UE_LOG(LogMCP, Error, TEXT("UEEditorMCP: Failed to create server thread"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return false;
	}

	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server started on port %d (max %d clients)"), Port, MaxClients);
	return true;
}

void FMCPServer::Stop()
{
	if (bIsStopping)
	{
		return;
	}

	bIsStopping = true;
	bShouldStop = true;

	// Close listener to unblock WaitForPendingConnection
	if (ListenerSocket)
	{
		ListenerSocket->Close();
	}

	// Wait for accept loop to exit
	if (Thread)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}

	// Stop and delete all client handlers
	{
		FScopeLock Lock(&HandlersLock);
		for (FMCPClientHandler* Handler : ClientHandlers)
		{
			Handler->RequestStop();
			delete Handler;  // destructor waits for thread + destroys socket
		}
		ClientHandlers.Empty();
	}

	// Destroy listener socket
	if (ListenerSocket)
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (SocketSubsystem)
		{
			SocketSubsystem->DestroySocket(ListenerSocket);
		}
		ListenerSocket = nullptr;
	}

	bIsRunning = false;
	bIsStopping = false;
	UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Server stopped"));
}

bool FMCPServer::Init()
{
	return true;
}

uint32 FMCPServer::Run()
{
	bIsRunning = true;

	while (!bShouldStop)
	{
		// Clean up finished handlers periodically
		CleanupFinishedHandlers();

		// Wait for connection (with timeout so we can check bShouldStop)
		bool bPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bPendingConnection, FTimespan::FromSeconds(0.5)))
		{
			if (bPendingConnection)
			{
				FSocket* ClientSocket = ListenerSocket->Accept(TEXT("UEEditorMCP Client"));
				if (ClientSocket)
				{
					FScopeLock Lock(&HandlersLock);

					// Check if at capacity
					if (ClientHandlers.Num() >= MaxClients)
					{
						UE_LOG(LogMCP, Warning, TEXT("UEEditorMCP: Max clients (%d) reached, rejecting connection"), MaxClients);
						ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
						if (SocketSubsystem)
						{
							SocketSubsystem->DestroySocket(ClientSocket);
						}
						continue;
					}

					UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Client connected (total: %d)"), ClientHandlers.Num() + 1);

					// Create a handler that runs on its own thread
					FMCPClientHandler* Handler = new FMCPClientHandler(ClientSocket, Bridge, bShouldStop);
					ClientHandlers.Add(Handler);
				}
			}
		}
	}

	bIsRunning = false;
	return 0;
}

void FMCPServer::Exit()
{
	bIsRunning = false;
}

void FMCPServer::CleanupFinishedHandlers()
{
	FScopeLock Lock(&HandlersLock);

	for (int32 i = ClientHandlers.Num() - 1; i >= 0; --i)
	{
		if (ClientHandlers[i]->IsFinished())
		{
			UE_LOG(LogMCP, Log, TEXT("UEEditorMCP: Cleaning up finished client handler (remaining: %d)"), ClientHandlers.Num() - 1);
			delete ClientHandlers[i];
			ClientHandlers.RemoveAt(i);
		}
	}
}

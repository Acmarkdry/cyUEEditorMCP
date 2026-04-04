// Copyright (c) 2025 zolnoor. All rights reserved.

#include "UEEditorMCPCommandlet.h"
#include "MCPBridge.h"
#include "Actions/EditorAction.h"
#include "Editor.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogMCPCommandlet, Log, All);

UUEEditorMCPCommandlet::UUEEditorMCPCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UUEEditorMCPCommandlet::Main(const FString& Params)
{
	UE_LOG(LogMCPCommandlet, Log, TEXT("UEEditorMCP Commandlet starting with params: %s"), *Params);

	// Parse command-line switches
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> SwitchParams;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, SwitchParams);

	// Check for -help
	if (Switches.Contains(TEXT("help")))
	{
		return ShowHelp();
	}

	// Check for -format=json|markdown
	FString* FormatValue = SwitchParams.Find(TEXT("format"));
	if (FormatValue)
	{
		return ExportSchema(*FormatValue);
	}

	// Check for -json output mode
	bool bJsonOutput = Switches.Contains(TEXT("json"));

	// Check for -batch mode
	if (Switches.Contains(TEXT("batch")))
	{
		FString* FilePath = SwitchParams.Find(TEXT("file"));
		if (!FilePath || FilePath->IsEmpty())
		{
			UE_LOG(LogMCPCommandlet, Error, TEXT("Batch mode requires -file=<path>"));
			return 1;
		}
		return ExecuteBatch(*FilePath, bJsonOutput);
	}

	// Check for -command
	FString* CommandValue = SwitchParams.Find(TEXT("command"));
	if (!CommandValue || CommandValue->IsEmpty())
	{
		UE_LOG(LogMCPCommandlet, Error, TEXT("No command specified. Use -command=<name> or -help"));
		return 1;
	}

	// Get params (either -params="{...}" or key-value pairs)
	FString ParamsJson;
	FString* ParamsValue = SwitchParams.Find(TEXT("params"));
	if (ParamsValue && !ParamsValue->IsEmpty())
	{
		ParamsJson = *ParamsValue;
	}
	else
	{
		// Build JSON from remaining key-value switches
		TSharedPtr<FJsonObject> ParamsObj = MakeShared<FJsonObject>();
		for (auto& Pair : SwitchParams)
		{
			// Skip known switches
			if (Pair.Key == TEXT("command") || Pair.Key == TEXT("json") ||
				Pair.Key == TEXT("batch") || Pair.Key == TEXT("file") ||
				Pair.Key == TEXT("format") || Pair.Key == TEXT("help") ||
				Pair.Key == TEXT("run"))
			{
				continue;
			}
			ParamsObj->SetStringField(Pair.Key, Pair.Value);
		}

		FString ParamsStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ParamsStr);
		FJsonSerializer::Serialize(ParamsObj.ToSharedRef(), Writer);
		ParamsJson = ParamsStr;
	}

	return ExecuteSingleCommand(*CommandValue, ParamsJson, bJsonOutput);
}

int32 UUEEditorMCPCommandlet::ExecuteSingleCommand(const FString& Command, const FString& ParamsJson, bool bJsonOutput)
{
	// Get MCPBridge instance
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		FString Error = TEXT("{\"success\":false,\"error\":\"MCPBridge not available\"}");
		OutputResult(Error, bJsonOutput);
		return 1;
	}

	// Parse params JSON
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!ParamsJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
		if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
		{
			FString Error = TEXT("{\"success\":false,\"error\":\"Invalid JSON params\"}");
			OutputResult(Error, bJsonOutput);
			return 1;
		}
	}

	// Execute command
	TSharedPtr<FJsonObject> Result = Bridge->ExecuteCommandSafe(Command, Params);

	// Serialize result
	FString ResultStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultStr);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

	OutputResult(ResultStr, bJsonOutput);

	// Return exit code based on success
	return Result->GetBoolField(TEXT("success")) ? 0 : 1;
}

int32 UUEEditorMCPCommandlet::ExecuteBatch(const FString& FilePath, bool bJsonOutput)
{
	// Read batch file
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		FString Error = FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Failed to read batch file: %s\"}"),
			*FilePath
		);
		OutputResult(Error, bJsonOutput);
		return 1;
	}

	// Parse as JSON array
	TArray<TSharedPtr<FJsonValue>> Commands;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
	if (!FJsonSerializer::Deserialize(Reader, Commands))
	{
		FString Error = TEXT("{\"success\":false,\"error\":\"Invalid JSON in batch file. Expected array.\"}");
		OutputResult(Error, bJsonOutput);
		return 1;
	}

	// Get MCPBridge
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		FString Error = TEXT("{\"success\":false,\"error\":\"MCPBridge not available\"}");
		OutputResult(Error, bJsonOutput);
		return 1;
	}

	// Execute each command
	TArray<TSharedPtr<FJsonValue>> Results;
	bool bAllSuccess = true;

	for (int32 i = 0; i < Commands.Num(); ++i)
	{
		TSharedPtr<FJsonObject> CmdObj = Commands[i]->AsObject();
		if (!CmdObj.IsValid())
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(TEXT("success"), false);
			ErrorObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid command at index %d"), i));
			Results.Add(MakeShared<FJsonValueObject>(ErrorObj));
			bAllSuccess = false;
			continue;
		}

		FString Command = CmdObj->GetStringField(TEXT("command"));
		TSharedPtr<FJsonObject> Params;
		const TSharedPtr<FJsonObject>* ParamsPtr;
		if (CmdObj->TryGetObjectField(TEXT("params"), ParamsPtr))
		{
			Params = *ParamsPtr;
		}
		else
		{
			Params = MakeShared<FJsonObject>();
		}

		TSharedPtr<FJsonObject> Result = Bridge->ExecuteCommandSafe(Command, Params);
		Results.Add(MakeShared<FJsonValueObject>(Result));

		if (!Result->GetBoolField(TEXT("success")))
		{
			bAllSuccess = false;
		}
	}

	// Serialize all results as array
	FString ResultStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultStr);
	FJsonSerializer::Serialize(Results, Writer);

	OutputResult(ResultStr, bJsonOutput);

	return bAllSuccess ? 0 : 1;
}

int32 UUEEditorMCPCommandlet::ShowHelp()
{
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		UE_LOG(LogMCPCommandlet, Error, TEXT("MCPBridge not available"));
		return 1;
	}

	UE_LOG(LogMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogMCPCommandlet, Display, TEXT("=== UEEditorMCP Commandlet ==="));
	UE_LOG(LogMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogMCPCommandlet, Display, TEXT("Usage:"));
	UE_LOG(LogMCPCommandlet, Display, TEXT("  -run=UEEditorMCP -command=<name> [-params=\"{...}\"] [-json]"));
	UE_LOG(LogMCPCommandlet, Display, TEXT("  -run=UEEditorMCP -help"));
	UE_LOG(LogMCPCommandlet, Display, TEXT("  -run=UEEditorMCP -batch -file=<path> [-json]"));
	UE_LOG(LogMCPCommandlet, Display, TEXT("  -run=UEEditorMCP -format=json|markdown"));
	UE_LOG(LogMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogMCPCommandlet, Display, TEXT("Available commands:"));

	// We need to access ActionHandlers which is private.
	// For help, we'll execute a special action or use ExecuteCommand with a query.
	// Since we can't easily iterate private map, we log what we can.
	// A better approach: try to call each well-known command or just list them.
	
	// Use ping to verify bridge is working
	TSharedPtr<FJsonObject> PingParams = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> PingResult = Bridge->ExecuteCommandSafe(TEXT("ping"), PingParams);

	UE_LOG(LogMCPCommandlet, Display, TEXT("  (Use -format=json to export full command schema)"));
	UE_LOG(LogMCPCommandlet, Display, TEXT("  (Use -format=markdown for a readable table)"));
	UE_LOG(LogMCPCommandlet, Display, TEXT(""));

	return 0;
}

int32 UUEEditorMCPCommandlet::ExportSchema(const FString& Format)
{
	UMCPBridge* Bridge = GEditor ? GEditor->GetEditorSubsystem<UMCPBridge>() : nullptr;
	if (!Bridge)
	{
		UE_LOG(LogMCPCommandlet, Error, TEXT("MCPBridge not available"));
		return 1;
	}

	if (Format.Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		// Output JSON schema stub
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("plugin"), TEXT("UEEditorMCP"));
		Schema->SetStringField(TEXT("format"), TEXT("json"));
		Schema->SetStringField(TEXT("note"), TEXT("Full schema export requires runtime introspection of registered actions."));

		FString SchemaStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SchemaStr);
		FJsonSerializer::Serialize(Schema.ToSharedRef(), Writer);

		OutputResult(SchemaStr, true);
	}
	else if (Format.Equals(TEXT("markdown"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogMCPCommandlet, Display, TEXT("# UEEditorMCP Command Schema"));
		UE_LOG(LogMCPCommandlet, Display, TEXT(""));
		UE_LOG(LogMCPCommandlet, Display, TEXT("| Command | Description |"));
		UE_LOG(LogMCPCommandlet, Display, TEXT("|---------|-------------|"));
		UE_LOG(LogMCPCommandlet, Display, TEXT("| exec_python | Execute Python code in UE embedded Python |"));
		UE_LOG(LogMCPCommandlet, Display, TEXT("| ping | Health check |"));
		UE_LOG(LogMCPCommandlet, Display, TEXT("| ... | See docs/actions.md for full list |"));
	}
	else
	{
		UE_LOG(LogMCPCommandlet, Error, TEXT("Unknown format: %s. Use json or markdown."), *Format);
		return 1;
	}

	return 0;
}

void UUEEditorMCPCommandlet::OutputResult(const FString& JsonStr, bool bJsonOutput)
{
	if (bJsonOutput)
	{
		UE_LOG(LogMCPCommandlet, Display, TEXT("JSON_BEGIN"));
		UE_LOG(LogMCPCommandlet, Display, TEXT("%s"), *JsonStr);
		UE_LOG(LogMCPCommandlet, Display, TEXT("JSON_END"));
	}
	else
	{
		UE_LOG(LogMCPCommandlet, Display, TEXT("%s"), *JsonStr);
	}
}

// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "UEEditorMCPCommandlet.generated.h"

/**
 * UUEEditorMCPCommandlet
 *
 * Commandlet for executing MCP commands from the command line.
 * Reuses MCPBridge's ActionHandlers and ExecuteCommandSafe.
 *
 * Usage:
 *   UnrealEditor.exe <project> -run=UEEditorMCP -command=<name> -params="{...}"
 *   UnrealEditor.exe <project> -run=UEEditorMCP -help
 *   UnrealEditor.exe <project> -run=UEEditorMCP -batch -file=<path>
 *   UnrealEditor.exe <project> -run=UEEditorMCP -format=json|markdown
 */
UCLASS()
class UUEEditorMCPCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UUEEditorMCPCommandlet();

	virtual int32 Main(const FString& Params) override;

private:
	/** Execute a single command */
	int32 ExecuteSingleCommand(const FString& Command, const FString& ParamsJson, bool bJsonOutput);

	/** Execute batch commands from a file */
	int32 ExecuteBatch(const FString& FilePath, bool bJsonOutput);

	/** Show help output */
	int32 ShowHelp();

	/** Export command schema */
	int32 ExportSchema(const FString& Format);

	/** Output a JSON result with optional JSON_BEGIN/JSON_END wrapping */
	void OutputResult(const FString& JsonStr, bool bJsonOutput);
};

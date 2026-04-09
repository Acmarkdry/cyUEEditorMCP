// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/LiveCodingActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "ILiveCodingModule.h"
#include "Modules/ModuleManager.h"

// ============================================================================
// trigger_live_coding
// ============================================================================

TSharedPtr<FJsonObject> FTriggerLiveCodingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bWait = GetOptionalBool(Params, TEXT("wait"), false);
	int32 Timeout = static_cast<int32>(GetOptionalNumber(Params, TEXT("timeout"), 60.0));

	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		return CreateErrorResponse(TEXT("Live Coding module not available"));
	}

	if (!LiveCoding->IsEnabledByDefault())
	{
		return CreateErrorResponse(TEXT("Live Coding is disabled — enable in Editor Preferences → Live Coding"));
	}

	// Trigger compile
	LiveCoding->Compile();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("compile_triggered"));
	Result->SetBoolField(TEXT("wait_mode"), bWait);

	return CreateSuccessResponse(Result);
}

// ============================================================================
// get_live_coding_status
// ============================================================================

TSharedPtr<FJsonObject> FGetLiveCodingStatusAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!LiveCoding)
	{
		Result->SetBoolField(TEXT("available"), false);
		Result->SetStringField(TEXT("status"), TEXT("module_not_loaded"));
		return CreateSuccessResponse(Result);
	}

	Result->SetBoolField(TEXT("available"), true);
	Result->SetBoolField(TEXT("enabled"), LiveCoding->IsEnabledByDefault());
	Result->SetBoolField(TEXT("compiling"), LiveCoding->IsCompiling());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// enable_live_coding
// ============================================================================

TSharedPtr<FJsonObject> FEnableLiveCodingAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bDisable = GetOptionalBool(Params, TEXT("disable"), false);

	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		return CreateErrorResponse(TEXT("Live Coding module not available"));
	}

	LiveCoding->EnableByDefault(!bDisable);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), bDisable ? TEXT("disabled") : TEXT("enabled"));

	return CreateSuccessResponse(Result);
}

// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

// ============================================================================
// v0.3.0: Live Coding Actions
// ============================================================================

/** Trigger Live Coding compile. */
class UECLITOOL_API FTriggerLiveCodingAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("trigger_live_coding"); }
	virtual bool RequiresSave() const override { return false; }
	virtual bool IsWriteAction() const override { return true; }
};

/** Query Live Coding current status. */
class UECLITOOL_API FGetLiveCodingStatusAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("get_live_coding_status"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Enable or disable Live Coding. */
class UECLITOOL_API FEnableLiveCodingAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("enable_live_coding"); }
	virtual bool RequiresSave() const override { return false; }
	virtual bool IsWriteAction() const override { return true; }
};

// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

// ============================================================================
// v0.3.0: Blueprint Debug Actions
// ============================================================================

/** Set or remove a breakpoint on a Blueprint node. */
class UECLITOOL_API FSetBreakpointAction : public FBlueprintAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("set_breakpoint"); }
	virtual bool RequiresSave() const override { return false; }
	virtual bool IsWriteAction() const override { return true; }
};

/** List all breakpoints in a Blueprint or all Blueprints. */
class UECLITOOL_API FListBreakpointsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override { return true; }
	virtual FString GetActionName() const override { return TEXT("list_breakpoints"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Get runtime watch values for Blueprint variables (requires PIE). */
class UECLITOOL_API FGetWatchValuesAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_watch_values"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Debug step control (continue/step_over/step_into/step_out). EXPERIMENTAL. */
class UECLITOOL_API FDebugStepAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("debug_step"); }
	virtual bool RequiresSave() const override { return false; }
	virtual bool IsWriteAction() const override { return true; }
};

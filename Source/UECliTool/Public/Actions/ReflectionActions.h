// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

// ============================================================================
// v0.3.0: C++ Class Reflection Query Actions
// ============================================================================

/** List all UClasses derived from a base class. */
class UECLITOOL_API FListClassesAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("list_classes"); }
	virtual bool RequiresSave() const override { return false; }

private:
	void CollectDerivedClasses(UClass* BaseClass, int32 CurrentDepth, int32 MaxDepth,
		bool bIncludeAbstract, bool bNativeOnly, const FString& NameFilter,
		TArray<TSharedPtr<FJsonValue>>& OutArray, int32& Count, int32 MaxResults) const;
};

/** Get all UPROPERTY fields of a UClass. */
class UECLITOOL_API FGetClassPropertiesAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_class_properties"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Get all UFUNCTION methods of a UClass. */
class UECLITOOL_API FGetClassFunctionsAction : public FEditorAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("get_class_functions"); }
	virtual bool RequiresSave() const override { return false; }
};

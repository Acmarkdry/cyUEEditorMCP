// Copyright (c) 2025 zolnoor. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actions/EditorAction.h"

class UDataTable;

// ============================================================================
// P8: DataTable Actions
// ============================================================================

class UEEDITORMCP_API FDataTableAction : public FEditorAction
{
protected:
	UDataTable* FindDataTable(const FString& TableName, FString& OutError) const;
};

/** Create a DataTable asset. */
class UEEDITORMCP_API FCreateDataTableAction : public FDataTableAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("create_datatable"); }
};

/** Describe a DataTable (row struct, fields, row count, preview). */
class UEEDITORMCP_API FDescribeDataTableAction : public FDataTableAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("describe_datatable"); }
	virtual bool RequiresSave() const override { return false; }
};

/** Add a row to a DataTable. */
class UEEDITORMCP_API FAddDataTableRowAction : public FDataTableAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("add_datatable_row"); }
};

/** Delete a row from a DataTable. */
class UEEDITORMCP_API FDeleteDataTableRowAction : public FDataTableAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("delete_datatable_row"); }
};

/** Export a DataTable to JSON. */
class UEEDITORMCP_API FExportDataTableJsonAction : public FDataTableAction
{
public:
	virtual TSharedPtr<FJsonObject> ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context) override;
protected:
	virtual bool Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError) override;
	virtual FString GetActionName() const override { return TEXT("export_datatable_json"); }
	virtual bool RequiresSave() const override { return false; }
};

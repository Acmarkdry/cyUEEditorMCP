// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/DataTableActions.h"
#include "MCPCommonUtils.h"
#include "Engine/DataTable.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "DataTableEditorUtils.h"

// ============================================================================
// Helper
// ============================================================================

UDataTable* FDataTableAction::FindDataTable(const FString& TableName, FString& OutError) const
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetList;
	ARM.Get().GetAssetsByClass(UDataTable::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& Data : AssetList)
	{
		if (Data.AssetName.ToString() == TableName)
		{
			return Cast<UDataTable>(Data.GetAsset());
		}
	}

	TArray<FString> Similar = FMCPCommonUtils::FindSimilarAssets(TableName, 5);
	OutError = Similar.Num() > 0
		? FString::Printf(TEXT("DataTable '%s' not found. Did you mean: [%s]?"), *TableName, *FString::Join(Similar, TEXT(", ")))
		: FString::Printf(TEXT("DataTable '%s' not found"), *TableName);
	return nullptr;
}

// ============================================================================
// create_datatable
// ============================================================================

bool FCreateDataTableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("row_struct"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FCreateDataTableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name = Params->GetStringField(TEXT("name"));
	FString RowStructName = Params->GetStringField(TEXT("row_struct"));
	FString Path = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Data"));

	// Find the row struct
	UScriptStruct* RowStruct = FindFirstObject<UScriptStruct>(*RowStructName, EFindFirstObjectOptions::ExactClass);
	if (!RowStruct)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Row struct '%s' not found. Common structs: FTableRowBase, FDataTableRowHandle"), *RowStructName));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UDataTable* NewTable = NewObject<UDataTable>(CreatePackage(*FString::Printf(TEXT("%s/%s"), *Path, *Name)), *Name, RF_Public | RF_Standalone);
	NewTable->RowStruct = RowStruct;
	NewTable->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(NewTable);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), NewTable->GetName());
	Result->SetStringField(TEXT("path"), NewTable->GetPathName());
	Result->SetStringField(TEXT("row_struct"), RowStruct->GetName());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// describe_datatable
// ============================================================================

bool FDescribeDataTableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("table_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FDescribeDataTableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TableName = Params->GetStringField(TEXT("table_name"));
	FString Error;
	UDataTable* Table = FindDataTable(TableName, Error);
	if (!Table) return CreateErrorResponse(Error);

	// Collect field info from row struct
	TArray<TSharedPtr<FJsonValue>> FieldsArray;
	if (Table->RowStruct)
	{
		for (TFieldIterator<FProperty> It(Table->RowStruct); It; ++It)
		{
			TSharedPtr<FJsonObject> FieldObj = MakeShared<FJsonObject>();
			FieldObj->SetStringField(TEXT("name"), It->GetName());
			FieldObj->SetStringField(TEXT("type"), It->GetCPPType());
			FieldsArray.Add(MakeShared<FJsonValueObject>(FieldObj));
		}
	}

	// Collect row names
	TArray<TSharedPtr<FJsonValue>> RowNames;
	TArray<FName> Names = Table->GetRowNames();
	for (const FName& RN : Names)
	{
		RowNames.Add(MakeShared<FJsonValueString>(RN.ToString()));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Table->GetName());
	Result->SetStringField(TEXT("row_struct"), Table->RowStruct ? Table->RowStruct->GetName() : TEXT("None"));
	Result->SetArrayField(TEXT("fields"), FieldsArray);
	Result->SetArrayField(TEXT("row_names"), RowNames);
	Result->SetNumberField(TEXT("row_count"), Names.Num());
	Result->SetNumberField(TEXT("field_count"), FieldsArray.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// add_datatable_row
// ============================================================================

bool FAddDataTableRowAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("table_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("row_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddDataTableRowAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TableName = Params->GetStringField(TEXT("table_name"));
	FString RowName = Params->GetStringField(TEXT("row_name"));

	FString Error;
	UDataTable* Table = FindDataTable(TableName, Error);
	if (!Table) return CreateErrorResponse(Error);

	if (!Table->RowStruct)
	{
		return CreateErrorResponse(TEXT("DataTable has no row struct defined"));
	}

	// Check if row already exists
	if (Table->FindRowUnchecked(FName(*RowName)))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Row '%s' already exists"), *RowName));
	}

	// Add empty row
	FDataTableEditorUtils::AddRow(Table, FName(*RowName));
	Table->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("table_name"), TableName);
	Result->SetStringField(TEXT("row_name"), RowName);
	Result->SetNumberField(TEXT("total_rows"), Table->GetRowNames().Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// delete_datatable_row
// ============================================================================

bool FDeleteDataTableRowAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("table_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("row_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FDeleteDataTableRowAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TableName = Params->GetStringField(TEXT("table_name"));
	FString RowName = Params->GetStringField(TEXT("row_name"));

	FString Error;
	UDataTable* Table = FindDataTable(TableName, Error);
	if (!Table) return CreateErrorResponse(Error);

	if (!Table->FindRowUnchecked(FName(*RowName)))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Row '%s' not found in table"), *RowName));
	}

	FDataTableEditorUtils::RemoveRow(Table, FName(*RowName));
	Table->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("table_name"), TableName);
	Result->SetStringField(TEXT("deleted_row"), RowName);
	Result->SetNumberField(TEXT("remaining_rows"), Table->GetRowNames().Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// export_datatable_json
// ============================================================================

bool FExportDataTableJsonAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("table_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FExportDataTableJsonAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TableName = Params->GetStringField(TEXT("table_name"));
	FString Error;
	UDataTable* Table = FindDataTable(TableName, Error);
	if (!Table) return CreateErrorResponse(Error);

	FString JsonOutput = Table->GetTableAsJSON();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("table_name"), TableName);
	Result->SetStringField(TEXT("json"), JsonOutput);
	Result->SetNumberField(TEXT("row_count"), Table->GetRowNames().Num());

	return CreateSuccessResponse(Result);
}

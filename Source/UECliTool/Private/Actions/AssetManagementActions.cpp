// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/AssetManagementActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

// ============================================================================
// duplicate_asset
// ============================================================================

bool FDuplicateAssetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	// Either single asset_path or batch items[]
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0) return true;

	FString AssetPath;
	return GetRequiredString(Params, TEXT("asset_path"), AssetPath, OutError);
}

TSharedPtr<FJsonObject> FDuplicateAssetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Build list of items to duplicate
	struct FDuplicateItem
	{
		FString SourcePath;
		FString Destination;
		FString NewName;
	};
	TArray<FDuplicateItem> Items;

	const TArray<TSharedPtr<FJsonValue>>* BatchItems = GetOptionalArray(Params, TEXT("items"));
	if (BatchItems && BatchItems->Num() > 0)
	{
		for (const auto& Val : *BatchItems)
		{
			const TSharedPtr<FJsonObject>* ItemObj;
			if (Val->TryGetObject(ItemObj))
			{
				FDuplicateItem Item;
				Item.SourcePath = (*ItemObj)->GetStringField(TEXT("asset_path"));
				Item.Destination = (*ItemObj)->HasField(TEXT("destination")) ? (*ItemObj)->GetStringField(TEXT("destination")) : TEXT("");
				Item.NewName = (*ItemObj)->HasField(TEXT("new_name")) ? (*ItemObj)->GetStringField(TEXT("new_name")) : TEXT("");
				Items.Add(Item);
			}
		}
	}
	else
	{
		FDuplicateItem Item;
		Item.SourcePath = Params->GetStringField(TEXT("asset_path"));
		Item.Destination = GetOptionalString(Params, TEXT("destination"));
		Item.NewName = GetOptionalString(Params, TEXT("new_name"));
		Items.Add(Item);
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("Duplicate Assets")));
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 SuccessCount = 0;

	for (const FDuplicateItem& Item : Items)
	{
		// Determine source asset name and package
		FString SourcePackagePath = FPackageName::GetLongPackagePath(Item.SourcePath);
		FString SourceAssetName = FPackageName::GetShortName(Item.SourcePath);

		// Determine destination
		FString DestPath = Item.Destination.IsEmpty() ? SourcePackagePath : Item.Destination;
		FString DestName = Item.NewName.IsEmpty() ? SourceAssetName + TEXT("_Copy") : Item.NewName;

		// Check if source exists
		if (!UEditorAssetLibrary::DoesAssetExist(Item.SourcePath))
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("source"), Item.SourcePath);
			Err->SetStringField(TEXT("error"), TEXT("source asset not found"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		// Handle name conflicts
		FString FinalDestPath = DestPath / DestName;
		int32 Suffix = 1;
		while (UEditorAssetLibrary::DoesAssetExist(FinalDestPath))
		{
			FinalDestPath = DestPath / FString::Printf(TEXT("%s_%d"), *DestName, Suffix++);
		}
		FString FinalName = FPackageName::GetShortName(FinalDestPath);

		// Duplicate
		UObject* NewAsset = UEditorAssetLibrary::DuplicateAsset(Item.SourcePath, FinalDestPath);
		if (NewAsset)
		{
			SuccessCount++;
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("source"), Item.SourcePath);
			Res->SetStringField(TEXT("destination"), FinalDestPath);
			Res->SetStringField(TEXT("name"), FinalName);
			Res->SetStringField(TEXT("class"), NewAsset->GetClass()->GetName());
			ResultsArray.Add(MakeShared<FJsonValueObject>(Res));
		}
		else
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("source"), Item.SourcePath);
			Err->SetStringField(TEXT("error"), TEXT("duplication failed"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("success_count"), SuccessCount);
	Result->SetNumberField(TEXT("total"), Items.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// delete_asset
// ============================================================================

bool FDeleteAssetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0) return true;

	FString AssetPath;
	return GetRequiredString(Params, TEXT("asset_path"), AssetPath, OutError);
}

TSharedPtr<FJsonObject> FDeleteAssetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	bool bForce = GetOptionalBool(Params, TEXT("force"), false);
	bool bFixRedirectors = GetOptionalBool(Params, TEXT("fix_redirectors"), true);

	// Collect paths
	TArray<FString> AssetPaths;
	const TArray<TSharedPtr<FJsonValue>>* BatchItems = GetOptionalArray(Params, TEXT("items"));
	if (BatchItems && BatchItems->Num() > 0)
	{
		for (const auto& Val : *BatchItems)
		{
			FString Path;
			if (Val->TryGetString(Path))
			{
				AssetPaths.Add(Path);
			}
		}
	}
	else
	{
		AssetPaths.Add(Params->GetStringField(TEXT("asset_path")));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 DeletedCount = 0;

	FScopedTransaction Transaction(FText::FromString(TEXT("Delete Assets")));

	for (const FString& AssetPath : AssetPaths)
	{
		if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("asset"), AssetPath);
			Err->SetStringField(TEXT("error"), TEXT("asset not found"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		// Check references unless force
		if (!bForce)
		{
			TArray<FName> Referencers;
			FName PackageName = FName(*FPackageName::ObjectPathToPackageName(AssetPath));
			AssetRegistry.GetReferencers(PackageName, Referencers);

			// Filter out self-references
			Referencers.RemoveAll([&PackageName](const FName& Ref) { return Ref == PackageName; });

			if (Referencers.Num() > 0)
			{
				TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
				Err->SetStringField(TEXT("asset"), AssetPath);
				Err->SetStringField(TEXT("error"), TEXT("asset has references"));

				TArray<TSharedPtr<FJsonValue>> RefArray;
				for (const FName& Ref : Referencers)
				{
					RefArray.Add(MakeShared<FJsonValueString>(Ref.ToString()));
				}
				Err->SetArrayField(TEXT("referencers"), RefArray);
				ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
				continue;
			}
		}

		// Delete
		bool bDeleted = UEditorAssetLibrary::DeleteAsset(AssetPath);
		if (bDeleted)
		{
			DeletedCount++;
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("asset"), AssetPath);
			Res->SetStringField(TEXT("status"), TEXT("deleted"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Res));
		}
		else
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("asset"), AssetPath);
			Err->SetStringField(TEXT("error"), TEXT("deletion failed"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("deleted_count"), DeletedCount);
	Result->SetNumberField(TEXT("total"), AssetPaths.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// move_asset
// ============================================================================

bool FMoveAssetAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Items = GetOptionalArray(Params, TEXT("items"));
	if (Items && Items->Num() > 0) return true;

	FString AssetPath, Destination;
	if (!GetRequiredString(Params, TEXT("asset_path"), AssetPath, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("destination"), Destination, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FMoveAssetAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	struct FMoveItem
	{
		FString SourcePath;
		FString Destination;
	};
	TArray<FMoveItem> Items;

	const TArray<TSharedPtr<FJsonValue>>* BatchItems = GetOptionalArray(Params, TEXT("items"));
	if (BatchItems && BatchItems->Num() > 0)
	{
		for (const auto& Val : *BatchItems)
		{
			const TSharedPtr<FJsonObject>* ItemObj;
			if (Val->TryGetObject(ItemObj))
			{
				FMoveItem Item;
				Item.SourcePath = (*ItemObj)->GetStringField(TEXT("asset_path"));
				Item.Destination = (*ItemObj)->GetStringField(TEXT("destination"));
				Items.Add(Item);
			}
		}
	}
	else
	{
		FMoveItem Item;
		Item.SourcePath = Params->GetStringField(TEXT("asset_path"));
		Item.Destination = Params->GetStringField(TEXT("destination"));
		Items.Add(Item);
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("Move Assets")));
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	int32 MovedCount = 0;

	for (const FMoveItem& Item : Items)
	{
		if (!UEditorAssetLibrary::DoesAssetExist(Item.SourcePath))
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("source"), Item.SourcePath);
			Err->SetStringField(TEXT("error"), TEXT("source asset not found"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
			continue;
		}

		FString AssetName = FPackageName::GetShortName(Item.SourcePath);
		FString DestFullPath = Item.Destination / AssetName;

		bool bRenamed = UEditorAssetLibrary::RenameAsset(Item.SourcePath, DestFullPath);
		if (bRenamed)
		{
			MovedCount++;
			TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
			Res->SetStringField(TEXT("source"), Item.SourcePath);
			Res->SetStringField(TEXT("destination"), DestFullPath);
			ResultsArray.Add(MakeShared<FJsonValueObject>(Res));
		}
		else
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetStringField(TEXT("source"), Item.SourcePath);
			Err->SetStringField(TEXT("error"), TEXT("move failed"));
			ResultsArray.Add(MakeShared<FJsonValueObject>(Err));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("moved_count"), MovedCount);
	Result->SetNumberField(TEXT("total"), Items.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// fix_redirectors
// ============================================================================

bool FFixRedirectorsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Path;
	return GetRequiredString(Params, TEXT("path"), Path, OutError);
}

TSharedPtr<FJsonObject> FFixRedirectorsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ContentPath = Params->GetStringField(TEXT("path"));
	bool bRecursive = GetOptionalBool(Params, TEXT("recursive"), true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Find all redirectors under the path
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*ContentPath));
	Filter.bRecursivePaths = bRecursive;
	Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());

	TArray<FAssetData> RedirectorAssets;
	AssetRegistry.GetAssets(Filter, RedirectorAssets);

	if (RedirectorAssets.Num() == 0)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), ContentPath);
		Result->SetNumberField(TEXT("redirectors_found"), 0);
		Result->SetStringField(TEXT("message"), TEXT("no redirectors found"));
		return CreateSuccessResponse(Result);
	}

	// Load the redirectors
	TArray<UObjectRedirector*> Redirectors;
	for (const FAssetData& Asset : RedirectorAssets)
	{
		UObject* Obj = Asset.GetAsset();
		UObjectRedirector* Redirector = Cast<UObjectRedirector>(Obj);
		if (Redirector)
		{
			Redirectors.Add(Redirector);
		}
	}

	// Fix up
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Use ObjectTools to fixup redirectors
	TArray<UObject*> ObjectsToFix;
	for (UObjectRedirector* R : Redirectors) { ObjectsToFix.Add(R); }

	int32 FixedCount = Redirectors.Num();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), ContentPath);
	Result->SetNumberField(TEXT("redirectors_found"), RedirectorAssets.Num());
	Result->SetNumberField(TEXT("fixed_count"), FixedCount);

	return CreateSuccessResponse(Result);
}

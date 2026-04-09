// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/ContentBrowserActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// ============================================================================
// create_folder
// ============================================================================

bool FCreateFolderAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Path;
	return GetRequiredString(Params, TEXT("path"), Path, OutError);
}

TSharedPtr<FJsonObject> FCreateFolderAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ContentPath = Params->GetStringField(TEXT("path"));

	// Convert content path to filesystem path
	FString DiskPath;
	if (!FPackageName::TryConvertLongPackageNameToFilename(ContentPath, DiskPath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Invalid content path: %s"), *ContentPath));
	}

	// Check if already exists
	if (IFileManager::Get().DirectoryExists(*DiskPath))
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), ContentPath);
		Result->SetStringField(TEXT("status"), TEXT("already_exists"));
		return CreateSuccessResponse(Result);
	}

	// Create directory recursively
	bool bCreated = IFileManager::Get().MakeDirectory(*DiskPath, true);
	if (!bCreated)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to create directory: %s"), *ContentPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), ContentPath);
	Result->SetStringField(TEXT("status"), TEXT("created"));

	return CreateSuccessResponse(Result);
}

// ============================================================================
// get_asset_references
// ============================================================================

bool FGetAssetReferencesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString AssetPath;
	return GetRequiredString(Params, TEXT("asset_path"), AssetPath, OutError);
}

TSharedPtr<FJsonObject> FGetAssetReferencesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString Direction = GetOptionalString(Params, TEXT("direction"), TEXT("both"));
	bool bRecursive = GetOptionalBool(Params, TEXT("recursive"), false);
	int32 MaxDepth = static_cast<int32>(GetOptionalNumber(Params, TEXT("max_depth"), 3.0));

	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FName PackageName = FName(*FPackageName::ObjectPathToPackageName(AssetPath));
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Dependencies
	if (Direction == TEXT("dependencies") || Direction == TEXT("both"))
	{
		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(PackageName, Dependencies);

		TArray<TSharedPtr<FJsonValue>> DepsArray;
		for (const FName& Dep : Dependencies)
		{
			FString DepStr = Dep.ToString();
			// Skip engine/script packages
			if (DepStr.StartsWith(TEXT("/Script/")) || DepStr.StartsWith(TEXT("/Engine/")))
				continue;

			TSharedPtr<FJsonObject> DepObj = MakeShared<FJsonObject>();
			DepObj->SetStringField(TEXT("path"), DepStr);
			DepsArray.Add(MakeShared<FJsonValueObject>(DepObj));
		}
		Result->SetArrayField(TEXT("dependencies"), DepsArray);
		Result->SetNumberField(TEXT("dependency_count"), DepsArray.Num());
	}

	// Referencers
	if (Direction == TEXT("referencers") || Direction == TEXT("both"))
	{
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(PackageName, Referencers);

		// Filter out self
		Referencers.RemoveAll([&PackageName](const FName& Ref) { return Ref == PackageName; });

		TArray<TSharedPtr<FJsonValue>> RefsArray;
		for (const FName& Ref : Referencers)
		{
			FString RefStr = Ref.ToString();
			if (RefStr.StartsWith(TEXT("/Script/")) || RefStr.StartsWith(TEXT("/Engine/")))
				continue;

			TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
			RefObj->SetStringField(TEXT("path"), RefStr);
			RefsArray.Add(MakeShared<FJsonValueObject>(RefObj));
		}
		Result->SetArrayField(TEXT("referencers"), RefsArray);
		Result->SetNumberField(TEXT("referencer_count"), RefsArray.Num());
	}

	return CreateSuccessResponse(Result);
}

// ============================================================================
// validate_assets
// ============================================================================

bool FValidateAssetsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Path;
	return GetRequiredString(Params, TEXT("path"), Path, OutError);
}

TSharedPtr<FJsonObject> FValidateAssetsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ContentPath = Params->GetStringField(TEXT("path"));
	bool bRecursive = GetOptionalBool(Params, TEXT("recursive"), true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*ContentPath));
	Filter.bRecursivePaths = bRecursive;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> IssuesArray;
	int32 ScannedCount = Assets.Num();
	int32 ErrorCount = 0;
	int32 WarnCount = 0;

	for (const FAssetData& Asset : Assets)
	{
		FString PackageName = Asset.PackageName.ToString();

		// Check if package file exists on disk
		FString PackageFilename;
		bool bExists = FPackageName::DoesPackageExist(PackageName, &PackageFilename);
		if (!bExists)
		{
			TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
			Issue->SetStringField(TEXT("asset"), PackageName);
			Issue->SetStringField(TEXT("severity"), TEXT("ERROR"));
			Issue->SetStringField(TEXT("message"), TEXT("Package file not found on disk"));
			IssuesArray.Add(MakeShared<FJsonValueObject>(Issue));
			ErrorCount++;
			continue;
		}

		// Check for missing dependencies
		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(Asset.PackageName, Dependencies);

		for (const FName& Dep : Dependencies)
		{
			FString DepStr = Dep.ToString();
			// Skip engine/script packages
			if (DepStr.StartsWith(TEXT("/Script/")) || DepStr.StartsWith(TEXT("/Engine/")))
				continue;

			FString DepFilename;
			if (!FPackageName::DoesPackageExist(DepStr, &DepFilename))
			{
				TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("asset"), PackageName);
				Issue->SetStringField(TEXT("severity"), TEXT("ERROR"));
				Issue->SetStringField(TEXT("message"), FString::Printf(TEXT("Missing dependency: %s"), *DepStr));
				IssuesArray.Add(MakeShared<FJsonValueObject>(Issue));
				ErrorCount++;
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), ContentPath);
	Result->SetNumberField(TEXT("scanned"), ScannedCount);
	Result->SetArrayField(TEXT("issues"), IssuesArray);
	Result->SetNumberField(TEXT("error_count"), ErrorCount);
	Result->SetNumberField(TEXT("warning_count"), WarnCount);
	Result->SetBoolField(TEXT("healthy"), ErrorCount == 0 && WarnCount == 0);

	return CreateSuccessResponse(Result);
}

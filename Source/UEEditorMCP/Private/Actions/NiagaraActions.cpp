// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/NiagaraActions.h"
#include "MCPCommonUtils.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraComponent.h"
#include "NiagaraEditorModule.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/SavePackage.h"

// ============================================================================
// Helper: Find Niagara System by name
// ============================================================================

UNiagaraSystem* FNiagaraAction::FindNiagaraSystem(const FString& SystemName, FString& OutError) const
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> AssetList;
	AR.GetAssetsByClass(UNiagaraSystem::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& Data : AssetList)
	{
		if (Data.AssetName.ToString() == SystemName)
		{
			return Cast<UNiagaraSystem>(Data.GetAsset());
		}
	}

	TArray<FString> Similar = FMCPCommonUtils::FindSimilarAssets(SystemName, 5);
	if (Similar.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Niagara System '%s' not found. Did you mean: [%s]?"),
			*SystemName, *FString::Join(Similar, TEXT(", ")));
	}
	else
	{
		OutError = FString::Printf(TEXT("Niagara System '%s' not found"), *SystemName);
	}
	return nullptr;
}


// ============================================================================
// create_niagara_system
// ============================================================================

bool FCreateNiagaraSystemAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FCreateNiagaraSystemAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name = Params->GetStringField(TEXT("name"));
	FString Path = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Effects"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, UNiagaraSystem::StaticClass(), Factory);

	if (!NewAsset)
	{
		return CreateErrorResponse(TEXT("Failed to create Niagara System asset"), TEXT("creation_failed"));
	}

	UNiagaraSystem* System = Cast<UNiagaraSystem>(NewAsset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), System->GetName());
	Result->SetStringField(TEXT("path"), System->GetPathName());
	Result->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// describe_niagara_system
// ============================================================================

bool FDescribeNiagaraSystemAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("system_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FDescribeNiagaraSystemAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SystemName = Params->GetStringField(TEXT("system_name"));
	FString Error;
	UNiagaraSystem* System = FindNiagaraSystem(SystemName, Error);
	if (!System) return CreateErrorResponse(Error);

	TArray<TSharedPtr<FJsonValue>> EmittersArray;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
		EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
		EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
		EmitterObj->SetStringField(TEXT("unique_name"), Handle.GetUniqueInstanceName());

		// Get emitter data if available
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData)
		{
			EmitterObj->SetStringField(TEXT("sim_target"),
				EmitterData->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));
		}

		EmittersArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), System->GetName());
	Result->SetStringField(TEXT("path"), System->GetPathName());
	Result->SetArrayField(TEXT("emitters"), EmittersArray);
	Result->SetNumberField(TEXT("emitter_count"), EmittersArray.Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// add_niagara_emitter
// ============================================================================

bool FAddNiagaraEmitterAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("system_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("emitter_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddNiagaraEmitterAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SystemName = Params->GetStringField(TEXT("system_name"));
	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));

	FString Error;
	UNiagaraSystem* System = FindNiagaraSystem(SystemName, Error);
	if (!System) return CreateErrorResponse(Error);

	// Add a new empty emitter handle - requires a source emitter and version GUID
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	if (Handles.Num() == 0)
	{
		return CreateErrorResponse(TEXT("System has no existing emitters to use as template"), TEXT("no_emitters"));
	}
	UNiagaraEmitter* SourceEmitter = Handles.Last().GetInstance().Emitter;
	if (!SourceEmitter)
	{
		return CreateErrorResponse(TEXT("Failed to get source emitter"), TEXT("emitter_error"));
	}
	FNiagaraEmitterHandle NewHandle = System->AddEmitterHandle(*SourceEmitter, FName(*EmitterName), SourceEmitter->GetExposedVersion().VersionGuid);

	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system_name"), SystemName);
	Result->SetStringField(TEXT("emitter_name"), EmitterName);
	Result->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// remove_niagara_emitter
// ============================================================================

bool FRemoveNiagaraEmitterAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("system_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("emitter_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FRemoveNiagaraEmitterAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SystemName = Params->GetStringField(TEXT("system_name"));
	FString EmitterName = Params->GetStringField(TEXT("emitter_name"));

	FString Error;
	UNiagaraSystem* System = FindNiagaraSystem(SystemName, Error);
	if (!System) return CreateErrorResponse(Error);

	// Find and remove emitter by name
	bool bRemoved = false;
	for (int32 i = 0; i < System->GetEmitterHandles().Num(); ++i)
	{
		if (System->GetEmitterHandles()[i].GetName().ToString() == EmitterName)
		{
			System->RemoveEmitterHandle(System->GetEmitterHandles()[i]);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Emitter '%s' not found in system"), *EmitterName));
	}

	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system_name"), SystemName);
	Result->SetStringField(TEXT("removed_emitter"), EmitterName);
	Result->SetNumberField(TEXT("remaining_emitters"), System->GetEmitterHandles().Num());

	return CreateSuccessResponse(Result);
}


// ============================================================================
// set_niagara_module_param
// ============================================================================

bool FSetNiagaraModuleParamAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("system_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("parameter_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FSetNiagaraModuleParamAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SystemName = Params->GetStringField(TEXT("system_name"));
	FString ParamName = Params->GetStringField(TEXT("parameter_name"));

	FString Error;
	UNiagaraSystem* System = FindNiagaraSystem(SystemName, Error);
	if (!System) return CreateErrorResponse(Error);

	// Attempt to set user parameter via exposed parameters
	bool bFound = false;
	FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();
	for (const FNiagaraVariableWithOffset& VarWithOffset : ExposedParams.ReadParameterVariables())
	{
		if (VarWithOffset.GetName().ToString() == ParamName)
		{
			bFound = true;
			// Build a FNiagaraVariable to use SetParameterData
			FNiagaraVariable Var(VarWithOffset.GetType(), VarWithOffset.GetName());
			if (VarWithOffset.GetType() == FNiagaraTypeDefinition::GetFloatDef())
			{
				float Value = static_cast<float>(GetOptionalNumber(Params, TEXT("value"), 0.0));
				Var.SetValue(Value);
				ExposedParams.SetParameterData(Var.GetData(), VarWithOffset, true);
			}
			else if (VarWithOffset.GetType() == FNiagaraTypeDefinition::GetIntDef())
			{
				int32 Value = static_cast<int32>(GetOptionalNumber(Params, TEXT("value"), 0.0));
				Var.SetValue(Value);
				ExposedParams.SetParameterData(Var.GetData(), VarWithOffset, true);
			}
			else if (VarWithOffset.GetType() == FNiagaraTypeDefinition::GetBoolDef())
			{
				FNiagaraBool BoolVal;
				BoolVal.SetValue(GetOptionalBool(Params, TEXT("value"), false));
				Var.SetValue(BoolVal);
				ExposedParams.SetParameterData(Var.GetData(), VarWithOffset, true);
			}
			break;
		}
	}

	if (!bFound)
	{
		TArray<FString> Suggestions;
		for (const FNiagaraVariableWithOffset& VarInfo : ExposedParams.ReadParameterVariables())
		{
			Suggestions.Add(FString::Printf(TEXT("'%s' (%s)"), *VarInfo.GetName().ToString(), *VarInfo.GetType().GetName()));
		}
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Parameter '%s' not found"), *ParamName),
			TEXT("param_not_found"),
			Suggestions);
	}

	System->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system_name"), SystemName);
	Result->SetStringField(TEXT("parameter_name"), ParamName);
	Result->SetBoolField(TEXT("set"), true);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// compile_niagara_system
// ============================================================================

bool FCompileNiagaraSystemAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("system_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FCompileNiagaraSystemAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SystemName = Params->GetStringField(TEXT("system_name"));
	FString Error;
	UNiagaraSystem* System = FindNiagaraSystem(SystemName, Error);
	if (!System) return CreateErrorResponse(Error);

	System->RequestCompile(false);
	System->WaitForCompilationComplete();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("system_name"), SystemName);
	Result->SetBoolField(TEXT("compiled"), true);

	return CreateSuccessResponse(Result);
}


// ============================================================================
// get_niagara_modules — list available emitter/module templates
// ============================================================================

TSharedPtr<FJsonObject> FGetNiagaraModulesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	// List all NiagaraEmitter assets as potential templates
	TArray<FAssetData> EmitterAssets;
	AR.GetAssetsByClass(UNiagaraEmitter::StaticClass()->GetClassPathName(), EmitterAssets);

	FString Filter = GetOptionalString(Params, TEXT("filter"));

	TArray<TSharedPtr<FJsonValue>> TemplatesArray;
	for (const FAssetData& Data : EmitterAssets)
	{
		FString Name = Data.AssetName.ToString();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> TemplateObj = MakeShared<FJsonObject>();
		TemplateObj->SetStringField(TEXT("name"), Name);
		TemplateObj->SetStringField(TEXT("path"), Data.GetObjectPathString());
		TemplatesArray.Add(MakeShared<FJsonValueObject>(TemplateObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("emitter_templates"), TemplatesArray);
	Result->SetNumberField(TEXT("count"), TemplatesArray.Num());

	return CreateSuccessResponse(Result);
}

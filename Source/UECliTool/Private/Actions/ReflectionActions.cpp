// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/ReflectionActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "EditorAssetLibrary.h"

// Helper: find a UClass by name (supports both native classes and Blueprint generated classes)
static UClass* FindClassByName(const FString& ClassName)
{
	// Try native class first (UE 5.x: use FindFirstObject instead of deprecated ANY_PACKAGE)
	UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (FoundClass) return FoundClass;

	// Try with U prefix
	FoundClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName), EFindFirstObjectOptions::NativeFirst);
	if (FoundClass) return FoundClass;

	// Try with A prefix (actors)
	FoundClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName), EFindFirstObjectOptions::NativeFirst);
	if (FoundClass) return FoundClass;

	// Try Blueprint generated class
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, true);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString() == ClassName)
		{
			UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
			if (BP && BP->GeneratedClass)
			{
				return BP->GeneratedClass;
			}
		}
	}

	return nullptr;
}

// Helper: get property type as human-readable string
static FString GetPropertyTypeString(FProperty* Property)
{
	if (!Property) return TEXT("Unknown");

	if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		return TEXT("Bool");
	if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
		return TEXT("Int");
	if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
		return TEXT("Int64");
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
		return TEXT("Float");
	if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
		return TEXT("Double");
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
		return TEXT("String");
	if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
		return TEXT("Name");
	if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
		return TEXT("Text");
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
		return FString::Printf(TEXT("Object<%s>"), *ObjProp->PropertyClass->GetName());
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
		return FString::Printf(TEXT("Class<%s>"), *ClassProp->MetaClass->GetName());
	if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Property))
		return TEXT("SoftObject");
	if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Property))
		return FString::Printf(TEXT("Array<%s>"), *GetPropertyTypeString(ArrProp->Inner));
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
		return TEXT("Set");
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
		return TEXT("Map");
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		return FString::Printf(TEXT("Struct<%s>"), *StructProp->Struct->GetName());
	if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		return FString::Printf(TEXT("Enum<%s>"), *EnumProp->GetEnum()->GetName());
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
			return FString::Printf(TEXT("Enum<%s>"), *ByteProp->Enum->GetName());
		return TEXT("Byte");
	}

	return Property->GetCPPType();
}

// ============================================================================
// list_classes
// ============================================================================

bool FListClassesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString BaseClass;
	return GetRequiredString(Params, TEXT("base_class"), BaseClass, OutError);
}

void FListClassesAction::CollectDerivedClasses(UClass* BaseClass, int32 CurrentDepth, int32 MaxDepth,
	bool bIncludeAbstract, bool bNativeOnly, const FString& NameFilter,
	TArray<TSharedPtr<FJsonValue>>& OutArray, int32& Count, int32 MaxResults) const
{
	if (Count >= MaxResults) return;
	if (MaxDepth >= 0 && CurrentDepth > MaxDepth) return;

	TArray<UClass*> DirectChildren;
	GetDerivedClasses(BaseClass, DirectChildren, false);

	for (UClass* Child : DirectChildren)
	{
		if (Count >= MaxResults) break;
		if (!Child) continue;

		// Filter abstract
		if (!bIncludeAbstract && Child->HasAnyClassFlags(CLASS_Abstract)) continue;

		// Filter native only
		if (bNativeOnly && !Child->HasAnyClassFlags(CLASS_Native)) continue;

		// Name filter (simple wildcard)
		if (!NameFilter.IsEmpty())
		{
			FString ChildName = Child->GetName();
			if (!ChildName.Contains(NameFilter))
				continue;
		}

		TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
		ClassObj->SetStringField(TEXT("name"), Child->GetName());
		ClassObj->SetStringField(TEXT("path"), Child->GetPathName());
		ClassObj->SetNumberField(TEXT("depth"), CurrentDepth);
		ClassObj->SetBoolField(TEXT("abstract"), Child->HasAnyClassFlags(CLASS_Abstract));
		ClassObj->SetBoolField(TEXT("native"), Child->HasAnyClassFlags(CLASS_Native));
		OutArray.Add(MakeShared<FJsonValueObject>(ClassObj));
		Count++;

		// Recurse
		if (MaxDepth < 0 || CurrentDepth < MaxDepth)
		{
			CollectDerivedClasses(Child, CurrentDepth + 1, MaxDepth, bIncludeAbstract, bNativeOnly, NameFilter, OutArray, Count, MaxResults);
		}
	}
}

TSharedPtr<FJsonObject> FListClassesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString BaseClassName = Params->GetStringField(TEXT("base_class"));
	bool bIncludeAbstract = GetOptionalBool(Params, TEXT("include_abstract"), false);
	bool bNativeOnly = GetOptionalBool(Params, TEXT("include_native_only"), false);
	int32 MaxDepth = static_cast<int32>(GetOptionalNumber(Params, TEXT("max_depth"), -1.0));
	FString NameFilter = GetOptionalString(Params, TEXT("name_filter"));
	int32 MaxResults = static_cast<int32>(GetOptionalNumber(Params, TEXT("max_results"), 200.0));

	UClass* BaseClass = FindClassByName(BaseClassName);
	if (!BaseClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class '%s' not found"), *BaseClassName));
	}

	TArray<TSharedPtr<FJsonValue>> ClassesArray;
	int32 Count = 0;

	CollectDerivedClasses(BaseClass, 1, MaxDepth, bIncludeAbstract, bNativeOnly, NameFilter, ClassesArray, Count, MaxResults);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("base_class"), BaseClass->GetName());
	Result->SetArrayField(TEXT("classes"), ClassesArray);
	Result->SetNumberField(TEXT("count"), ClassesArray.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// get_class_properties
// ============================================================================

bool FGetClassPropertiesAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ClassName;
	return GetRequiredString(Params, TEXT("class_name"), ClassName, OutError);
}

TSharedPtr<FJsonObject> FGetClassPropertiesAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ClassName = Params->GetStringField(TEXT("class_name"));
	bool bIncludeInherited = GetOptionalBool(Params, TEXT("include_inherited"), false);
	FString CategoryFilter = GetOptionalString(Params, TEXT("category"));
	bool bIncludeMeta = GetOptionalBool(Params, TEXT("include_meta"), false);

	UClass* TargetClass = FindClassByName(ClassName);
	if (!TargetClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class '%s' not found"), *ClassName));
	}

	TArray<TSharedPtr<FJsonValue>> PropertiesArray;
	EFieldIterationFlags IterFlags = bIncludeInherited
		? EFieldIterationFlags::IncludeSuper
		: EFieldIterationFlags::None;

	for (TFieldIterator<FProperty> It(TargetClass, IterFlags); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property) continue;

		// Category filter
		FString Category = Property->GetMetaData(TEXT("Category"));
		if (!CategoryFilter.IsEmpty() && !Category.Contains(CategoryFilter))
			continue;

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Property->GetName());
		PropObj->SetStringField(TEXT("type"), GetPropertyTypeString(Property));
		PropObj->SetStringField(TEXT("category"), Category);
		PropObj->SetStringField(TEXT("owner"), Property->GetOwnerClass() ? Property->GetOwnerClass()->GetName() : TEXT("Unknown"));

		// Flags
		TArray<FString> FlagStrings;
		if (Property->HasAnyPropertyFlags(CPF_Edit))
			FlagStrings.Add(TEXT("EditAnywhere"));
		if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
			FlagStrings.Add(TEXT("BlueprintVisible"));
		if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
			FlagStrings.Add(TEXT("BlueprintReadOnly"));
		if (Property->HasAnyPropertyFlags(CPF_Net))
			FlagStrings.Add(TEXT("Replicated"));

		PropObj->SetStringField(TEXT("flags"), FString::Join(FlagStrings, TEXT("|")));

		if (bIncludeMeta)
		{
			TSharedPtr<FJsonObject> MetaObj = MakeShared<FJsonObject>();
			const TMap<FName, FString>* MetaMap = Property->GetMetaDataMap();
			if (MetaMap)
			{
				for (const auto& Pair : *MetaMap)
				{
					MetaObj->SetStringField(Pair.Key.ToString(), Pair.Value);
				}
			}
			PropObj->SetObjectField(TEXT("metadata"), MetaObj);
		}

		PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("class_name"), TargetClass->GetName());
	Result->SetArrayField(TEXT("properties"), PropertiesArray);
	Result->SetNumberField(TEXT("count"), PropertiesArray.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// get_class_functions
// ============================================================================

bool FGetClassFunctionsAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString ClassName;
	return GetRequiredString(Params, TEXT("class_name"), ClassName, OutError);
}

TSharedPtr<FJsonObject> FGetClassFunctionsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString ClassName = Params->GetStringField(TEXT("class_name"));
	bool bIncludeInherited = GetOptionalBool(Params, TEXT("include_inherited"), false);
	bool bCallableOnly = GetOptionalBool(Params, TEXT("callable_only"), false);
	FString NameFilter = GetOptionalString(Params, TEXT("name_filter"));

	UClass* TargetClass = FindClassByName(ClassName);
	if (!TargetClass)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Class '%s' not found"), *ClassName));
	}

	TArray<TSharedPtr<FJsonValue>> FunctionsArray;
	EFieldIterationFlags IterFlags = bIncludeInherited
		? EFieldIterationFlags::IncludeSuper
		: EFieldIterationFlags::None;

	for (TFieldIterator<UFunction> It(TargetClass, IterFlags); It; ++It)
	{
		UFunction* Function = *It;
		if (!Function) continue;

		// Callable only filter
		if (bCallableOnly && !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			continue;

		// Name filter
		if (!NameFilter.IsEmpty() && !Function->GetName().Contains(NameFilter))
			continue;

		TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Function->GetName());
		FuncObj->SetStringField(TEXT("owner"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetName() : TEXT("Unknown"));

		// Return type
		FProperty* ReturnProp = Function->GetReturnProperty();
		FuncObj->SetStringField(TEXT("return_type"), ReturnProp ? GetPropertyTypeString(ReturnProp) : TEXT("void"));

		// Parameters
		TArray<TSharedPtr<FJsonValue>> ParamsArray;
		for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
		{
			FProperty* Param = *ParamIt;
			if (!Param || Param->HasAnyPropertyFlags(CPF_ReturnParm)) continue;

			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), Param->GetName());
			ParamObj->SetStringField(TEXT("type"), GetPropertyTypeString(Param));
			ParamObj->SetBoolField(TEXT("is_output"), Param->HasAnyPropertyFlags(CPF_OutParm));
			ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		FuncObj->SetArrayField(TEXT("params"), ParamsArray);

		// Flags
		TArray<FString> FlagStrings;
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
			FlagStrings.Add(TEXT("BlueprintCallable"));
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
			FlagStrings.Add(TEXT("BlueprintEvent"));
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
			FlagStrings.Add(TEXT("BlueprintPure"));
		if (Function->HasAnyFunctionFlags(FUNC_Native))
			FlagStrings.Add(TEXT("Native"));
		if (Function->HasAnyFunctionFlags(FUNC_Static))
			FlagStrings.Add(TEXT("Static"));

		FuncObj->SetStringField(TEXT("flags"), FString::Join(FlagStrings, TEXT("|")));

		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("class_name"), TargetClass->GetName());
	Result->SetArrayField(TEXT("functions"), FunctionsArray);
	Result->SetNumberField(TEXT("count"), FunctionsArray.Num());

	return CreateSuccessResponse(Result);
}

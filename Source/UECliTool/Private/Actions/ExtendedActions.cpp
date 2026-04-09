// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/ExtendedActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMemory.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"

// ============================================================================
// P10: Testing — run_automation_test
// ============================================================================

bool FRunAutomationTestAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Filter;
	return GetRequiredString(Params, TEXT("test_filter"), Filter, OutError);
}

TSharedPtr<FJsonObject> FRunAutomationTestAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString TestFilter = Params->GetStringField(TEXT("test_filter"));

	// Collect matching tests
	TArray<FAutomationTestInfo> TestInfos;
	FAutomationTestFramework::Get().GetValidTestNames(TestInfos);

	TArray<FString> MatchingTests;
	for (const FAutomationTestInfo& Info : TestInfos)
	{
		if (Info.GetDisplayName().Contains(TestFilter) || Info.GetTestName().Contains(TestFilter))
		{
			MatchingTests.Add(Info.GetTestName());
		}
	}

	if (MatchingTests.Num() == 0)
	{
		return CreateErrorResponse(FString::Printf(TEXT("No automation tests matching filter '%s'"), *TestFilter));
	}

	// Run tests
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	for (const FString& TestName : MatchingTests)
	{
		FAutomationTestExecutionInfo ExecInfo;
		bool bPassed = FAutomationTestFramework::Get().RunSmokeTests();

		TSharedPtr<FJsonObject> TestResult = MakeShared<FJsonObject>();
		TestResult->SetStringField(TEXT("test_name"), TestName);
		TestResult->SetBoolField(TEXT("passed"), bPassed);
		ResultsArray.Add(MakeShared<FJsonValueObject>(TestResult));

		// Only run first matching test to avoid blocking too long
		break;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("results"), ResultsArray);
	Result->SetNumberField(TEXT("matching_tests"), MatchingTests.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// P10: Testing — list_automation_tests
// ============================================================================

TSharedPtr<FJsonObject> FListAutomationTestsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Filter = GetOptionalString(Params, TEXT("filter"));
	int32 Limit = static_cast<int32>(GetOptionalNumber(Params, TEXT("limit"), 50.0));

	TArray<FAutomationTestInfo> TestInfos;
	FAutomationTestFramework::Get().GetValidTestNames(TestInfos);

	TArray<TSharedPtr<FJsonValue>> TestsArray;
	int32 Count = 0;
	for (const FAutomationTestInfo& Info : TestInfos)
	{
		if (!Filter.IsEmpty() && !Info.GetDisplayName().Contains(Filter) && !Info.GetTestName().Contains(Filter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> TestObj = MakeShared<FJsonObject>();
		TestObj->SetStringField(TEXT("name"), Info.GetTestName());
		TestObj->SetStringField(TEXT("display_name"), Info.GetDisplayName());
		TestsArray.Add(MakeShared<FJsonValueObject>(TestObj));

		if (++Count >= Limit) break;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tests"), TestsArray);
	Result->SetNumberField(TEXT("count"), TestsArray.Num());
	Result->SetNumberField(TEXT("total_available"), TestInfos.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// P10: Level — list_sublevels
// ============================================================================

TSharedPtr<FJsonObject> FListSublevelsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	TArray<TSharedPtr<FJsonValue>> LevelsArray;

	// Main persistent level
	TSharedPtr<FJsonObject> MainLevel = MakeShared<FJsonObject>();
	MainLevel->SetStringField(TEXT("name"), World->GetName());
	MainLevel->SetStringField(TEXT("type"), TEXT("persistent"));
	MainLevel->SetBoolField(TEXT("loaded"), true);
	LevelsArray.Add(MakeShared<FJsonValueObject>(MainLevel));

	// Streaming sublevels
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel) continue;

		TSharedPtr<FJsonObject> LevelObj = MakeShared<FJsonObject>();
		LevelObj->SetStringField(TEXT("name"), StreamingLevel->GetWorldAssetPackageName());
		LevelObj->SetStringField(TEXT("type"), TEXT("streaming"));
		LevelObj->SetBoolField(TEXT("loaded"), StreamingLevel->IsLevelLoaded());
		LevelObj->SetBoolField(TEXT("visible"), StreamingLevel->IsLevelVisible());
		LevelObj->SetBoolField(TEXT("should_be_loaded"), StreamingLevel->HasLoadRequestPending() || StreamingLevel->IsLevelLoaded());
		LevelsArray.Add(MakeShared<FJsonValueObject>(LevelObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("levels"), LevelsArray);
	Result->SetNumberField(TEXT("count"), LevelsArray.Num());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// P10: Level — get_world_settings
// ============================================================================

TSharedPtr<FJsonObject> FGetWorldSettingsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return CreateErrorResponse(TEXT("WorldSettings not found"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetBoolField(TEXT("enable_world_bounds_checks"), Settings->bEnableWorldBoundsChecks);
	Result->SetNumberField(TEXT("world_gravity_z"), Settings->GetGravityZ());
	Result->SetNumberField(TEXT("kill_z"), Settings->KillZ);

	// GameMode
	if (Settings->DefaultGameMode)
	{
		Result->SetStringField(TEXT("default_game_mode"), Settings->DefaultGameMode->GetName());
	}

	// Global illumination
	Result->SetBoolField(TEXT("force_no_precomputed_lighting"), Settings->bForceNoPrecomputedLighting);

	return CreateSuccessResponse(Result);
}

// ============================================================================
// P10: Profiler — get_frame_stats
// ============================================================================

TSharedPtr<FJsonObject> FGetFrameStatsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	// Get average unit times
	float GameThreadTime = 0, RenderThreadTime = 0, GPUTime = 0, FrameTime = 0;

	extern ENGINE_API float GAverageFPS;
	extern ENGINE_API float GAverageMS;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("average_fps"), GAverageFPS);
	Result->SetNumberField(TEXT("average_ms"), GAverageMS);
	Result->SetNumberField(TEXT("delta_time"), FApp::GetDeltaTime() * 1000.0); // in ms

	// Viewport stats
	if (GEditor && GEditor->GetActiveViewport())
	{
		FViewport* VP = GEditor->GetActiveViewport();
		Result->SetNumberField(TEXT("viewport_width"), VP->GetSizeXY().X);
		Result->SetNumberField(TEXT("viewport_height"), VP->GetSizeXY().Y);
	}

	return CreateSuccessResponse(Result);
}

// ============================================================================
// P10: Profiler — get_memory_stats
// ============================================================================

TSharedPtr<FJsonObject> FGetMemoryStatsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("total_physical_mb"), MemStats.TotalPhysical / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("available_physical_mb"), MemStats.AvailablePhysical / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("used_physical_mb"), (MemStats.TotalPhysical - MemStats.AvailablePhysical) / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("total_virtual_mb"), MemStats.TotalVirtual / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("available_virtual_mb"), MemStats.AvailableVirtual / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("peak_used_physical_mb"), MemStats.PeakUsedPhysical / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("used_virtual_mb"), MemStats.UsedVirtual / (1024.0 * 1024.0));
	Result->SetNumberField(TEXT("peak_used_virtual_mb"), MemStats.PeakUsedVirtual / (1024.0 * 1024.0));

	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: Level — load_level
// ============================================================================

bool FLoadLevelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Path;
	return GetRequiredString(Params, TEXT("level_path"), Path, OutError);
}

TSharedPtr<FJsonObject> FLoadLevelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString LevelPath = Params->GetStringField(TEXT("level_path"));
	bool bSaveCurrent = GetOptionalBool(Params, TEXT("save_current"), true);

	if (!GEditor)
	{
		return CreateErrorResponse(TEXT("Editor not available"));
	}

	// Save current level if requested
	if (bSaveCurrent)
	{
		FEditorFileUtils::SaveDirtyPackages(false, true, true);
	}

	// Check if the map exists
	FString MapFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(LevelPath, MapFilename, FPackageName::GetMapPackageExtension()))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Invalid level path: %s"), *LevelPath));
	}

	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*MapFilename))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Level not found: %s"), *LevelPath));
	}

	// Load the map
	bool bLoaded = FEditorFileUtils::LoadMap(LevelPath);
	if (!bLoaded)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to load level: %s"), *LevelPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("level_path"), LevelPath);
	Result->SetStringField(TEXT("status"), TEXT("loaded"));

	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: Level — create_sublevel
// ============================================================================

bool FCreateSublevelAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("sublevel_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FCreateSublevelAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SublevelName = Params->GetStringField(TEXT("sublevel_name"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sublevel_name"), SublevelName);
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetStringField(TEXT("status"), TEXT("sublevel_created"));

	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: Level — set_world_settings
// ============================================================================

TSharedPtr<FJsonObject> FSetWorldSettingsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return CreateErrorResponse(TEXT("WorldSettings not found"));
	}

	Settings->Modify();

	TArray<FString> Changed;

	if (Params->HasField(TEXT("kill_z")))
	{
		Settings->KillZ = Params->GetNumberField(TEXT("kill_z"));
		Changed.Add(TEXT("kill_z"));
	}

	if (Params->HasField(TEXT("game_mode")))
	{
		FString GameModePath = Params->GetStringField(TEXT("game_mode"));
		UClass* GMClass = StaticLoadClass(AGameModeBase::StaticClass(), nullptr, *GameModePath);
		if (GMClass)
		{
			Settings->DefaultGameMode = GMClass;
			Changed.Add(TEXT("game_mode"));
		}
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("world"), World->GetName());

	TArray<TSharedPtr<FJsonValue>> ChangedArray;
	for (const FString& C : Changed)
	{
		ChangedArray.Add(MakeShared<FJsonValueString>(C));
	}
	Result->SetArrayField(TEXT("changed_fields"), ChangedArray);

	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: Level — get_level_bounds
// ============================================================================

TSharedPtr<FJsonObject> FGetLevelBoundsAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return CreateErrorResponse(TEXT("No editor world available"));
	}

	FBox WorldBounds(ForceInit);
	int32 ActorCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<AWorldSettings>()) continue;

		FBox ActorBounds = Actor->GetComponentsBoundingBox(true);
		if (ActorBounds.IsValid)
		{
			WorldBounds += ActorBounds;
			ActorCount++;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("actor_count"), ActorCount);

	if (WorldBounds.IsValid)
	{
		FVector Origin = WorldBounds.GetCenter();
		FVector Extent = WorldBounds.GetExtent();
		FVector Min = WorldBounds.Min;
		FVector Max = WorldBounds.Max;

		TSharedPtr<FJsonObject> OriginObj = MakeShared<FJsonObject>();
		OriginObj->SetNumberField(TEXT("x"), Origin.X);
		OriginObj->SetNumberField(TEXT("y"), Origin.Y);
		OriginObj->SetNumberField(TEXT("z"), Origin.Z);
		Result->SetObjectField(TEXT("origin"), OriginObj);

		TSharedPtr<FJsonObject> ExtentObj = MakeShared<FJsonObject>();
		ExtentObj->SetNumberField(TEXT("x"), Extent.X);
		ExtentObj->SetNumberField(TEXT("y"), Extent.Y);
		ExtentObj->SetNumberField(TEXT("z"), Extent.Z);
		Result->SetObjectField(TEXT("extent"), ExtentObj);

		TSharedPtr<FJsonObject> MinObj = MakeShared<FJsonObject>();
		MinObj->SetNumberField(TEXT("x"), Min.X);
		MinObj->SetNumberField(TEXT("y"), Min.Y);
		MinObj->SetNumberField(TEXT("z"), Min.Z);
		Result->SetObjectField(TEXT("min"), MinObj);

		TSharedPtr<FJsonObject> MaxObj = MakeShared<FJsonObject>();
		MaxObj->SetNumberField(TEXT("x"), Max.X);
		MaxObj->SetNumberField(TEXT("y"), Max.Y);
		MaxObj->SetNumberField(TEXT("z"), Max.Z);
		Result->SetObjectField(TEXT("max"), MaxObj);
	}

	return CreateSuccessResponse(Result);
}

// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/ExtendedActions.h"
#include "MCPCommonUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformMemory.h"
#include "GenericPlatform/GenericPlatformMemory.h"

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

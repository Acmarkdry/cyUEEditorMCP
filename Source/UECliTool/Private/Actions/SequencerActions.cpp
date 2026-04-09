// Copyright (c) 2025 zolnoor. All rights reserved.

#include "Actions/SequencerActions.h"
#include "MCPCommonUtils.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FrameRate.h"

// ============================================================================
// Helper
// ============================================================================

ULevelSequence* FSequencerAction::FindLevelSequence(const FString& Name, FString& OutError) const
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetList;
	ARM.Get().GetAssetsByClass(ULevelSequence::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& Data : AssetList)
	{
		if (Data.AssetName.ToString() == Name)
		{
			return Cast<ULevelSequence>(Data.GetAsset());
		}
	}

	TArray<FString> Similar = FMCPCommonUtils::FindSimilarAssets(Name, 5);
	OutError = Similar.Num() > 0
		? FString::Printf(TEXT("LevelSequence '%s' not found. Did you mean: [%s]?"), *Name, *FString::Join(Similar, TEXT(", ")))
		: FString::Printf(TEXT("LevelSequence '%s' not found"), *Name);
	return nullptr;
}

// ============================================================================
// create_level_sequence
// ============================================================================

bool FCreateLevelSequenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("name"), Name, OutError);
}

TSharedPtr<FJsonObject> FCreateLevelSequenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString Name = Params->GetStringField(TEXT("name"));
	FString Path = GetOptionalString(Params, TEXT("path"), TEXT("/Game/Cinematics"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, Path, ULevelSequence::StaticClass(), nullptr);

	if (!NewAsset)
	{
		return CreateErrorResponse(TEXT("Failed to create LevelSequence asset"), TEXT("creation_failed"));
	}

	ULevelSequence* Sequence = Cast<ULevelSequence>(NewAsset);
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	// Set default playback range if requested
	double Duration = GetOptionalNumber(Params, TEXT("duration_seconds"), 5.0);
	if (MovieScene)
	{
		FFrameRate FrameRate = MovieScene->GetTickResolution();
		FFrameNumber EndFrame = (Duration * FrameRate).FloorToFrame();
		MovieScene->SetPlaybackRange(FFrameNumber(0), EndFrame.Value);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Sequence->GetName());
	Result->SetStringField(TEXT("path"), Sequence->GetPathName());
	Result->SetNumberField(TEXT("duration_seconds"), Duration);

	return CreateSuccessResponse(Result);
}

// ============================================================================
// describe_level_sequence
// ============================================================================

bool FDescribeLevelSequenceAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("sequence_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FDescribeLevelSequenceAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqName = Params->GetStringField(TEXT("sequence_name"));
	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqName, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Collect bindings
	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		TSharedPtr<FJsonObject> BindObj = MakeShared<FJsonObject>();
		BindObj->SetStringField(TEXT("name"), Possessable.GetName());
		BindObj->SetStringField(TEXT("guid"), Possessable.GetGuid().ToString());
		BindObj->SetStringField(TEXT("class"), Possessable.GetPossessedObjectClass() ?
			Possessable.GetPossessedObjectClass()->GetName() : TEXT("Unknown"));
		BindingsArray.Add(MakeShared<FJsonValueObject>(BindObj));
	}

	// Collect master tracks
	TArray<TSharedPtr<FJsonValue>> TracksArray;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
		TrackObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
		TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
		TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
	}

	// Playback range
	FFrameRate FrameRate = MovieScene->GetTickResolution();
	TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Sequence->GetName());
	Result->SetArrayField(TEXT("bindings"), BindingsArray);
	Result->SetArrayField(TEXT("master_tracks"), TracksArray);
	Result->SetNumberField(TEXT("binding_count"), BindingsArray.Num());
	Result->SetNumberField(TEXT("track_count"), TracksArray.Num());
	Result->SetNumberField(TEXT("frame_rate"), FrameRate.AsDecimal());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// add_sequencer_possessable
// ============================================================================

bool FAddSequencerPossessableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("sequence_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("actor_name"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddSequencerPossessableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqName = Params->GetStringField(TEXT("sequence_name"));
	FString ActorName = Params->GetStringField(TEXT("actor_name"));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqName, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Find actor in level
	UWorld* World = GEditor->GetEditorWorldContext().World();
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
		{
			FoundActor = *It;
			break;
		}
	}

	if (!FoundActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName));
	}

	// Add possessable
	FGuid BindingGuid = MovieScene->AddPossessable(FoundActor->GetActorLabel(), FoundActor->GetClass());
	Sequence->BindPossessableObject(BindingGuid, *FoundActor, World);
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence_name"), SeqName);
	Result->SetStringField(TEXT("actor_name"), FoundActor->GetActorLabel());
	Result->SetStringField(TEXT("binding_guid"), BindingGuid.ToString());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// add_sequencer_track
// ============================================================================

bool FAddSequencerTrackAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	if (!GetRequiredString(Params, TEXT("sequence_name"), Name, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("track_type"), Name, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddSequencerTrackAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqName = Params->GetStringField(TEXT("sequence_name"));
	FString TrackType = Params->GetStringField(TEXT("track_type"));
	FString BindingGuid = GetOptionalString(Params, TEXT("binding_guid"));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqName, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Resolve track class from type name
	UClass* TrackClass = nullptr;
	if (TrackType == TEXT("Transform") || TrackType == TEXT("3DTransform"))
	{
		TrackClass = UMovieScene3DTransformTrack::StaticClass();
	}
	else if (TrackType == TEXT("Float"))
	{
		TrackClass = UMovieSceneFloatTrack::StaticClass();
	}
	else if (TrackType == TEXT("Bool"))
	{
		TrackClass = UMovieSceneBoolTrack::StaticClass();
	}
	else if (TrackType == TEXT("Visibility"))
	{
		TrackClass = UMovieSceneVisibilityTrack::StaticClass();
	}
	else
	{
		TArray<FString> Suggestions = {TEXT("Transform"), TEXT("Float"), TEXT("Bool"), TEXT("Visibility")};
		return CreateErrorResponseWithSuggestions(
			FString::Printf(TEXT("Unknown track type '%s'"), *TrackType),
			TEXT("unknown_track_type"),
			Suggestions);
	}

	UMovieSceneTrack* NewTrack = nullptr;

	if (!BindingGuid.IsEmpty())
	{
		// Add track to a specific binding
		FGuid Guid;
		if (FGuid::Parse(BindingGuid, Guid))
		{
			if (FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid))
			{
				NewTrack = MovieScene->AddTrack(TrackClass, Guid);
			}
		}
	}

	if (!NewTrack)
	{
		// Add as master track
		NewTrack = MovieScene->AddTrack(TrackClass);
	}

	if (!NewTrack)
	{
		return CreateErrorResponse(TEXT("Failed to add track"), TEXT("track_add_failed"));
	}

	// Add a default section
	NewTrack->AddSection(*NewTrack->CreateNewSection());
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence_name"), SeqName);
	Result->SetStringField(TEXT("track_type"), TrackType);
	Result->SetStringField(TEXT("track_class"), NewTrack->GetClass()->GetName());

	return CreateSuccessResponse(Result);
}

// ============================================================================
// set_sequencer_range
// ============================================================================

bool FSetSequencerRangeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Name;
	return GetRequiredString(Params, TEXT("sequence_name"), Name, OutError);
}

TSharedPtr<FJsonObject> FSetSequencerRangeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqName = Params->GetStringField(TEXT("sequence_name"));
	double StartSeconds = GetOptionalNumber(Params, TEXT("start_seconds"), 0.0);
	double EndSeconds = GetOptionalNumber(Params, TEXT("end_seconds"), 5.0);

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqName, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	FFrameRate FrameRate = MovieScene->GetTickResolution();
	FFrameNumber StartFrame = (StartSeconds * FrameRate).FloorToFrame();
	FFrameNumber EndFrame = (EndSeconds * FrameRate).FloorToFrame();

	MovieScene->SetPlaybackRange(StartFrame, (EndFrame - StartFrame).Value);
	Sequence->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence_name"), SeqName);
	Result->SetNumberField(TEXT("start_seconds"), StartSeconds);
	Result->SetNumberField(TEXT("end_seconds"), EndSeconds);

	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: add_sequencer_keyframe
// ============================================================================

bool FAddSequencerKeyframeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Seq, Bind, Track, Val;
	if (!GetRequiredString(Params, TEXT("sequence_path"), Seq, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("binding_name"), Bind, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("track_type"), Track, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("value"), Val, OutError)) return false;
	if (!Params->HasField(TEXT("frame")))
	{
		OutError = TEXT("Missing required parameter: frame");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FAddSequencerKeyframeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqPath = Params->GetStringField(TEXT("sequence_path"));
	FString BindingName = Params->GetStringField(TEXT("binding_name"));
	FString TrackType = Params->GetStringField(TEXT("track_type"));
	int32 FrameNum = static_cast<int32>(Params->GetNumberField(TEXT("frame")));
	FString Value = Params->GetStringField(TEXT("value"));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqPath, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Find binding by name
	const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
	const FMovieSceneBinding* FoundBinding = nullptr;
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		if (Binding.GetName() == BindingName)
		{
			FoundBinding = &Binding;
			break;
		}
	}

	if (!FoundBinding)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Binding '%s' not found in sequence"), *BindingName));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence"), SeqPath);
	Result->SetStringField(TEXT("binding"), BindingName);
	Result->SetStringField(TEXT("track_type"), TrackType);
	Result->SetNumberField(TEXT("frame"), FrameNum);
	Result->SetStringField(TEXT("value"), Value);
	Result->SetStringField(TEXT("status"), TEXT("keyframe_added"));

	Sequence->MarkPackageDirty();
	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: set_sequencer_keyframe
// ============================================================================

bool FSetSequencerKeyframeAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Seq, Bind, Track, Val;
	if (!GetRequiredString(Params, TEXT("sequence_path"), Seq, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("binding_name"), Bind, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("track_type"), Track, OutError)) return false;
	if (!Params->HasField(TEXT("frame")))
	{
		OutError = TEXT("Missing required parameter: frame");
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FSetSequencerKeyframeAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqPath = Params->GetStringField(TEXT("sequence_path"));
	FString BindingName = Params->GetStringField(TEXT("binding_name"));
	FString TrackType = Params->GetStringField(TEXT("track_type"));
	int32 FrameNum = static_cast<int32>(Params->GetNumberField(TEXT("frame")));
	bool bDelete = GetOptionalBool(Params, TEXT("delete"), false);

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqPath, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence"), SeqPath);
	Result->SetStringField(TEXT("binding"), BindingName);
	Result->SetNumberField(TEXT("frame"), FrameNum);
	Result->SetStringField(TEXT("status"), bDelete ? TEXT("keyframe_deleted") : TEXT("keyframe_modified"));

	Sequence->MarkPackageDirty();
	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: add_camera_cut_track
// ============================================================================

bool FAddCameraCutTrackAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Seq, Cam;
	if (!GetRequiredString(Params, TEXT("sequence_path"), Seq, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("camera_name"), Cam, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddCameraCutTrackAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqPath = Params->GetStringField(TEXT("sequence_path"));
	FString CameraName = Params->GetStringField(TEXT("camera_name"));
	int32 StartFrame = static_cast<int32>(GetOptionalNumber(Params, TEXT("start_frame"), 0.0));
	int32 EndFrame = static_cast<int32>(GetOptionalNumber(Params, TEXT("end_frame"), -1.0));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqPath, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Find camera actor in the level
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return CreateErrorResponse(TEXT("No editor world available"));

	AActor* CameraActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == CameraName || It->GetName() == CameraName)
		{
			CameraActor = *It;
			break;
		}
	}

	if (!CameraActor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Camera actor '%s' not found in level"), *CameraName));
	}

	// Add camera cut track
	UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
	if (!CameraCutTrack)
	{
		CameraCutTrack = MovieScene->AddTrack(UMovieSceneCameraCutTrack::StaticClass());
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence"), SeqPath);
	Result->SetStringField(TEXT("camera"), CameraName);
	Result->SetStringField(TEXT("status"), TEXT("camera_cut_track_added"));

	Sequence->MarkPackageDirty();
	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: add_sequencer_spawnable
// ============================================================================

bool FAddSequencerSpawnableAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Seq, BP;
	if (!GetRequiredString(Params, TEXT("sequence_path"), Seq, OutError)) return false;
	if (!GetRequiredString(Params, TEXT("blueprint"), BP, OutError)) return false;
	return true;
}

TSharedPtr<FJsonObject> FAddSequencerSpawnableAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqPath = Params->GetStringField(TEXT("sequence_path"));
	FString BlueprintPath = Params->GetStringField(TEXT("blueprint"));
	FString SpawnableName = GetOptionalString(Params, TEXT("name"));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqPath, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) return CreateErrorResponse(TEXT("MovieScene is null"));

	// Load the blueprint
	UObject* BPObj = StaticLoadObject(UObject::StaticClass(), nullptr, *BlueprintPath);
	if (!BPObj)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence"), SeqPath);
	Result->SetStringField(TEXT("blueprint"), BlueprintPath);
	Result->SetStringField(TEXT("name"), SpawnableName.IsEmpty() ? BPObj->GetName() : SpawnableName);
	Result->SetStringField(TEXT("status"), TEXT("spawnable_added"));

	Sequence->MarkPackageDirty();
	return CreateSuccessResponse(Result);
}

// ============================================================================
// v0.3.0: play_sequence_preview
// ============================================================================

bool FPlaySequencePreviewAction::Validate(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context, FString& OutError)
{
	FString Seq;
	return GetRequiredString(Params, TEXT("sequence_path"), Seq, OutError);
}

TSharedPtr<FJsonObject> FPlaySequencePreviewAction::ExecuteInternal(const TSharedPtr<FJsonObject>& Params, FMCPEditorContext& Context)
{
	FString SeqPath = Params->GetStringField(TEXT("sequence_path"));
	FString Action = GetOptionalString(Params, TEXT("action"), TEXT("play"));

	FString Error;
	ULevelSequence* Sequence = FindLevelSequence(SeqPath, Error);
	if (!Sequence) return CreateErrorResponse(Error);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sequence"), SeqPath);
	Result->SetStringField(TEXT("action"), Action);
	Result->SetStringField(TEXT("status"), FString::Printf(TEXT("preview_%s"), *Action));

	return CreateSuccessResponse(Result);
}

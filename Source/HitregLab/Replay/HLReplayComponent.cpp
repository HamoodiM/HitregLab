// Hitreg Lab: kill cam recorder and player
#include "Replay/HLReplayComponent.h"
#include "Replay/HLReplayGhost.h"
#include "Core/HLCharacter.h"
#include "Core/HLPlayerController.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

UHLReplayComponent::UHLReplayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // record after everything has moved this frame
}

void UHLReplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

void UHLReplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (!bPlaying)
	{
		RecordAccumulator += DeltaTime;
		if (RecordAccumulator >= 1.f / RecordHz)
		{
			RecordAccumulator = FMath::Fmod(RecordAccumulator, 1.f / RecordHz);
			Record();
		}
		WatchForDeath();
		// Start the kill cam a moment after death so the clip includes the fall
		if (PendingKillCamTime > 0.0 && GetWorld()->GetTimeSeconds() >= PendingKillCamTime)
		{
			PendingKillCamTime = -1.0;
			const float Ago = float(GetWorld()->GetTimeSeconds() - DeathTime);
			LastKillCam = BuildClip(KillCamBefore + Ago, PendingKillerId, PendingVictimId, Ago);
			Play(LastKillCam);
		}
	}
	else
	{
		HandleKeys();
		UpdatePlayback(DeltaTime);
	}
}

void UHLReplayComponent::Record()
{
	const float Now = float(GetWorld()->GetTimeSeconds());
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		const AHLCharacter* Char = *It;
		const int32 Id = Char->GetHLPlayerId();
		FHLReplayTrack& Track = Live.FindOrAdd(Id);
		Track.PlayerId = Id;
		Track.Color = Char->GetBodyColor();
		if (Char->GetPlayerState())
		{
			Track.Name = Char->GetPlayerState()->GetPlayerName();
		}
		FHLReplayKey Key;
		Key.Time = Now;
		Key.Pose = FHLQuantPose::Quantize(Char->GetPose());
		Key.bAlive = Char->IsAlive();
		Track.Keys.Add(Key);
	}
	// Trim the ring
	const float Oldest = Now - RecordSeconds;
	for (auto It = Live.CreateIterator(); It; ++It)
	{
		TArray<FHLReplayKey>& Keys = It.Value().Keys;
		int32 Drop = 0;
		while (Drop < Keys.Num() && Keys[Drop].Time < Oldest)
		{
			++Drop;
		}
		if (Drop > 0)
		{
			Keys.RemoveAt(0, Drop, EAllowShrinking::No);
		}
		if (Keys.Num() == 0)
		{
			It.RemoveCurrent();
		}
	}
	LiveShots.RemoveAll([Oldest](const FHLReplayShot& S) { return S.Time < Oldest; });
}

void UHLReplayComponent::RecordShot(int32 ShooterId, const FVector& Start, const FVector& End, bool bHit)
{
	if (bPlaying)
	{
		return;
	}
	FHLReplayShot Shot;
	Shot.Time = float(GetWorld()->GetTimeSeconds());
	Shot.ShooterId = ShooterId;
	Shot.Start = Start;
	Shot.End = End;
	Shot.bHit = bHit;
	LiveShots.Add(Shot);
}

void UHLReplayComponent::WatchForDeath()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	AHLCharacter* Pawn = Cast<AHLCharacter>(PC->GetPawn());
	if (Pawn != WatchedPawn.Get())
	{
		WatchedPawn = Pawn;
		bDeathHandled = false;
	}
	if (Pawn && !Pawn->IsAlive() && !bDeathHandled && Pawn->KilledBy)
	{
		bDeathHandled = true;
		DeathTime = GetWorld()->GetTimeSeconds();
		PendingKillerId = Pawn->KilledBy->GetPlayerId();
		PendingVictimId = Pawn->GetHLPlayerId();
		PendingKillCamTime = DeathTime + KillCamAfter;
	}
}

FHLReplayClip UHLReplayComponent::BuildClip(float Seconds, int32 KillerId, int32 VictimId, float KillAgo) const
{
	FHLReplayClip Clip;
	const float Now = float(GetWorld()->GetTimeSeconds());
	const float Start = Now - Seconds;
	Clip.Duration = Seconds;
	Clip.KillerId = KillerId;
	Clip.VictimId = VictimId;
	Clip.KillTime = Seconds - KillAgo;
	for (const auto& Pair : Live)
	{
		FHLReplayTrack Track;
		Track.PlayerId = Pair.Value.PlayerId;
		Track.Name = Pair.Value.Name;
		Track.Color = Pair.Value.Color;
		for (const FHLReplayKey& Key : Pair.Value.Keys)
		{
			if (Key.Time >= Start)
			{
				FHLReplayKey Rel = Key;
				Rel.Time = Key.Time - Start;
				Track.Keys.Add(Rel);
			}
		}
		if (Track.Keys.Num() > 0)
		{
			Clip.Tracks.Add(Track);
		}
	}
	for (const FHLReplayShot& Shot : LiveShots)
	{
		if (Shot.Time >= Start)
		{
			FHLReplayShot Rel = Shot;
			Rel.Time = Shot.Time - Start;
			Clip.Shots.Add(Rel);
		}
	}
	return Clip;
}

void UHLReplayComponent::Play(const FHLReplayClip& Clip)
{
	Stop();
	if (!Clip.IsValid())
	{
		return;
	}
	Current = Clip;
	bPlaying = true;
	bPaused = false;
	PlaybackTime = 0.f;
	Speed = 1.f;
	EndHold = 0.f;

	// Hide the live world's characters; ghosts take their place
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsHidden())
		{
			It->SetActorHiddenInGame(true);
			HiddenActors.Add(*It);
		}
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHLReplayGhost* KillerGhost = nullptr;
	AHLReplayGhost* VictimGhost = nullptr;
	for (const FHLReplayTrack& Track : Current.Tracks)
	{
		AHLReplayGhost* Ghost = GetWorld()->SpawnActor<AHLReplayGhost>(AHLReplayGhost::StaticClass(), FTransform::Identity, Params);
		Ghost->SetColor(Track.Color);
		Ghosts.Add(Ghost);
		GhostIds.Add(Track.PlayerId);
		if (Track.PlayerId == Current.KillerId)
		{
			KillerGhost = Ghost;
		}
		if (Track.PlayerId == Current.VictimId)
		{
			VictimGhost = Ghost;
		}
	}
	ApplyPlaybackTime(0.f);

	APlayerController* PC = Cast<APlayerController>(GetOwner());
	AHLReplayGhost* ViewGhost = KillerGhost ? KillerGhost : VictimGhost;
	if (PC && ViewGhost)
	{
		ViewGhost->SetFirstPerson(true);
		PC->SetViewTargetWithBlend(ViewGhost, 0.f);
	}
}

void UHLReplayComponent::Stop()
{
	if (!bPlaying)
	{
		return;
	}
	bPlaying = false;
	for (AHLReplayGhost* Ghost : Ghosts)
	{
		if (Ghost)
		{
			Ghost->Destroy();
		}
	}
	Ghosts.Reset();
	GhostIds.Reset();
	for (const TWeakObjectPtr<AActor>& A : HiddenActors)
	{
		if (A.IsValid())
		{
			A->SetActorHiddenInGame(false);
		}
	}
	HiddenActors.Reset();
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Pawn, 0.f);
		}
	}
}

void UHLReplayComponent::HandleKeys()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom))
	{
		bPaused = !bPaused;
	}
	if (PC->WasInputKeyJustPressed(EKeys::Left) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Left))
	{
		const float Prev = PlaybackTime;
		PlaybackTime = FMath::Max(0.f, PlaybackTime - 0.25f);
		ApplyPlaybackTime(Prev);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Right) || PC->WasInputKeyJustPressed(EKeys::Gamepad_DPad_Right))
	{
		const float Prev = PlaybackTime;
		PlaybackTime = FMath::Min(Current.Duration, PlaybackTime + 0.25f);
		ApplyPlaybackTime(Prev);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Down))
	{
		Speed = FMath::Max(0.125f, Speed * 0.5f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Up))
	{
		Speed = FMath::Min(2.f, Speed * 2.f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Enter) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Right))
	{
		Stop();
	}
}

void UHLReplayComponent::UpdatePlayback(float DeltaTime)
{
	if (!bPlaying)
	{
		return;
	}
	// A new life ends the kill cam
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const AHLCharacter* Pawn = PC ? Cast<AHLCharacter>(PC->GetPawn()) : nullptr;
	if (Pawn && Pawn->IsAlive())
	{
		Stop();
		return;
	}
	if (bPaused)
	{
		return;
	}
	// Slow motion around the killing shot
	const bool bNearKill = FMath::Abs(PlaybackTime - Current.KillTime) < 0.35f;
	const float Prev = PlaybackTime;
	PlaybackTime = FMath::Min(Current.Duration, PlaybackTime + DeltaTime * Speed * (bNearKill ? 0.3f : 1.f));
	ApplyPlaybackTime(Prev);
	if (PlaybackTime >= Current.Duration)
	{
		EndHold += DeltaTime;
		if (EndHold > 0.6f)
		{
			Stop();
		}
	}
}

void UHLReplayComponent::ApplyPlaybackTime(float PrevTime)
{
	for (int32 i = 0; i < Ghosts.Num(); ++i)
	{
		const FHLReplayTrack* Track = Current.FindTrack(GhostIds[i]);
		FHLPose Pose;
		bool bAlive = true;
		if (Ghosts[i] && Track && Track->Sample(PlaybackTime, Pose, bAlive))
		{
			Ghosts[i]->ApplyPose(Pose, bAlive || Track->PlayerId == Current.KillerId);
		}
	}
	// Tracers for shots that happened between the previous and current playback time
	const float Lo = FMath::Min(PrevTime, PlaybackTime);
	const float Hi = FMath::Max(PrevTime, PlaybackTime);
	for (const FHLReplayShot& Shot : Current.Shots)
	{
		if (Shot.Time > Lo && Shot.Time <= Hi)
		{
			const FColor Color = Shot.bHit ? FColor(255, 120, 60) : FColor(230, 230, 230);
			DrawDebugLine(GetWorld(), Shot.Start, Shot.End, Color, false, 0.12f, 0, 1.2f);
			DrawDebugPoint(GetWorld(), Shot.End, 8.f, Color, false, 0.3f);
		}
	}
}

FString UHLReplayComponent::GetKillerName() const
{
	const FHLReplayTrack* Track = Current.FindTrack(Current.KillerId);
	return Track ? Track->Name : TEXT("?");
}

FString UHLReplayComponent::SaveLastKillCam()
{
	if (!LastKillCam.IsValid())
	{
		return TEXT("No kill cam recorded yet");
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Replays");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Path = Dir / FString::Printf(TEXT("killcam_%s.hlreplay"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	if (!LastKillCam.SaveToFile(Path))
	{
		return TEXT("Saving the replay failed");
	}
	return FString::Printf(TEXT("Saved %s (%d bytes, %.0f B/s per player)"), *FPaths::GetCleanFilename(Path), LastKillCam.ToBytes().Num(), LastKillCam.BytesPerSecondPerPlayer());
}

FString UHLReplayComponent::LoadAndPlay(const FString& Path)
{
	FHLReplayClip Clip;
	FString Full = Path;
	if (FPaths::IsRelative(Full))
	{
		Full = FPaths::ProjectSavedDir() / TEXT("Replays") / Path;
	}
	if (!Clip.LoadFromFile(Full))
	{
		return FString::Printf(TEXT("Could not load %s"), *Full);
	}
	Play(Clip);
	return FString::Printf(TEXT("Playing %s"), *FPaths::GetCleanFilename(Full));
}

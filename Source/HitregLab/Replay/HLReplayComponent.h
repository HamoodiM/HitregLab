// Hitreg Lab: client-side recorder + kill cam player (lives on the local PlayerController).
//
// Every client records what it displayed (all characters at 30 Hz, plus every tracer) into an 8 s ring.
// When the local player dies, the last ~5 s become a clip played back from the killer's eyes.
// Recording on the client costs no bandwidth; the trade-off is that the clip shows the victim's view of
// the killer (already delayed by interpolation), not the killer's own screen. See README.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Replay/HLReplayData.h"
#include "HLReplayComponent.generated.h"

class AHLCharacter;
class AHLReplayGhost;

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLReplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLReplayComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Builds a clip from the recording: [Now - Seconds, Now]. */
	FHLReplayClip BuildClip(float Seconds, int32 KillerId, int32 VictimId, float KillAgo) const;

	void Play(const FHLReplayClip& Clip);
	void Stop();

	bool IsPlaying() const { return bPlaying; }
	float GetPlaybackTime() const { return PlaybackTime; }
	float GetSpeed() const { return Speed; }
	bool IsPaused() const { return bPaused; }
	const FHLReplayClip& GetClip() const { return Current; }
	FString GetKillerName() const;

	/** Saves the last kill cam clip under Saved/Replays and returns a status line. */
	FString SaveLastKillCam();
	FString LoadAndPlay(const FString& Path);

	/** Shot events reported by weapons on this machine. */
	void RecordShot(int32 ShooterId, const FVector& Start, const FVector& End, bool bHit);

	static constexpr float RecordHz = 30.f;
	static constexpr float RecordSeconds = 8.f;
	static constexpr float KillCamBefore = 4.f;
	static constexpr float KillCamAfter = 0.8f;

private:
	void Record();
	void WatchForDeath();
	void UpdatePlayback(float DeltaTime);
	void HandleKeys();
	void ApplyPlaybackTime(float PrevTime);
	void LogSanity() const;

	TMap<int32, FHLReplayTrack> Live; // absolute world times
	TArray<FHLReplayShot> LiveShots;
	float RecordAccumulator = 0.f;

	TWeakObjectPtr<AHLCharacter> WatchedPawn;
	bool bDeathHandled = false;
	double PendingKillCamTime = -1.0;
	int32 PendingKillerId = INDEX_NONE;
	int32 PendingVictimId = INDEX_NONE;
	double DeathTime = 0.0;

	FHLReplayClip Current;
	FHLReplayClip LastKillCam;
	bool bPlaying = false;
	bool bPaused = false;
	float PlaybackTime = 0.f;
	float Speed = 1.f;
	float EndHold = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AHLReplayGhost>> Ghosts;

	TArray<int32> GhostIds;
	TArray<TWeakObjectPtr<AActor>> HiddenActors;
};

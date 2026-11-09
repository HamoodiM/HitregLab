// Hitreg Lab: server-side lag compensation. Records every character's pose each server frame, and traces
// shots against where the shooter saw the targets (rewound) or where they are now (rewind off).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Net/HLPoseHistory.h"
#include "Net/HLHitboxMath.h"
#include "Net/HLRewindRules.h"
#include "HLLagCompensationSubsystem.generated.h"

class AHLCharacter;

struct FHLTraceHit
{
	TWeakObjectPtr<AHLCharacter> Target;
	FHLHitboxHit Hit;
	FVector End = FVector::ZeroVector;
	/** Pose the hitboxes were tested at, and the target's pose at the moment of the trace. */
	FHLPose TracedPose;
	FHLPose CurrentPose;
	float RewindSeconds = 0.f;

	// --- For the Shot Autopsy: the character the shot was "about" (the one hit, else the one under the
	// crosshair on the shooter's screen) and why the server agreed or not ---
	TWeakObjectPtr<AHLCharacter> Subject;
	/** Subject at the shooter's raw view time (no cap, no rule): what the shooter's screen showed. */
	FHLPose ViewPose;
	/** At the view time the ray hit a character before any wall / would have hit one through walls. */
	bool bViewHit = false;
	bool bViewHitThroughWorld = false;
	/** Rewind clamp applied to the subject, and whether it was out of the shooter's sight at its current pose. */
	EHLRewindClamp Clamp = EHLRewindClamp::None;
	bool bSubjectInCoverNow = false;
	/** Distance to the first wall along the ray (or MaxDist). */
	float WorldLimit = 0.f;
	float ViewAgeSeconds = 0.f;
};

UCLASS()
class HITREGLAB_API UHLLagCompensationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * Server: trace a shot. World geometry blocks first; then every other living character's hitboxes are
	 * tested at ViewTime (clamped to MaxRewind) if lag compensation is on, or at their current pose if not.
	 * Returns true if a character was hit. OutHit.End is where the shot stopped either way.
	 */
	bool TraceShot(const AHLCharacter* Shooter, const FVector& Origin, const FVector& Dir, float MaxDist, double ViewTime, FHLTraceHit& OutHit);

	/** Pose of a character at a past server time (for tests, the rewind debug view and replays). */
	bool GetPoseAt(const AHLCharacter* Character, double Time, FHLPose& OutPose) const;

	/** True if neither the head nor the torso centre of a character at Pose is visible from Eye (walls only). */
	bool IsInCoverFrom(const FVector& Eye, const AHLCharacter* Target, const FHLPose& Pose) const;

	/** Rolling average cost of TraceShot in microseconds (shown on the HUD and quoted in the README). */
	float GetAverageTraceMicros() const { return AvgTraceMicros; }
	int32 GetTracesMeasured() const { return TracesMeasured; }

	/** Seconds of history kept per character. */
	static constexpr double HistorySeconds = 1.0;

private:
	void OnPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
	void Record();

	TMap<TWeakObjectPtr<AHLCharacter>, FHLPoseHistory> Histories;
	FDelegateHandle PostTickHandle;
	float AvgTraceMicros = 0.f;
	int32 TracesMeasured = 0;
};

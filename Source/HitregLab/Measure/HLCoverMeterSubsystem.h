// Hitreg Lab: behind-cover deaths sweep, server side. Runs runner/shooter trials (see HLCoverComponent.h)
// across simulated RTT x rewind rule, collects the shot's server verdict (from the Shot Autopsy) and the
// runner's own-screen timings, and writes Saved/Measure/cover_<date>.csv.
//
// Per trial:
//   registered     the shot (on target on the shooter's screen) was confirmed by the server;
//   behind_ms      when the hit reached the runner minus when the runner's own screen lost sight of the
//                  shooter (> 0 means "I was already behind the wall when I died");
//   behind_cm      how far the runner had moved past that point on its own screen.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HLCoverMeterSubsystem.generated.h"

class AHLPlayerController;
struct FHLShotAutopsy;

struct FHLCoverConfig
{
	int32 RttMs = 0;
	float MaxRewindMs = 200.f;
	bool bCoverRule = false;
	float CoverBudgetMs = 100.f;
	FString Name() const;
};

struct FHLCoverRow
{
	FHLCoverConfig Config;
	int32 Trial = 0;
	float LeadCm = 0.f;
	bool bShot = false;
	bool bShooterSawTorso = false;
	FString Verdict;
	bool bRegistered = false;
	float RewindMs = 0.f;
	float ViewAgeMs = 0.f;
	bool bClamped = false;
	bool bInCoverAtServer = false;
	bool bRunnerReported = false;
	bool bLostSight = false;
	double LostSightTime = 0.0;
	float LostSightAlong = 0.f;
	bool bHitArrived = false;
	double HitTime = 0.0;
	float HitAlong = 0.f;

	bool IsValid() const { return bShot && bShooterSawTorso && bRunnerReported; }
	/** Registered, and the runner's own screen had already lost sight of the shooter when the hit arrived. */
	bool IsBehindCoverDeath() const { return bRegistered && bHitArrived && bLostSight && HitTime > LostSightTime; }
	float BehindMs() const { return (bHitArrived && bLostSight) ? float((HitTime - LostSightTime) * 1000.0) : 0.f; }
	float BehindCm() const { return (bHitArrived && bLostSight) ? HitAlong - LostSightAlong : 0.f; }
};

UCLASS()
class HITREGLAB_API UHLCoverMeterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHLCoverMeterSubsystem, STATGROUP_Tickables); }

	/**
	 * Starts a sweep. By default RTT {0,50,100,150,250} x {cap 200, cap 100, cap 300, cover rule}.
	 * Command line filters: -CoverRtt=N (one RTT), -CoverConfigs=base (cap 200 only).
	 */
	FString StartSweep(int32 TrialsPerConfig);
	void ReportShot(AHLPlayerController* From, int32 TrialId, float LeadCm, bool bTorsoVisible);
	void ReportRunner(AHLPlayerController* From, int32 TrialId, bool bLostSight, double LostSightTime, float LostSightAlong, bool bHit, double HitTime, float HitAlong);
	bool IsRunning() const { return Phase != EPhase::Idle; }

	static FString Summarize(const TArray<FHLCoverRow>& Rows);

private:
	enum class EPhase : uint8 { Idle, Settle, Trial, Gap };

	void OnShotJudged(const FHLShotAutopsy& Shot);
	void ApplyConfig();
	void BeginTrial();
	void EndTrial();
	void Finish();
	bool FindPlayers(AHLPlayerController*& OutRunner, AHLPlayerController*& OutShooter) const;

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;
	TArray<FHLCoverConfig> Configs;
	int32 ConfigIndex = 0;
	int32 TrialIndex = 0;
	int32 TrialsPer = 10;
	int32 TrialId = 0;
	bool bRowOpen = false;
	FHLCoverRow Current;
	TArray<FHLCoverRow> Rows;
	TWeakObjectPtr<AHLPlayerController> Runner;
	TWeakObjectPtr<AHLPlayerController> Shooter;
	FDelegateHandle JudgedHandle;
};

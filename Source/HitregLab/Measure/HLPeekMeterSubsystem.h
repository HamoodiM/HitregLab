// Hitreg Lab: peeker's-advantage sweep, server side. Runs trials across (RTT x tick rate x interpolation
// delay), collects each client's first-sight time and writes Saved/Measure/peek_<date>.csv.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HLPeekMeterSubsystem.generated.h"

class AHLPlayerController;

struct FHLPeekConfig
{
	int32 RttMs = 0;
	int32 TickRate = 60;
	int32 InterpTicks = 2;
};

struct FHLPeekRow
{
	FHLPeekConfig Config;
	int32 Trial = 0;
	double PeekerSees = -1.0;
	double HolderSees = -1.0;
	double AdvantageMs() const { return (HolderSees - PeekerSees) * 1000.0; }
	bool IsValid() const { return PeekerSees > 0.0 && HolderSees > 0.0; }
};

UCLASS()
class HITREGLAB_API UHLPeekMeterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHLPeekMeterSubsystem, STATGROUP_Tickables); }

	/** Starts a sweep. bFullGrid: RTT {0,50,100,150} x tick {20,30,60,128} at 2 ticks, plus 1-tick interp at 60 Hz. */
	FString StartSweep(int32 TrialsPerConfig, bool bFullGrid);
	void ReportSight(AHLPlayerController* From, int32 TrialId, double ServerTime);
	bool IsRunning() const { return Phase != EPhase::Idle; }

	static FString Summarize(const TArray<FHLPeekRow>& Rows);

private:
	enum class EPhase : uint8 { Idle, Settle, Trial, Gap };

	void BeginConfig();
	void BeginTrial();
	void EndTrial();
	void Finish();
	bool FindPlayers(AHLPlayerController*& OutPeeker, AHLPlayerController*& OutHolder) const;

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;
	TArray<FHLPeekConfig> Configs;
	int32 ConfigIndex = 0;
	int32 TrialIndex = 0;
	int32 TrialsPer = 10;
	int32 TrialId = 0;
	FHLPeekRow Current;
	TArray<FHLPeekRow> Rows;
	TWeakObjectPtr<AHLPlayerController> Peeker;
	TWeakObjectPtr<AHLPlayerController> Holder;
};

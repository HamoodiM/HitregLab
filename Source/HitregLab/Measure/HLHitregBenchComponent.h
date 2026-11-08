// Hitreg Lab: lag compensation benchmark (client side, on the PlayerController).
//
// The client aims perfectly at the torso of the nearest strafing dummy AS IT DRAWS IT and fires single taps.
// For each simulated RTT, with lag compensation on and off, it counts how many shots the server confirmed.
// Perfect aim on your own screen should mean ~100% registered with compensation, and much less without.
// Start with "HLHitregBench [shots]" in the console, or -HitregBench=N on a client's command line.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HLHitregBenchComponent.generated.h"

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLHitregBenchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLHitregBenchComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void Start(int32 ShotsPerConfig);
	bool IsRunning() const { return Phase != EPhase::Idle; }

private:
	enum class EPhase : uint8 { Idle, Settle, Shooting, Drain };

	struct FConfig
	{
		int32 RttMs = 0;
		bool bLagComp = true;
	};
	struct FRow
	{
		FConfig Config;
		int32 Shots = 0;
		int32 Predicted = 0;
		int32 Confirmed = 0;
		float MeanRewindMs = 0.f;
	};

	void BeginConfig();
	void Finish();
	class AHLCharacter* FindTarget() const;

	EPhase Phase = EPhase::Idle;
	TArray<FConfig> Configs;
	TArray<FRow> Rows;
	int32 ConfigIndex = 0;
	int32 ShotsPer = 40;
	int32 ShotsFired = 0;
	float PhaseTime = 0.f;
	float ShotTimer = 0.f;
	int32 BaseFired = 0;
	int32 BasePredicted = 0;
	int32 BaseConfirmed = 0;
	double RewindSum = 0.0;
	int32 RewindCount = 0;
	double LastResultTime = -1.0;
	bool bReleaseNextFrame = false;
};

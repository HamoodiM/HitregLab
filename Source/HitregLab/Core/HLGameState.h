// Hitreg Lab: replicated lab settings (tick rate, lag compensation, simulated RTT) and the kill feed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/HLTypes.h"
#include "HLGameState.generated.h"

UCLASS()
class HITREGLAB_API AHLGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AHLGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server network tick rate (Hz): how often snapshots go out. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	int32 ServerTickRate = 60;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	bool bLagCompensation = true;

	/** Shots older than this are traced at the oldest allowed time (anti "shot around the corner" cap). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	float MaxRewindMs = 200.f;

	/** Round trip added by packet simulation (half each way), for the HUD. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	int32 SimulatedRttMs = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	int32 SimulatedJitterMs = 0;

	/** Server-measured average lag compensation trace cost (microseconds). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lab")
	float AvgTraceMicros = 0.f;

	UPROPERTY(Replicated)
	TArray<FHLKillFeedEntry> KillFeed;

	void AddKill(const FHLKillFeedEntry& Entry);

	/** Server: apply the network settings (tick rate on the net driver, packet lag on server and clients). */
	void ServerApplyNetSettings(int32 TickRate, int32 RttMs, int32 JitterMs);

	/** Interpolation delay clients render remote players with: InterpTicks server ticks. */
	double GetInterpDelaySeconds(int32 InterpTicks) const { return double(InterpTicks) / double(FMath::Max(ServerTickRate, 1)); }
};

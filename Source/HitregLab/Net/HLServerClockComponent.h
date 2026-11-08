// Hitreg Lab: keeps a client's estimate of the server clock (lives on the PlayerController).
// The engine's GameState time is only refreshed every few seconds and has no RTT correction; lag compensation
// needs a clock good to a few milliseconds, so this pings the server and runs FHLClockSync.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/HLClockSync.h"
#include "HLServerClockComponent.generated.h"

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLServerClockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLServerClockComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Best estimate of the server's world time right now. On the server it is simply the world time. */
	double GetServerTime() const;

	bool IsSynced() const;
	double GetRttMs() const { return Sync.GetLastRtt() * 1000.0; }
	double GetBestRttMs() const { return Sync.GetBestRtt() * 1000.0; }
	/** Difference between this estimate and the engine's GameState server time, in ms (for the HUD comparison). */
	double GetEngineClockErrorMs() const;

	/** The synced server time from any world context (falls back to world time). */
	static double ServerNow(const UObject* WorldContext);

private:
	UFUNCTION(Server, Unreliable)
	void Server_Ping(double ClientTime);

	UFUNCTION(Client, Unreliable)
	void Client_Pong(double ClientTime, double ServerTime);

	FHLClockSync Sync;
	double SmoothedOffset = 0.0;
	bool bHasOffset = false;
	float PingTimer = 0.f;
	int32 PingsSent = 0;
};

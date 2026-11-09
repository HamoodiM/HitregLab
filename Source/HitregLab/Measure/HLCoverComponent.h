// Hitreg Lab: "I was behind the wall and still died", client side (lives on each PlayerController).
//
// Each trial, the server makes one player the RUNNER and one the SHOOTER on the peek map. The runner's client
// strafes from the open into cover behind the wall at full speed. The shooter's client aims perfectly at the
// runner AS IT DRAWS IT and fires one shot at the last moment the runner is still on its screen (a given lead,
// in cm, before its torso centre disappears behind the corner). The runner's client records, in synced
// server time, when ITS OWN screen lost sight of the shooter and when the hit reached it.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HLCoverComponent.generated.h"

UENUM()
enum class EHLCoverRole : uint8
{
	None,
	Runner,
	Shooter
};

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLCoverComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Client, Reliable)
	void Client_BeginTrial(int32 TrialId, EHLCoverRole InRole, FVector Start, FRotator Facing, FVector InPathStart, FVector InPathEnd, float StrafeDelay, float LeadCm);

	UFUNCTION(Client, Reliable)
	void Client_EndTrial();

	/** Runner: the server applied the trial's hit (arrives one-way latency later, like the death would). */
	UFUNCTION(Client, Reliable)
	void Client_Hit(int32 TrialId);

	UFUNCTION(Client, Reliable)
	void Client_Summary(const FString& Summary);

	UFUNCTION(Server, Reliable)
	void Server_ReportShot(int32 TrialId, float LeadCm, bool bTorsoVisible);

	UFUNCTION(Server, Reliable)
	void Server_ReportRunner(int32 TrialId, bool bInLostSight, double InLostSightTime, float InLostSightAlong, bool bInHit, double InHitTime, float InHitAlong);

	UFUNCTION(Server, Reliable)
	void Server_StartSweep(int32 TrialsPerConfig);

	EHLCoverRole GetRole() const { return Role; }
	const FString& GetLastSummary() const { return LastSummary; }

private:
	void TickRunner(class AHLCharacter* Me);
	void TickShooter(class AHLCharacter* Me);
	class AHLCharacter* FindOther(const class AHLCharacter* Me) const;
	bool TorsoVisible(const class AHLCharacter* Me, const FVector& TorsoCentre) const;

	EHLCoverRole Role = EHLCoverRole::None;
	int32 ActiveTrial = INDEX_NONE;
	FVector PathStart = FVector::ZeroVector;
	FVector PathEnd = FVector::ZeroVector;
	double StrafeAt = 0.0;
	float Lead = 0.f;

	// Runner
	bool bHadSight = false;
	bool bLostSight = false;
	double LostSightTime = 0.0;
	float LostSightAlong = 0.f;
	bool bHit = false;
	double HitTime = 0.0;
	float HitAlong = 0.f;

	// Shooter
	bool bEdgeKnown = false;
	float EdgeAlong = 0.f;
	bool bFired = false;

	FString LastSummary;
};

// Hitreg Lab: peeker's-advantage measurement, client side (lives on each PlayerController).
//
// For each trial the server puts one player behind a corner (the peeker) and one aiming at the corner (the
// holder). The peeker's client then strafes out at full speed, scripted so every run is identical. Each
// client reports, in synced server time, the moment ITS OWN screen first had line of sight to the other
// player's head. Peeker's advantage = when the holder saw the peeker - when the peeker saw the holder.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HLPeekComponent.generated.h"

UENUM()
enum class EHLPeekRole : uint8
{
	None,
	Peeker,
	Holder
};

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLPeekComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLPeekComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Client, Reliable)
	void Client_BeginTrial(int32 TrialId, EHLPeekRole InRole, FVector Start, FRotator Facing, FVector End, float StrafeDelay, int32 InterpTicks);

	UFUNCTION(Client, Reliable)
	void Client_EndTrial();

	UFUNCTION(Client, Reliable)
	void Client_Summary(const FString& Summary);

	UFUNCTION(Server, Reliable)
	void Server_ReportSight(int32 TrialId, double ServerTime);

	UFUNCTION(Server, Reliable)
	void Server_StartSweep(int32 TrialsPerConfig, bool bFullGrid);

	EHLPeekRole GetRole() const { return Role; }
	const FString& GetLastSummary() const { return LastSummary; }

private:
	EHLPeekRole Role = EHLPeekRole::None;
	int32 ActiveTrial = INDEX_NONE;
	FVector EndPoint = FVector::ZeroVector;
	double StrafeAt = 0.0;
	bool bReported = false;
	FString LastSummary;
};

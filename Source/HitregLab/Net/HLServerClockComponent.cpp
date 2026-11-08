// Hitreg Lab: client-side server clock
#include "Net/HLServerClockComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UHLServerClockComponent::UHLServerClockComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UHLServerClockComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || GetOwnerRole() == ROLE_Authority)
	{
		return;
	}

	// Ping fast at first so the estimate settles quickly, then twice a second
	PingTimer -= DeltaTime;
	if (PingTimer <= 0.f)
	{
		Server_Ping(GetWorld()->GetTimeSeconds());
		++PingsSent;
		PingTimer = PingsSent < 10 ? 0.1f : 0.5f;
	}

	// Slew the offset instead of jumping, so remote players don't stutter when a new sample arrives
	if (Sync.HasEstimate())
	{
		const double Target = Sync.GetOffset();
		if (!bHasOffset || FMath::Abs(Target - SmoothedOffset) > 0.05)
		{
			SmoothedOffset = Target;
			bHasOffset = true;
		}
		else
		{
			const double MaxStep = 0.001; // 1 ms per frame
			SmoothedOffset += FMath::Clamp(Target - SmoothedOffset, -MaxStep, MaxStep);
		}
	}
}

void UHLServerClockComponent::Server_Ping_Implementation(double ClientTime)
{
	Client_Pong(ClientTime, GetWorld()->GetTimeSeconds());
}

void UHLServerClockComponent::Client_Pong_Implementation(double ClientTime, double ServerTime)
{
	Sync.AddSample(ClientTime, ServerTime, GetWorld()->GetTimeSeconds());
}

bool UHLServerClockComponent::IsSynced() const
{
	return GetOwnerRole() == ROLE_Authority || (bHasOffset && Sync.NumSamples() >= 3);
}

double UHLServerClockComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (GetOwnerRole() == ROLE_Authority || !bHasOffset)
	{
		return World->GetTimeSeconds();
	}
	return World->GetTimeSeconds() + SmoothedOffset;
}

double UHLServerClockComponent::GetEngineClockErrorMs() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? (GS->GetServerWorldTimeSeconds() - GetServerTime()) * 1000.0 : 0.0;
}

double UHLServerClockComponent::ServerNow(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0;
	}
	if (World->GetNetMode() != NM_Client)
	{
		return World->GetTimeSeconds();
	}
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const UHLServerClockComponent* Clock = PC->FindComponentByClass<UHLServerClockComponent>())
		{
			return Clock->GetServerTime();
		}
	}
	return World->GetTimeSeconds();
}

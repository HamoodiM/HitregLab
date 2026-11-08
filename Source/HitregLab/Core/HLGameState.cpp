// Hitreg Lab: game state
#include "Core/HLGameState.h"
#include "Core/HLCharacter.h"
#include "Net/HLLagCompensationSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AHLGameState::AHLGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
}

void AHLGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHLGameState, ServerTickRate);
	DOREPLIFETIME(AHLGameState, bLagCompensation);
	DOREPLIFETIME(AHLGameState, MaxRewindMs);
	DOREPLIFETIME(AHLGameState, SimulatedRttMs);
	DOREPLIFETIME(AHLGameState, SimulatedJitterMs);
	DOREPLIFETIME(AHLGameState, AvgTraceMicros);
	DOREPLIFETIME(AHLGameState, KillFeed);
}

void AHLGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		if (const UHLLagCompensationSubsystem* LagComp = GetWorld()->GetSubsystem<UHLLagCompensationSubsystem>())
		{
			AvgTraceMicros = LagComp->GetAverageTraceMicros();
		}
	}
}

void AHLGameState::AddKill(const FHLKillFeedEntry& Entry)
{
	KillFeed.Add(Entry);
	if (KillFeed.Num() > 5)
	{
		KillFeed.RemoveAt(0);
	}
}

void AHLGameState::ServerApplyNetSettings(int32 TickRate, int32 RttMs, int32 JitterMs)
{
	if (!HasAuthority())
	{
		return;
	}
	ServerTickRate = FMath::Clamp(TickRate, 10, 128);
	SimulatedRttMs = FMath::Max(0, RttMs);
	SimulatedJitterMs = FMath::Max(0, JitterMs);

	UWorld* World = GetWorld();
	if (UNetDriver* Driver = World->GetNetDriver())
	{
		Driver->SetNetServerMaxTickRate(ServerTickRate);
#if DO_ENABLE_NET_TEST
		// Half the round trip on the server's outgoing packets; clients add the other half on theirs
		FPacketSimulationSettings Sim;
		Sim.PktLag = SimulatedRttMs / 2;
		Sim.PktLagVariance = SimulatedJitterMs;
		Driver->SetPacketSimulationSettings(Sim);
#endif
	}
	// Snapshots and movement go out once per server tick
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		It->SetNetUpdateFrequency(float(ServerTickRate));
		It->SetMinNetUpdateFrequency(float(ServerTickRate));
	}
}

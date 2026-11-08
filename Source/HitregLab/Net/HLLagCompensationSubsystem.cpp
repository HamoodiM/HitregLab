// Hitreg Lab: server-side lag compensation
#include "Net/HLLagCompensationSubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

bool UHLLagCompensationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UHLLagCompensationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UHLLagCompensationSubsystem::OnPostActorTick);
}

void UHLLagCompensationSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	Histories.Reset();
	Super::Deinitialize();
}

void UHLLagCompensationSubsystem::OnPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
	if (InWorld == GetWorld() && InWorld->GetNetMode() != NM_Client)
	{
		Record();
	}
}

void UHLLagCompensationSubsystem::Record()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	// Enough slots for HistorySeconds at a high frame rate
	const int32 Capacity = 256;
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		AHLCharacter* Char = *It;
		FHLPoseHistory* History = Histories.Find(Char);
		if (!History)
		{
			History = &Histories.Add(Char, FHLPoseHistory(Capacity));
		}
		History->Push(Now, Char->GetPose());
	}
	// Forget characters that were destroyed
	for (auto It = Histories.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UHLLagCompensationSubsystem::GetPoseAt(const AHLCharacter* Character, double Time, FHLPose& OutPose) const
{
	const FHLPoseHistory* History = Histories.Find(const_cast<AHLCharacter*>(Character));
	if (!History || History->Num() == 0)
	{
		return false;
	}
	History->Sample(Time, OutPose);
	return true;
}

bool UHLLagCompensationSubsystem::TraceShot(const AHLCharacter* Shooter, const FVector& Origin, const FVector& Dir, float MaxDist, double ViewTime, FHLTraceHit& OutHit)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();
	UWorld* World = GetWorld();
	const AHLGameState* GS = World->GetGameState<AHLGameState>();
	const bool bRewind = GS ? GS->bLagCompensation : true;
	const double MaxRewind = GS ? GS->MaxRewindMs / 1000.0 : 0.2;
	const double Now = World->GetTimeSeconds();
	const double TraceTime = bRewind ? FMath::Clamp(ViewTime, Now - MaxRewind, Now) : Now;

	// 1) World geometry: characters are tested analytically below, so ignore all of them here
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLShotWorld), true);
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	float Limit = MaxDist;
	FHitResult WorldHit;
	if (World->LineTraceSingleByChannel(WorldHit, Origin, Origin + Dir * MaxDist, ECC_Visibility, Params))
	{
		Limit = WorldHit.Distance;
	}
	OutHit.End = Origin + Dir * Limit;

	// 2) Characters, at the rewound (or current) pose
	bool bHitAny = false;
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		AHLCharacter* Target = *It;
		if (Target == Shooter || !Target->IsAlive())
		{
			continue;
		}
		FHLPose Pose = Target->GetPose();
		if (bRewind && TraceTime < Now)
		{
			GetPoseAt(Target, TraceTime, Pose);
		}
		FHLHitboxHit Hit;
		if (HLHitboxMath::RayHitboxes(Origin, Dir, Limit, Target->Hitboxes, Pose, Hit) && (!bHitAny || Hit.Distance < OutHit.Hit.Distance))
		{
			bHitAny = true;
			OutHit.Target = Target;
			OutHit.Hit = Hit;
			OutHit.TracedPose = Pose;
			OutHit.CurrentPose = Target->GetPose();
			OutHit.End = Origin + Dir * Hit.Distance;
		}
	}
	OutHit.RewindSeconds = float(Now - TraceTime);

	const float Micros = float(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0);
	AvgTraceMicros = TracesMeasured == 0 ? Micros : FMath::Lerp(AvgTraceMicros, Micros, 0.05f);
	++TracesMeasured;
	return bHitAny;
}

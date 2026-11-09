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

bool UHLLagCompensationSubsystem::IsInCoverFrom(const FVector& Eye, const AHLCharacter* Target, const FHLPose& Pose) const
{
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLCoverTest), true);
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (const FHLHitbox& Box : Target->Hitboxes)
	{
		if (Box.Zone == EHLHitZone::Legs)
		{
			continue; // head or torso visible = not in cover
		}
		if (!World->LineTraceTestByChannel(Eye, HLHitboxMath::HitboxCenter(Box, Pose), ECC_Visibility, Params))
		{
			return false;
		}
	}
	return true;
}

bool UHLLagCompensationSubsystem::TraceShot(const AHLCharacter* Shooter, const FVector& Origin, const FVector& Dir, float MaxDist, double ViewTime, FHLTraceHit& OutHit)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();
	UWorld* World = GetWorld();
	const AHLGameState* GS = World->GetGameState<AHLGameState>();
	const double Now = World->GetTimeSeconds();
	FHLRewindInput Rule;
	Rule.Now = Now;
	Rule.ViewTime = ViewTime;
	Rule.bLagCompensation = GS ? GS->bLagCompensation : true;
	Rule.MaxRewindSeconds = GS ? GS->MaxRewindMs / 1000.0 : 0.2;
	Rule.bCoverRule = GS ? GS->bCoverRule : false;
	Rule.CoverBudgetSeconds = GS ? GS->CoverRuleBudgetMs / 1000.0 : 0.1;
	const FHLRewindDecision Default = HLRewindRules::Resolve(Rule);
	OutHit.ViewAgeSeconds = float(Now - ViewTime);

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
	OutHit.WorldLimit = Limit;

	// 2) Characters, at the rewound (or current) pose. The rewind is decided per target: the cover rule only
	// limits it against a target that is already in cover from the shooter right now.
	bool bHitAny = false;
	double HitTraceTime = Default.TraceTime;
	float BestView = TNumericLimits<float>::Max();
	float BestThrough = TNumericLimits<float>::Max();
	TWeakObjectPtr<AHLCharacter> ViewTarget;
	FHLPose ViewTargetPose;
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		AHLCharacter* Target = *It;
		if (Target == Shooter || !Target->IsAlive())
		{
			continue;
		}
		const FHLPose Current = Target->GetPose();
		FHLRewindInput TargetRule = Rule;
		bool bInCover = false;
		if (HLRewindRules::NeedsCoverTest(TargetRule))
		{
			bInCover = IsInCoverFrom(Origin, Target, Current);
			TargetRule.bTargetInCoverNow = bInCover;
		}
		const FHLRewindDecision Decision = HLRewindRules::Resolve(TargetRule);
		FHLPose Pose = Current;
		if (Decision.TraceTime < Now)
		{
			GetPoseAt(Target, Decision.TraceTime, Pose);
		}
		FHLHitboxHit Hit;
		if (HLHitboxMath::RayHitboxes(Origin, Dir, Limit, Target->Hitboxes, Pose, Hit) && (!bHitAny || Hit.Distance < OutHit.Hit.Distance))
		{
			bHitAny = true;
			OutHit.Target = Target;
			OutHit.Hit = Hit;
			OutHit.TracedPose = Pose;
			OutHit.CurrentPose = Current;
			OutHit.End = Origin + Dir * Hit.Distance;
			OutHit.Clamp = Decision.Clamp;
			OutHit.bSubjectInCoverNow = bInCover;
			HitTraceTime = Decision.TraceTime;
		}

		// What the shooter's own screen showed: the target at the raw view time (no cap, no rule)
		FHLPose ViewPose = Current;
		if (ViewTime < Now)
		{
			GetPoseAt(Target, ViewTime, ViewPose);
		}
		FHLHitboxHit ViewHit;
		if (HLHitboxMath::RayHitboxes(Origin, Dir, MaxDist, Target->Hitboxes, ViewPose, ViewHit))
		{
			if (ViewHit.Distance <= Limit && ViewHit.Distance < BestView)
			{
				BestView = ViewHit.Distance;
				ViewTarget = Target;
				ViewTargetPose = ViewPose;
				OutHit.bViewHit = true;
			}
			else if (ViewHit.Distance > Limit && ViewHit.Distance < BestThrough)
			{
				BestThrough = ViewHit.Distance;
				OutHit.bViewHitThroughWorld = true;
				if (!OutHit.bViewHit)
				{
					ViewTarget = Target;
					ViewTargetPose = ViewPose;
				}
			}
		}
	}
	OutHit.RewindSeconds = float(Now - HitTraceTime);

	const float Micros = float(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles) * 1000.0);
	AvgTraceMicros = TracesMeasured == 0 ? Micros : FMath::Lerp(AvgTraceMicros, Micros, 0.05f);
	++TracesMeasured;

	// Autopsy bookkeeping (after the timing, so the HUD cost stays the cost of the trace itself)
	AHLCharacter* Subject = bHitAny ? OutHit.Target.Get() : ViewTarget.Get();
	OutHit.Subject = Subject;
	if (Subject)
	{
		OutHit.CurrentPose = Subject->GetPose();
		OutHit.ViewPose = Subject == ViewTarget.Get() ? ViewTargetPose : OutHit.CurrentPose;
		if (!bHitAny)
		{
			// A miss: redo the subject's rewind decision so the record says where it was tested and why
			FHLRewindInput SubjectRule = Rule;
			SubjectRule.bTargetInCoverNow = IsInCoverFrom(Origin, Subject, OutHit.CurrentPose);
			const FHLRewindDecision D = HLRewindRules::Resolve(SubjectRule);
			OutHit.Clamp = D.Clamp;
			OutHit.bSubjectInCoverNow = SubjectRule.bTargetInCoverNow;
			OutHit.TracedPose = OutHit.CurrentPose;
			if (D.TraceTime < Now)
			{
				GetPoseAt(Subject, D.TraceTime, OutHit.TracedPose);
			}
			OutHit.RewindSeconds = float(Now - D.TraceTime);
		}
		else if (bHitAny)
		{
			if (Subject == ViewTarget.Get())
			{
				OutHit.ViewPose = ViewTargetPose;
			}
			else
			{
				GetPoseAt(Subject, ViewTime, OutHit.ViewPose);
			}
			if (!HLRewindRules::NeedsCoverTest(Rule))
			{
				OutHit.bSubjectInCoverNow = IsInCoverFrom(Origin, Subject, OutHit.CurrentPose);
			}
		}
	}
	return bHitAny;
}

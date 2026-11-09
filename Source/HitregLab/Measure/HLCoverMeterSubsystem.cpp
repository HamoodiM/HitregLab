// Hitreg Lab: behind-cover deaths sweep
#include "Measure/HLCoverMeterSubsystem.h"
#include "Measure/HLCoverComponent.h"
#include "Autopsy/HLShotAutopsySubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameMode.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Level/HLLevelBuilder.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "HitregLab.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
	constexpr float CoverSettleSeconds = 2.0f;
	constexpr float CoverTrialSeconds = 3.0f;
	constexpr float CoverGapSeconds = 0.8f;  // long enough for the runner's report at 250 ms RTT
	constexpr float CoverStrafeDelay = 0.7f;
}

FString FHLCoverConfig::Name() const
{
	return bCoverRule ? FString::Printf(TEXT("cover_rule_%.0f"), CoverBudgetMs) : FString::Printf(TEXT("cap_%.0f"), MaxRewindMs);
}

bool UHLCoverMeterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UHLCoverMeterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UHLShotAutopsySubsystem* Autopsy = Collection.InitializeDependency<UHLShotAutopsySubsystem>())
	{
		JudgedHandle = Autopsy->OnShotJudged.AddUObject(this, &UHLCoverMeterSubsystem::OnShotJudged);
	}
}

void UHLCoverMeterSubsystem::Deinitialize()
{
	if (UHLShotAutopsySubsystem* Autopsy = GetWorld() ? GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>() : nullptr)
	{
		Autopsy->OnShotJudged.Remove(JudgedHandle);
	}
	Super::Deinitialize();
}

bool UHLCoverMeterSubsystem::FindPlayers(AHLPlayerController*& OutRunner, AHLPlayerController*& OutShooter) const
{
	OutRunner = nullptr;
	OutShooter = nullptr;
	for (TActorIterator<AHLPlayerController> It(GetWorld()); It; ++It)
	{
		if (!It->GetPawn())
		{
			continue;
		}
		if (!OutRunner)
		{
			OutRunner = *It;
		}
		else if (!OutShooter)
		{
			OutShooter = *It;
		}
	}
	return OutRunner && OutShooter;
}

FString UHLCoverMeterSubsystem::StartSweep(int32 TrialsPerConfig)
{
	AHLPlayerController* R = nullptr;
	AHLPlayerController* S = nullptr;
	if (!FindPlayers(R, S))
	{
		return TEXT("Cover sweep needs two connected players on the peek map");
	}
	Runner = R;
	Shooter = S;
	int32 OnlyRtt = -1;
	FParse::Value(FCommandLine::Get(), TEXT("CoverRtt="), OnlyRtt);
	FString Which;
	FParse::Value(FCommandLine::Get(), TEXT("CoverConfigs="), Which);
	const bool bBaseOnly = Which.Equals(TEXT("base"), ESearchCase::IgnoreCase);
	Configs.Reset();
	for (const int32 Rtt : { 0, 50, 100, 150, 250 })
	{
		if (OnlyRtt >= 0 && Rtt != OnlyRtt)
		{
			continue;
		}
		Configs.Add({ Rtt, 200.f, false, 100.f });
		if (!bBaseOnly)
		{
			Configs.Add({ Rtt, 100.f, false, 100.f });
			Configs.Add({ Rtt, 300.f, false, 100.f });
			Configs.Add({ Rtt, 200.f, true, 100.f });
		}
	}
	if (Configs.Num() == 0)
	{
		return TEXT("Cover sweep: no configs (check -CoverRtt)");
	}
	TrialsPer = FMath::Max(1, TrialsPerConfig);
	ConfigIndex = 0;
	Rows.Reset();
	bRowOpen = false;
	ApplyConfig();
	return FString::Printf(TEXT("Cover sweep: %d configs x %d trials"), Configs.Num(), TrialsPer);
}

void UHLCoverMeterSubsystem::ApplyConfig()
{
	const FHLCoverConfig& C = Configs[ConfigIndex];
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->ServerApplyNetSettings(60, C.RttMs, 0);
		GS->bLagCompensation = true;
		GS->MaxRewindMs = C.MaxRewindMs;
		GS->bCoverRule = C.bCoverRule;
		GS->CoverRuleBudgetMs = C.CoverBudgetMs;
	}
	TrialIndex = 0;
	Phase = EPhase::Settle;
	PhaseTime = 0.f;
	UE_LOG(LogHitreg, Display, TEXT("Cover config %d/%d: RTT %d ms, %s"), ConfigIndex + 1, Configs.Num(), C.RttMs, *C.Name());
}

void UHLCoverMeterSubsystem::BeginTrial()
{
	if (bRowOpen)
	{
		Rows.Add(Current);
		bRowOpen = false;
	}
	AHLPlayerController* R = Runner.Get();
	AHLPlayerController* S = Shooter.Get();
	const AHLGameMode* GM = GetWorld()->GetAuthGameMode<AHLGameMode>();
	const AHLLevelBuilder* Builder = GM ? GM->GetBuilder() : nullptr;
	AHLCharacter* RunnerChar = R ? Cast<AHLCharacter>(R->GetPawn()) : nullptr;
	if (!R || !S || !RunnerChar || !S->GetPawn() || !Builder)
	{
		Finish();
		return;
	}
	// Runner: from the open (east of the corner) to well behind the wall; shooter holds the corner
	const FHLPeekSetup& P = Builder->GetPeekSetup();
	const FVector PathStart = P.PeekerEnd + FVector(150.f, 0.f, 0.f);
	const FVector PathEnd = P.PeekerStart - FVector(50.f, 0.f, 0.f);
	const FRotator RunnerFacing = (P.HolderSpot - PathStart).Rotation();
	const FRotator ShooterFacing = (P.Corner - P.HolderSpot).Rotation();
	RunnerChar->TeleportTo(PathStart, RunnerFacing);
	S->GetPawn()->TeleportTo(P.HolderSpot, ShooterFacing);
	R->SetControlRotation(RunnerFacing);
	S->SetControlRotation(ShooterFacing);
	RunnerChar->Health = RunnerChar->MaxHealth; // every trial's hit counts as "the" killing shot; nobody dies

	++TrialId;
	Current = FHLCoverRow();
	Current.Config = Configs[ConfigIndex];
	Current.Trial = TrialIndex;
	// Stratified leads: 10..90 cm before the torso centre leaves the shooter's screen
	Current.LeadCm = TrialsPer > 1 ? 10.f + 80.f * float(TrialIndex) / float(TrialsPer - 1) : 50.f;
	bRowOpen = true;
	if (UHLCoverComponent* RC = R->FindComponentByClass<UHLCoverComponent>())
	{
		RC->Client_BeginTrial(TrialId, EHLCoverRole::Runner, PathStart, RunnerFacing, PathStart, PathEnd, CoverStrafeDelay, 0.f);
	}
	if (UHLCoverComponent* SC = S->FindComponentByClass<UHLCoverComponent>())
	{
		SC->Client_BeginTrial(TrialId, EHLCoverRole::Shooter, P.HolderSpot, ShooterFacing, PathStart, PathEnd, CoverStrafeDelay, Current.LeadCm);
	}
	Phase = EPhase::Trial;
	PhaseTime = 0.f;
}

void UHLCoverMeterSubsystem::OnShotJudged(const FHLShotAutopsy& Shot)
{
	if (Phase != EPhase::Trial || !bRowOpen || Current.bShot)
	{
		return;
	}
	const AHLPlayerController* S = Shooter.Get();
	if (!S || !S->PlayerState || Shot.ShooterId != S->PlayerState->GetPlayerId())
	{
		return;
	}
	Current.bShot = true;
	Current.Verdict = HLShotAutopsy::VerdictKey(Shot.Verdict);
	Current.bRegistered = Shot.IsRegistered();
	Current.RewindMs = Shot.RewindMs;
	Current.ViewAgeMs = Shot.ViewAgeMs;
	Current.bClamped = Shot.Clamp != EHLRewindClamp::None;
	Current.bInCoverAtServer = Shot.HasFlag(FHLShotAutopsy::FlagTargetInCover);
	if (Current.bRegistered)
	{
		if (UHLCoverComponent* RC = Runner.IsValid() ? Runner->FindComponentByClass<UHLCoverComponent>() : nullptr)
		{
			RC->Client_Hit(TrialId);
		}
	}
}

void UHLCoverMeterSubsystem::ReportShot(AHLPlayerController* From, int32 InTrialId, float LeadCm, bool bTorsoVisible)
{
	if (bRowOpen && InTrialId == TrialId && From == Shooter.Get())
	{
		Current.LeadCm = LeadCm;
		Current.bShooterSawTorso = bTorsoVisible;
	}
}

void UHLCoverMeterSubsystem::ReportRunner(AHLPlayerController* From, int32 InTrialId, bool bLostSight, double LostSightTime, float LostSightAlong, bool bHit, double HitTime, float HitAlong)
{
	if (bRowOpen && InTrialId == TrialId && From == Runner.Get())
	{
		Current.bRunnerReported = true;
		Current.bLostSight = bLostSight;
		Current.LostSightTime = LostSightTime;
		Current.LostSightAlong = LostSightAlong;
		Current.bHitArrived = bHit;
		Current.HitTime = HitTime;
		Current.HitAlong = HitAlong;
	}
}

void UHLCoverMeterSubsystem::EndTrial()
{
	for (AHLPlayerController* PC : { Runner.Get(), Shooter.Get() })
	{
		if (UHLCoverComponent* Comp = PC ? PC->FindComponentByClass<UHLCoverComponent>() : nullptr)
		{
			Comp->Client_EndTrial();
		}
	}
	++TrialIndex;
	Phase = EPhase::Gap;
	PhaseTime = 0.f;
}

void UHLCoverMeterSubsystem::Tick(float DeltaTime)
{
	if (Phase == EPhase::Idle)
	{
		return;
	}
	PhaseTime += DeltaTime;
	switch (Phase)
	{
	case EPhase::Settle:
		if (PhaseTime >= CoverSettleSeconds)
		{
			BeginTrial();
		}
		break;
	case EPhase::Trial:
		if (PhaseTime >= CoverTrialSeconds)
		{
			EndTrial();
		}
		break;
	case EPhase::Gap:
		if (PhaseTime >= CoverGapSeconds)
		{
			if (TrialIndex < TrialsPer)
			{
				BeginTrial();
			}
			else if (ConfigIndex + 1 < Configs.Num())
			{
				if (bRowOpen)
				{
					Rows.Add(Current);
					bRowOpen = false;
				}
				++ConfigIndex;
				ApplyConfig();
			}
			else
			{
				Finish();
			}
		}
		break;
	default:
		break;
	}
}

FString UHLCoverMeterSubsystem::Summarize(const TArray<FHLCoverRow>& InRows)
{
	struct FAgg { int32 Valid = 0; int32 Registered = 0; int32 Behind = 0; TArray<float> BehindMs; };
	TMap<FString, FAgg> ByConfig;
	TArray<FString> Order;
	for (const FHLCoverRow& R : InRows)
	{
		const FString Key = FString::Printf(TEXT("%dms/%s"), R.Config.RttMs, *R.Config.Name());
		if (!ByConfig.Contains(Key))
		{
			Order.Add(Key);
		}
		FAgg& A = ByConfig.FindOrAdd(Key);
		if (!R.IsValid())
		{
			continue;
		}
		++A.Valid;
		A.Registered += R.bRegistered ? 1 : 0;
		A.Behind += R.IsBehindCoverDeath() ? 1 : 0;
		if (R.bRegistered && R.bHitArrived && R.bLostSight)
		{
			A.BehindMs.Add(R.BehindMs());
		}
	}
	FString Out;
	for (const FString& Key : Order)
	{
		FAgg& A = ByConfig[Key];
		A.BehindMs.Sort();
		const float Median = A.BehindMs.Num() > 0 ? A.BehindMs[A.BehindMs.Num() / 2] : 0.f;
		Out += FString::Printf(TEXT("%s: %d/%d registered, %d behind cover (median %+.0f ms)   "), *Key, A.Registered, A.Valid, A.Behind, Median);
	}
	return Out.IsEmpty() ? TEXT("No cover trials") : Out;
}

void UHLCoverMeterSubsystem::Finish()
{
	if (bRowOpen)
	{
		Rows.Add(Current);
		bRowOpen = false;
	}
	Phase = EPhase::Idle;
	FString Csv = TEXT("rtt_ms,config,max_rewind_ms,cover_rule,cover_budget_ms,trial,lead_cm,valid,verdict,registered,rewind_ms,view_age_ms,clamped,")
		TEXT("in_cover_at_server,runner_lost_sight,hit_arrived,behind_ms,behind_cm,behind_cover_death\n");
	for (const FHLCoverRow& R : Rows)
	{
		Csv += FString::Printf(TEXT("%d,%s,%.0f,%d,%.0f,%d,%.1f,%d,%s,%d,%.1f,%.1f,%d,%d,%d,%d,%.1f,%.1f,%d\n"),
			R.Config.RttMs, *R.Config.Name(), R.Config.MaxRewindMs, R.Config.bCoverRule ? 1 : 0, R.Config.CoverBudgetMs, R.Trial, R.LeadCm,
			R.IsValid() ? 1 : 0, R.Verdict.IsEmpty() ? TEXT("no_shot") : *R.Verdict, R.bRegistered ? 1 : 0, R.RewindMs, R.ViewAgeMs, R.bClamped ? 1 : 0,
			R.bInCoverAtServer ? 1 : 0, R.bLostSight ? 1 : 0, R.bHitArrived ? 1 : 0, R.BehindMs(), R.BehindCm(), R.IsBehindCoverDeath() ? 1 : 0);
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / FString::Printf(TEXT("cover_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	FFileHelper::SaveStringToFile(Csv, *File);
	const FString Summary = Summarize(Rows) + TEXT("-> ") + FPaths::GetCleanFilename(File);
	UE_LOG(LogHitreg, Display, TEXT("Cover sweep done: %s"), *Summary);
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->MaxRewindMs = 200.f;
		GS->bCoverRule = false;
	}
	for (AHLPlayerController* PC : { Runner.Get(), Shooter.Get() })
	{
		if (UHLCoverComponent* Comp = PC ? PC->FindComponentByClass<UHLCoverComponent>() : nullptr)
		{
			Comp->Client_Summary(TEXT("done"));
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("QuitAfterSweep")))
	{
		// Let the summary RPCs and the autopsy log flush first
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([]() { FPlatformMisc::RequestExit(false); }), 1.5f, false);
	}
}

// Hitreg Lab: lag compensation benchmark
#include "Measure/HLHitregBenchComponent.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Weapons/HLWeaponComponent.h"
#include "EngineUtils.h"
#include "HitregLab.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

namespace
{
	constexpr float BenchSettleSeconds = 2.5f;
	constexpr float BenchShotGap = 0.35f;   // single taps, so bloom has recovered
	constexpr float BenchDrainSeconds = 1.0f; // let the last results come back
}

UHLHitregBenchComponent::UHLHitregBenchComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics; // aim before the weapon fires this frame
}

void UHLHitregBenchComponent::BeginPlay()
{
	Super::BeginPlay();
	int32 Shots = 0;
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC && PC->IsLocalController() && FParse::Value(FCommandLine::Get(), TEXT("HitregBench="), Shots) && Shots > 0)
	{
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Shots]() { Start(Shots); }), 8.f, false);
	}
}

void UHLHitregBenchComponent::Start(int32 ShotsPerConfig)
{
	Configs.Reset();
	// -BenchRtt=N runs just that RTT (on, then off): used to record the comparison clip
	int32 OnlyRtt = -1;
	FParse::Value(FCommandLine::Get(), TEXT("BenchRtt="), OnlyRtt);
	for (const int32 Rtt : { 0, 50, 100, 150, 250 })
	{
		if (OnlyRtt >= 0 && Rtt != OnlyRtt)
		{
			continue;
		}
		Configs.Add({ Rtt, true });
		Configs.Add({ Rtt, false });
	}
	ShotsPer = FMath::Max(5, ShotsPerConfig);
	ConfigIndex = 0;
	Rows.Reset();
	BeginConfig();
}

void UHLHitregBenchComponent::BeginConfig()
{
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	const FConfig& C = Configs[ConfigIndex];
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	PC->Server_SetNet(GS ? GS->ServerTickRate : 60, C.RttMs, 0);
	PC->Server_SetLagComp(C.bLagComp);
	Phase = EPhase::Settle;
	PhaseTime = 0.f;
	UE_LOG(LogHitreg, Display, TEXT("Hitreg bench %d/%d: RTT %d ms, lag compensation %s"), ConfigIndex + 1, Configs.Num(), C.RttMs, C.bLagComp ? TEXT("on") : TEXT("off"));
}

AHLCharacter* UHLHitregBenchComponent::FindTarget() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	AHLCharacter* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Me || !It->IsAlive() || !Me)
		{
			continue;
		}
		const float D = FVector::Dist(It->GetActorLocation(), Me->GetActorLocation());
		if (D < BestDist)
		{
			BestDist = D;
			Best = *It;
		}
	}
	return Best;
}

void UHLHitregBenchComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Phase == EPhase::Idle)
	{
		return;
	}
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	AHLCharacter* Me = PC ? Cast<AHLCharacter>(PC->GetPawn()) : nullptr;
	if (!Me || !Me->Weapon)
	{
		return;
	}
	if (bReleaseNextFrame)
	{
		Me->Weapon->SetTriggerHeld(false);
		bReleaseNextFrame = false;
	}

	// Perfect aim at the torso of the target as this client is drawing it
	if (AHLCharacter* Target = FindTarget())
	{
		const FVector Aim = Target->GetPose().Location + FVector(0.f, 0.f, 22.f);
		PC->SetControlRotation((Aim - Me->GetEyeLocation()).Rotation());
	}

	const FHLShotStats& S = Me->Weapon->GetStats();
	if (Me->Weapon->GetLastResultTime() > LastResultTime)
	{
		LastResultTime = Me->Weapon->GetLastResultTime();
		if (Me->Weapon->GetLastResult().bHasTarget)
		{
			RewindSum += Me->Weapon->GetLastResult().RewindMs;
			++RewindCount;
		}
	}
	PhaseTime += DeltaTime;
	switch (Phase)
	{
	case EPhase::Settle:
		if (PhaseTime >= BenchSettleSeconds)
		{
			Phase = EPhase::Shooting;
			PhaseTime = 0.f;
			ShotsFired = 0;
			ShotTimer = 0.f;
			BaseFired = S.Fired;
			BasePredicted = S.PredictedHits;
			BaseConfirmed = S.ConfirmedHits;
			RewindSum = 0.0;
			RewindCount = 0;
		}
		break;
	case EPhase::Shooting:
		ShotTimer -= DeltaTime;
		if (ShotTimer <= 0.f && ShotsFired < ShotsPer)
		{
			if (Me->Weapon->GetAmmo() <= 1)
			{
				Me->Weapon->RequestReload();
				ShotTimer = 2.5f;
				break;
			}
			Me->Weapon->SetTriggerHeld(true); // one frame = one tap
			bReleaseNextFrame = true;
			++ShotsFired;
			ShotTimer = BenchShotGap;
		}
		if (ShotsFired >= ShotsPer && ShotTimer <= 0.f)
		{
			Phase = EPhase::Drain;
			PhaseTime = 0.f;
		}
		break;
	case EPhase::Drain:
		if (PhaseTime >= BenchDrainSeconds)
		{
			FRow Row;
			Row.Config = Configs[ConfigIndex];
			Row.Shots = S.Fired - BaseFired;
			Row.Predicted = S.PredictedHits - BasePredicted;
			Row.Confirmed = S.ConfirmedHits - BaseConfirmed;
			Row.MeanRewindMs = RewindCount > 0 ? float(RewindSum / RewindCount) : 0.f;
			Rows.Add(Row);
			UE_LOG(LogHitreg, Display, TEXT("Hitreg bench: RTT %d lagcomp %d -> %d/%d confirmed (%d predicted), mean rewind %.0f ms"),
				Row.Config.RttMs, Row.Config.bLagComp ? 1 : 0, Row.Confirmed, Row.Shots, Row.Predicted, Row.MeanRewindMs);
			if (++ConfigIndex < Configs.Num())
			{
				BeginConfig();
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

void UHLHitregBenchComponent::Finish()
{
	Phase = EPhase::Idle;
	FString Csv = TEXT("rtt_ms,lag_compensation,shots,predicted_hits,confirmed_hits,confirmed_pct,mean_rewind_ms\n");
	FString Summary;
	for (const FRow& R : Rows)
	{
		const float Pct = R.Shots > 0 ? 100.f * R.Confirmed / R.Shots : 0.f;
		Csv += FString::Printf(TEXT("%d,%d,%d,%d,%d,%.1f,%.1f\n"), R.Config.RttMs, R.Config.bLagComp ? 1 : 0, R.Shots, R.Predicted, R.Confirmed, Pct, R.MeanRewindMs);
		Summary += FString::Printf(TEXT("%dms %s %.0f%%  "), R.Config.RttMs, R.Config.bLagComp ? TEXT("on") : TEXT("off"), Pct);
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / FString::Printf(TEXT("hitreg_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	FFileHelper::SaveStringToFile(Csv, *File);
	UE_LOG(LogHitreg, Display, TEXT("Hitreg bench done: %s-> %s"), *Summary, *File);
	if (AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner()))
	{
		PC->ShowToast(TEXT("Hitreg bench: ") + Summary);
		PC->Server_SetNet(60, 0, 0);
		PC->Server_SetLagComp(true);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("QuitAfterBench")))
	{
		FPlatformMisc::RequestExit(false);
	}
}

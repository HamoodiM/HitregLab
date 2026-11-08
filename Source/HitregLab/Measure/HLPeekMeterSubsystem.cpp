// Hitreg Lab: peeker's-advantage sweep
#include "Measure/HLPeekMeterSubsystem.h"
#include "Measure/HLPeekComponent.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameMode.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Level/HLLevelBuilder.h"
#include "EngineUtils.h"
#include "HitregLab.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	constexpr float SettleSeconds = 2.0f;  // after changing net settings (clock re-sync, buffers refill)
	constexpr float TrialSeconds = 2.2f;
	constexpr float GapSeconds = 0.5f;
	constexpr float StrafeDelay = 0.8f;    // both players standing still before the peek
}

bool UHLPeekMeterSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

bool UHLPeekMeterSubsystem::FindPlayers(AHLPlayerController*& OutPeeker, AHLPlayerController*& OutHolder) const
{
	OutPeeker = nullptr;
	OutHolder = nullptr;
	for (TActorIterator<AHLPlayerController> It(GetWorld()); It; ++It)
	{
		if (!It->GetPawn())
		{
			continue;
		}
		if (!OutPeeker)
		{
			OutPeeker = *It;
		}
		else if (!OutHolder)
		{
			OutHolder = *It;
		}
	}
	return OutPeeker && OutHolder;
}

FString UHLPeekMeterSubsystem::StartSweep(int32 TrialsPerConfig, bool bFullGrid)
{
	AHLPlayerController* P = nullptr;
	AHLPlayerController* H = nullptr;
	if (!FindPlayers(P, H))
	{
		return TEXT("Peek sweep needs two connected players (PIE: Play As Client, 2 players)");
	}
	Peeker = P;
	Holder = H;
	Configs.Reset();
	if (bFullGrid)
	{
		for (const int32 Rtt : { 0, 50, 100, 150 })
		{
			for (const int32 Tick : { 20, 30, 60, 128 })
			{
				Configs.Add({ Rtt, Tick, 2 });
			}
		}
		for (const int32 Rtt : { 0, 50, 100, 150 })
		{
			Configs.Add({ Rtt, 60, 1 });
		}
	}
	else
	{
		const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
		Configs.Add({ GS ? GS->SimulatedRttMs : 0, GS ? GS->ServerTickRate : 60, 2 });
	}
	TrialsPer = FMath::Max(1, TrialsPerConfig);
	ConfigIndex = 0;
	Rows.Reset();
	BeginConfig();
	return FString::Printf(TEXT("Peek sweep: %d configs x %d trials"), Configs.Num(), TrialsPer);
}

void UHLPeekMeterSubsystem::BeginConfig()
{
	const FHLPeekConfig& C = Configs[ConfigIndex];
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->ServerApplyNetSettings(C.TickRate, C.RttMs, 0);
	}
	TrialIndex = 0;
	Phase = EPhase::Settle;
	PhaseTime = 0.f;
	UE_LOG(LogHitreg, Display, TEXT("Peek config %d/%d: RTT %d ms, %d Hz, interp %d ticks"), ConfigIndex + 1, Configs.Num(), C.RttMs, C.TickRate, C.InterpTicks);
}

void UHLPeekMeterSubsystem::BeginTrial()
{
	AHLPlayerController* P = Peeker.Get();
	AHLPlayerController* H = Holder.Get();
	const AHLGameMode* GM = GetWorld()->GetAuthGameMode<AHLGameMode>();
	const AHLLevelBuilder* Builder = GM ? GM->GetBuilder() : nullptr;
	if (!P || !H || !P->GetPawn() || !H->GetPawn() || !Builder)
	{
		Finish();
		return;
	}
	const FHLPeekSetup& S = Builder->GetPeekSetup();
	const FRotator PeekerFacing = (S.PeekerEnd - S.PeekerStart).Rotation();
	const FRotator HolderFacing = (S.Corner - S.HolderSpot).Rotation();
	// Server-side placement; the clients get the same placement in Client_BeginTrial
	P->GetPawn()->TeleportTo(S.PeekerStart, PeekerFacing);
	H->GetPawn()->TeleportTo(S.HolderSpot, HolderFacing);
	P->SetControlRotation(PeekerFacing);
	H->SetControlRotation(HolderFacing);

	++TrialId;
	Current = FHLPeekRow();
	Current.Config = Configs[ConfigIndex];
	Current.Trial = TrialIndex;
	const int32 Interp = Configs[ConfigIndex].InterpTicks;
	if (UHLPeekComponent* PC = P->FindComponentByClass<UHLPeekComponent>())
	{
		PC->Client_BeginTrial(TrialId, EHLPeekRole::Peeker, S.PeekerStart, PeekerFacing, S.PeekerEnd, StrafeDelay, Interp);
	}
	if (UHLPeekComponent* HC = H->FindComponentByClass<UHLPeekComponent>())
	{
		HC->Client_BeginTrial(TrialId, EHLPeekRole::Holder, S.HolderSpot, HolderFacing, S.HolderSpot, StrafeDelay, Interp);
	}
	Phase = EPhase::Trial;
	PhaseTime = 0.f;
}

void UHLPeekMeterSubsystem::ReportSight(AHLPlayerController* From, int32 InTrialId, double ServerTime)
{
	if (Phase != EPhase::Trial || InTrialId != TrialId)
	{
		return;
	}
	if (From == Peeker.Get() && Current.PeekerSees < 0.0)
	{
		Current.PeekerSees = ServerTime;
	}
	else if (From == Holder.Get() && Current.HolderSees < 0.0)
	{
		Current.HolderSees = ServerTime;
	}
}

void UHLPeekMeterSubsystem::EndTrial()
{
	for (AHLPlayerController* PC : { Peeker.Get(), Holder.Get() })
	{
		if (UHLPeekComponent* Comp = PC ? PC->FindComponentByClass<UHLPeekComponent>() : nullptr)
		{
			Comp->Client_EndTrial();
		}
	}
	Rows.Add(Current);
	++TrialIndex;
	Phase = EPhase::Gap;
	PhaseTime = 0.f;
}

void UHLPeekMeterSubsystem::Tick(float DeltaTime)
{
	if (Phase == EPhase::Idle)
	{
		return;
	}
	PhaseTime += DeltaTime;
	switch (Phase)
	{
	case EPhase::Settle:
		if (PhaseTime >= SettleSeconds)
		{
			BeginTrial();
		}
		break;
	case EPhase::Trial:
		if (PhaseTime >= TrialSeconds)
		{
			EndTrial();
		}
		break;
	case EPhase::Gap:
		if (PhaseTime >= GapSeconds)
		{
			if (TrialIndex < TrialsPer)
			{
				BeginTrial();
			}
			else if (++ConfigIndex < Configs.Num())
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

FString UHLPeekMeterSubsystem::Summarize(const TArray<FHLPeekRow>& Rows)
{
	// Median advantage per config
	TMap<FString, TArray<double>> ByConfig;
	TArray<FString> Order;
	for (const FHLPeekRow& R : Rows)
	{
		if (!R.IsValid())
		{
			continue;
		}
		const FString Key = FString::Printf(TEXT("%dms/%dHz/%dt"), R.Config.RttMs, R.Config.TickRate, R.Config.InterpTicks);
		if (!ByConfig.Contains(Key))
		{
			Order.Add(Key);
		}
		ByConfig.FindOrAdd(Key).Add(R.AdvantageMs());
	}
	FString Out;
	for (const FString& Key : Order)
	{
		TArray<double>& V = ByConfig[Key];
		V.Sort();
		Out += FString::Printf(TEXT("%s: %.0f ms   "), *Key, V[V.Num() / 2]);
	}
	return Out.IsEmpty() ? TEXT("No valid peek trials") : Out;
}

void UHLPeekMeterSubsystem::Finish()
{
	Phase = EPhase::Idle;
	FString Csv = TEXT("rtt_ms,tick_hz,interp_ticks,trial,peeker_sees_s,holder_sees_s,advantage_ms,valid\n");
	for (const FHLPeekRow& R : Rows)
	{
		Csv += FString::Printf(TEXT("%d,%d,%d,%d,%.4f,%.4f,%.1f,%d\n"), R.Config.RttMs, R.Config.TickRate, R.Config.InterpTicks, R.Trial,
			R.PeekerSees, R.HolderSees, R.IsValid() ? R.AdvantageMs() : 0.0, R.IsValid() ? 1 : 0);
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / FString::Printf(TEXT("peek_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	FFileHelper::SaveStringToFile(Csv, *File);
	const FString Summary = Summarize(Rows) + TEXT("-> ") + FPaths::GetCleanFilename(File);
	UE_LOG(LogHitreg, Display, TEXT("Peek sweep done: %s"), *Summary);
	for (AHLPlayerController* PC : { Peeker.Get(), Holder.Get() })
	{
		if (UHLPeekComponent* Comp = PC ? PC->FindComponentByClass<UHLPeekComponent>() : nullptr)
		{
			Comp->Client_Summary(Summary);
		}
	}
}

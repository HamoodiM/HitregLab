// Hitreg Lab: Shot Autopsy log
#include "Autopsy/HLShotAutopsySubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Net/HLLagCompensationSubsystem.h"
#include "Weapons/HLWeaponDefinition.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HitregLab.h"

namespace
{
	/** Track spacing and how far it reaches around the shot. */
	constexpr float TrackStepMs = 25.f;
	constexpr float TrackBeforeViewMs = 50.f;
	constexpr float TrackAfterVictimMs = 60.f;
	constexpr float TrackMaxAfterMs = 600.f;
}

bool UHLShotAutopsySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
}

void UHLShotAutopsySubsystem::Deinitialize()
{
	// The session log: written on whichever machine judged shots (dedicated server, listen host, standalone)
	const UWorld* World = GetWorld();
	if (Records.Num() > 0 && World && World->GetNetMode() != NM_Client)
	{
		UE_LOG(LogHitreg, Display, TEXT("Shot autopsy: %s"), *WriteCsv());
	}
	Pending.Reset();
	Records.Reset();
	Super::Deinitialize();
}

float UHLShotAutopsySubsystem::EstimateRttMs(const AHLCharacter* Character)
{
	if (!Character || !Character->IsPlayerControlled())
	{
		return 0.f; // bots live on the server
	}
	float Rtt = 0.f;
	if (const APlayerState* PS = Character->GetPlayerState())
	{
		Rtt = PS->GetPingInMilliseconds();
	}
	if (const AHLGameState* GS = Character->GetWorld()->GetGameState<AHLGameState>())
	{
		Rtt = FMath::Max(Rtt, float(GS->SimulatedRttMs)); // the engine ping lags behind a changed simulation
	}
	return Rtt;
}

void UHLShotAutopsySubsystem::ServerRecord(const AHLCharacter* Shooter, const FHLShotRequest& Request, double ViewTime, FName RejectReason,
	bool bProjectile, const FHLTraceHit* Trace, bool bServerHit, float Damage, bool bKill)
{
	UWorld* World = GetWorld();
	const AHLGameState* GS = World->GetGameState<AHLGameState>();
	const double Now = World->GetTimeSeconds();

	FPending P;
	FHLShotAutopsy& R = P.Record;
	R.ShotId = NextShotId++;
	R.ShotSeq = Request.ShotSeq;
	R.WeaponIndex = Request.WeaponIndex;
	R.ShooterId = Shooter ? Shooter->GetHLPlayerId() : INDEX_NONE;
	R.ServerTime = Now;
	R.ViewAgeMs = float((Now - ViewTime) * 1000.0);
	R.MaxRewindMs = GS ? GS->MaxRewindMs : 200.f;
	R.CoverBudgetMs = GS ? GS->CoverRuleBudgetMs : 100.f;
	R.SetFlag(FHLShotAutopsy::FlagLagComp, GS ? GS->bLagCompensation : true);
	R.SetFlag(FHLShotAutopsy::FlagCoverRule, GS ? GS->bCoverRule : false);
	R.SetFlag(FHLShotAutopsy::FlagKill, bKill);
	R.ShooterRttMs = EstimateRttMs(Shooter);
	R.Damage = Damage;
	const FVector Dir = FVector(Request.Direction).GetSafeNormal();
	FHLPose ShotPose;
	ShotPose.Location = Request.Origin;
	ShotPose.Yaw = Dir.Rotation().Yaw;
	ShotPose.Pitch = Dir.Rotation().Pitch;
	R.Shot = FHLQuantPose::Quantize(ShotPose);

	FHLVerdictInputs In;
	In.RejectReason = RejectReason;
	In.bProjectile = bProjectile;
	In.bLagCompensation = R.HasFlag(FHLShotAutopsy::FlagLagComp);
	In.bServerHit = bServerHit;
	if (Trace)
	{
		In.bViewHit = Trace->bViewHit;
		In.bViewHitThroughWorld = Trace->bViewHitThroughWorld;
		In.Clamp = Trace->Clamp;
		R.Clamp = Trace->Clamp;
		R.Zone = bServerHit ? Trace->Hit.Zone : EHLHitZone::None;
		R.RewindMs = Trace->RewindSeconds * 1000.f;
		R.WorldBlockCm = Trace->WorldLimit;
		R.HitCm = bServerHit ? Trace->Hit.Distance : 0.f;
		if (const AHLCharacter* Subject = Trace->Subject.Get())
		{
			R.TargetId = Subject->GetHLPlayerId();
			R.TargetRttMs = EstimateRttMs(Subject);
			R.ViewPose = FHLQuantPose::Quantize(Trace->ViewPose);
			R.TracedPose = FHLQuantPose::Quantize(Trace->TracedPose);
			R.CurrentPose = FHLQuantPose::Quantize(Trace->CurrentPose);
			R.SetFlag(FHLShotAutopsy::FlagTargetInCover, Trace->bSubjectInCoverNow);
			P.Target = const_cast<AHLCharacter*>(Subject);
		}
	}
	R.Verdict = HLShotAutopsy::Classify(In);
	P.Shooter = const_cast<AHLCharacter*>(Shooter);

	// The track runs from just before the shooter's view to when the victim's own screen would show the result
	R.TrackStepMs = TrackStepMs;
	R.TrackStartMs = -(R.ViewAgeMs + TrackBeforeViewMs);
	const float AfterMs = FMath::Min(R.TargetRttMs + TrackAfterVictimMs, TrackMaxAfterMs);
	P.FinalizeAt = Now + AfterMs / 1000.0 + 0.02;
	if (!P.Target.IsValid())
	{
		P.FinalizeAt = Now; // nothing to track
	}

	OnShotJudged.Broadcast(R);
	Pending.Add(MoveTemp(P));
}

void UHLShotAutopsySubsystem::Tick(float DeltaTime)
{
	if (Pending.Num() == 0)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 i = 0; i < Pending.Num();)
	{
		if (Now >= Pending[i].FinalizeAt)
		{
			Finalize(Pending[i]);
			Pending.RemoveAt(i, EAllowShrinking::No);
		}
		else
		{
			++i;
		}
	}
}

void UHLShotAutopsySubsystem::Finalize(FPending& P)
{
	FHLShotAutopsy& R = P.Record;
	const UHLLagCompensationSubsystem* LagComp = GetWorld()->GetSubsystem<UHLLagCompensationSubsystem>();
	if (AHLCharacter* Target = P.Target.Get())
	{
		const double Now = GetWorld()->GetTimeSeconds();
		const float EndMs = FMath::Min(float((Now - R.ServerTime) * 1000.0), TrackMaxAfterMs);
		for (float T = R.TrackStartMs; T <= EndMs + 0.01f; T += R.TrackStepMs)
		{
			FHLPose Pose = Target->GetPose();
			if (LagComp)
			{
				LagComp->GetPoseAt(Target, R.ServerTime + T / 1000.0, Pose);
			}
			R.Track.Add(FHLQuantPose::Quantize(Pose));
		}
	}
	Store(R);

	// Tell the two players involved (remote clients only; a listen host reads this log directly)
	const TArray<uint8> Bytes = R.ToBytes();
	TArray<AHLPlayerController*, TInlineAllocator<2>> Sent;
	for (const TWeakObjectPtr<AHLCharacter>& Who : { P.Shooter, P.Target })
	{
		AHLPlayerController* PC = Who.IsValid() ? Cast<AHLPlayerController>(Who->GetController()) : nullptr;
		if (PC && !PC->IsLocalController() && !Sent.Contains(PC))
		{
			Sent.Add(PC);
			PC->Client_AutopsyRecord(Bytes);
		}
	}
}

void UHLShotAutopsySubsystem::Store(const FHLShotAutopsy& Record)
{
	if (Records.Num() >= MaxRecords)
	{
		Records.RemoveAt(0, Records.Num() - MaxRecords + 1);
	}
	Records.Add(Record);
	++Revision;
}

void UHLShotAutopsySubsystem::AddReceived(const FHLShotAutopsy& Record)
{
	Store(Record);
}

FString UHLShotAutopsySubsystem::WriteCsv() const
{
	if (Records.Num() == 0)
	{
		return TEXT("No shots recorded");
	}
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString Shots = FHLShotAutopsy::CsvHeader() + TEXT("\n");
	for (const FHLShotAutopsy& R : Records)
	{
		Shots += R.ToCsvRow() + TEXT("\n");
	}
	const FString ShotsFile = Dir / FString::Printf(TEXT("autopsy_%s_shots.csv"), *Stamp);
	const FString SummaryFile = Dir / FString::Printf(TEXT("autopsy_%s_summary.csv"), *Stamp);
	FFileHelper::SaveStringToFile(Shots, *ShotsFile);
	FFileHelper::SaveStringToFile(HLShotAutopsy::SummaryCsv(Records), *SummaryFile);

	const TArray<int32> Counts = HLShotAutopsy::CountByVerdict(Records);
	FString Line = FString::Printf(TEXT("%d shots:"), Records.Num());
	for (int32 i = 0; i < Counts.Num(); ++i)
	{
		if (Counts[i] > 0)
		{
			Line += FString::Printf(TEXT(" %s %d"), HLShotAutopsy::VerdictKey(EHLShotVerdict(i)), Counts[i]);
		}
	}
	return Line + TEXT(" -> ") + FPaths::GetCleanFilename(SummaryFile);
}

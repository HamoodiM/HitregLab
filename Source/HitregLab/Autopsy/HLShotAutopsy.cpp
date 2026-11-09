// Hitreg Lab: Shot Autopsy record
#include "Autopsy/HLShotAutopsy.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	struct FVerdictInfo
	{
		const TCHAR* Key;
		const TCHAR* Sentence;
		FColor Color;
	};

	const FVerdictInfo VerdictTable[] = {
		{ TEXT("hit"), TEXT("Hit: the server tested the target where the shooter saw it."), FColor(70, 220, 100) },
		{ TEXT("hit_clamped"), TEXT("Hit, with the rewind limited (cap or cover rule): the target was still under the crosshair."), FColor(150, 220, 70) },
		{ TEXT("miss_aim"), TEXT("Miss on the shooter's own screen too."), FColor(200, 200, 200) },
		{ TEXT("miss_blocked"), TEXT("A wall was in the way on the shooter's own screen too."), FColor(150, 150, 160) },
		{ TEXT("miss_moved"), TEXT("A hit on the shooter's screen, but lag compensation was off: the server tested the target where it is now."), FColor(240, 90, 70) },
		{ TEXT("miss_capped"), TEXT("A hit on the shooter's screen, but older than the rewind cap: the target had already moved on."), FColor(250, 160, 50) },
		{ TEXT("miss_cover_rule"), TEXT("A hit on the shooter's screen, refused by the cover rule: the target had already reached cover."), FColor(90, 170, 250) },
		{ TEXT("rejected_fire_rate"), TEXT("Rejected: faster than the gun can fire."), FColor(220, 80, 220) },
		{ TEXT("rejected_origin"), TEXT("Rejected: fired from too far from where the server has the shooter."), FColor(220, 80, 220) },
		{ TEXT("rejected_timestamp"), TEXT("Rejected: a view time more than a second old (or in the future)."), FColor(220, 80, 220) },
		{ TEXT("rejected_no_ammo"), TEXT("Rejected: the server says the magazine was empty."), FColor(220, 80, 220) },
		{ TEXT("rejected_dead"), TEXT("Rejected: the shooter was dead or had no weapon on the server."), FColor(220, 80, 220) },
		{ TEXT("rejected_other"), TEXT("Rejected by validation."), FColor(220, 80, 220) },
		{ TEXT("projectile"), TEXT("Projectile: simulated in flight, not traced or lag compensated."), FColor(160, 160, 220) },
	};
	static_assert(UE_ARRAY_COUNT(VerdictTable) == int32(EHLShotVerdict::Count), "one table row per verdict");

	const FVerdictInfo& Info(EHLShotVerdict V)
	{
		const int32 I = FMath::Clamp(int32(V), 0, int32(EHLShotVerdict::Count) - 1);
		return VerdictTable[I];
	}

	/** Version of a single record inside its own byte blob (records travel to clients and into files). */
	constexpr uint8 RecordVersion = 1;
}

EHLShotVerdict HLShotAutopsy::VerdictForRejectReason(FName Reason)
{
	static const FName FireRate(TEXT("fire_rate"));
	static const FName Origin(TEXT("origin"));
	static const FName Timestamp(TEXT("bad_timestamp"));
	static const FName NoAmmo(TEXT("no_ammo"));
	static const FName Dead(TEXT("dead_or_no_weapon"));
	if (Reason == FireRate) return EHLShotVerdict::RejectedFireRate;
	if (Reason == Origin) return EHLShotVerdict::RejectedOrigin;
	if (Reason == Timestamp) return EHLShotVerdict::RejectedTimestamp;
	if (Reason == NoAmmo) return EHLShotVerdict::RejectedNoAmmo;
	if (Reason == Dead) return EHLShotVerdict::RejectedDead;
	return EHLShotVerdict::RejectedOther;
}

EHLShotVerdict HLShotAutopsy::Classify(const FHLVerdictInputs& In)
{
	if (!In.RejectReason.IsNone())
	{
		return VerdictForRejectReason(In.RejectReason);
	}
	if (In.bProjectile)
	{
		return EHLShotVerdict::Projectile;
	}
	if (In.bServerHit)
	{
		return In.Clamp == EHLRewindClamp::None ? EHLShotVerdict::Hit : EHLShotVerdict::HitClamped;
	}
	if (In.bViewHit)
	{
		// The shooter saw a hit; say why the server didn't
		if (!In.bLagCompensation)
		{
			return EHLShotVerdict::MissMoved;
		}
		switch (In.Clamp)
		{
		case EHLRewindClamp::Cap: return EHLShotVerdict::MissCapped;
		case EHLRewindClamp::CoverRule: return EHLShotVerdict::MissCoverRule;
		default: return EHLShotVerdict::MissMoved; // interpolation edge: the history disagreed with the screen
		}
	}
	return In.bViewHitThroughWorld ? EHLShotVerdict::MissBlocked : EHLShotVerdict::MissAim;
}

const TCHAR* HLShotAutopsy::VerdictKey(EHLShotVerdict V) { return Info(V).Key; }
const TCHAR* HLShotAutopsy::VerdictSentence(EHLShotVerdict V) { return Info(V).Sentence; }
FColor HLShotAutopsy::VerdictColor(EHLShotVerdict V) { return Info(V).Color; }

TArray<int32> HLShotAutopsy::CountByVerdict(const TArray<FHLShotAutopsy>& Records)
{
	TArray<int32> Counts;
	Counts.SetNumZeroed(int32(EHLShotVerdict::Count));
	for (const FHLShotAutopsy& R : Records)
	{
		++Counts[FMath::Clamp(int32(R.Verdict), 0, Counts.Num() - 1)];
	}
	return Counts;
}

FString HLShotAutopsy::SummaryCsv(const TArray<FHLShotAutopsy>& Records)
{
	const TArray<int32> Counts = CountByVerdict(Records);
	FString Csv = TEXT("verdict,count,share_pct\n");
	const float Total = FMath::Max(Records.Num(), 1);
	for (int32 i = 0; i < Counts.Num(); ++i)
	{
		Csv += FString::Printf(TEXT("%s,%d,%.1f\n"), VerdictKey(EHLShotVerdict(i)), Counts[i], 100.f * Counts[i] / Total);
	}
	int32 Registered = 0, BehindCover = 0, Clamped = 0;
	double RewindSum = 0.0;
	for (const FHLShotAutopsy& R : Records)
	{
		Registered += R.IsRegistered() ? 1 : 0;
		BehindCover += R.IsBehindCoverHit() ? 1 : 0;
		Clamped += R.Clamp != EHLRewindClamp::None ? 1 : 0;
		RewindSum += R.IsRegistered() ? R.RewindMs : 0.0;
	}
	Csv += FString::Printf(TEXT("total_shots,%d,100.0\n"), Records.Num());
	Csv += FString::Printf(TEXT("registered,%d,%.1f\n"), Registered, 100.f * Registered / Total);
	Csv += FString::Printf(TEXT("registered_on_target_already_in_cover,%d,%.1f\n"), BehindCover, 100.f * BehindCover / Total);
	Csv += FString::Printf(TEXT("rewind_clamped,%d,%.1f\n"), Clamped, 100.f * Clamped / Total);
	Csv += FString::Printf(TEXT("mean_rewind_ms_of_hits,%.1f,\n"), Registered > 0 ? RewindSum / Registered : 0.0);
	return Csv;
}

FVector FHLShotAutopsy::GetOrigin() const
{
	return Shot.Dequantize().Location;
}

FVector FHLShotAutopsy::GetDirection() const
{
	const FHLPose P = Shot.Dequantize();
	return FRotator(P.Pitch, P.Yaw, 0.f).Vector();
}

FVector FHLShotAutopsy::GetRayEnd() const
{
	return GetOrigin() + GetDirection() * (HitCm > 0.f ? HitCm : WorldBlockCm);
}

bool FHLShotAutopsy::SampleTrack(float RelMs, FHLPose& OutPose) const
{
	if (Track.Num() == 0)
	{
		return false;
	}
	const float F = (RelMs - TrackStartMs) / FMath::Max(TrackStepMs, 1e-3f);
	if (F <= 0.f)
	{
		OutPose = Track[0].Dequantize();
		return true;
	}
	if (F >= Track.Num() - 1)
	{
		OutPose = Track.Last().Dequantize();
		return true;
	}
	const int32 I = FMath::FloorToInt(F);
	OutPose = FHLPose::Lerp(Track[I].Dequantize(), Track[I + 1].Dequantize(), F - I);
	return true;
}

FArchive& operator<<(FArchive& Ar, FHLShotAutopsy& A)
{
	uint8 Version = RecordVersion;
	Ar << Version;
	if (Ar.IsLoading() && (Version == 0 || Version > RecordVersion))
	{
		Ar.SetError(); // a record from a newer build: refuse rather than misread
		return Ar;
	}
	uint8 Verdict = uint8(A.Verdict);
	uint8 Clamp = uint8(A.Clamp);
	uint8 Zone = uint8(A.Zone);
	Ar << A.ShotId << A.ShotSeq << A.ShooterId << A.TargetId << Verdict << Clamp << Zone << A.Flags << A.WeaponIndex;
	Ar << A.ServerTime << A.ViewAgeMs << A.RewindMs << A.MaxRewindMs << A.CoverBudgetMs << A.ShooterRttMs << A.TargetRttMs;
	Ar << A.Damage << A.WorldBlockCm << A.HitCm;
	Ar << A.Shot << A.ViewPose << A.TracedPose << A.CurrentPose;
	Ar << A.TrackStartMs << A.TrackStepMs << A.Track;
	if (Ar.IsLoading())
	{
		A.Verdict = EHLShotVerdict(FMath::Min<uint8>(Verdict, uint8(EHLShotVerdict::Count) - 1));
		A.Clamp = EHLRewindClamp(FMath::Min<uint8>(Clamp, uint8(EHLRewindClamp::CoverRule)));
		A.Zone = EHLHitZone(FMath::Min<uint8>(Zone, uint8(EHLHitZone::Legs)));
	}
	return Ar;
}

TArray<uint8> FHLShotAutopsy::ToBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer << const_cast<FHLShotAutopsy&>(*this);
	return Bytes;
}

bool FHLShotAutopsy::FromBytes(const TArray<uint8>& Bytes)
{
	FMemoryReader Reader(Bytes);
	Reader << *this;
	return !Reader.IsError();
}

FString FHLShotAutopsy::CsvHeader()
{
	return TEXT("shot_id,server_time_s,shooter_id,shot_seq,target_id,verdict,clamp,zone,damage,kill,lag_comp,cover_rule,")
		TEXT("target_in_cover_at_server,view_age_ms,rewind_ms,max_rewind_ms,cover_budget_ms,shooter_rtt_ms,target_rtt_ms,")
		TEXT("target_moved_view_to_server_cm,hit_cm,world_block_cm");
}

FString FHLShotAutopsy::ToCsvRow() const
{
	static const TCHAR* ClampNames[] = { TEXT("none"), TEXT("cap"), TEXT("cover_rule") };
	static const TCHAR* ZoneNames[] = { TEXT("none"), TEXT("head"), TEXT("torso"), TEXT("legs") };
	const float Moved = HasTarget() ? float(FVector::Dist(ViewPose.Dequantize().Location, CurrentPose.Dequantize().Location)) : 0.f;
	return FString::Printf(TEXT("%u,%.4f,%d,%u,%d,%s,%s,%s,%.1f,%d,%d,%d,%d,%.1f,%.1f,%.0f,%.0f,%.0f,%.0f,%.1f,%.1f,%.1f"),
		ShotId, ServerTime, ShooterId, uint32(ShotSeq), TargetId, HLShotAutopsy::VerdictKey(Verdict), ClampNames[FMath::Min(int32(Clamp), 2)],
		ZoneNames[FMath::Min(int32(Zone), 3)], Damage, HasFlag(FlagKill) ? 1 : 0, HasFlag(FlagLagComp) ? 1 : 0, HasFlag(FlagCoverRule) ? 1 : 0,
		HasFlag(FlagTargetInCover) ? 1 : 0, ViewAgeMs, RewindMs, MaxRewindMs, CoverBudgetMs, ShooterRttMs, TargetRttMs, Moved, HitCm, WorldBlockCm);
}

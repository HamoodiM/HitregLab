// Hitreg Lab: the Shot Autopsy record. The server writes one per shot with its verdict and the reason, plus
// everything needed to redraw the decision later: the shooter's ray, the target where the shooter saw it,
// where the server tested it, where it really was, and a short track of the target around the shot.
// Pure data: classification, serialization (kept in .hlreplay files) and CSV, no world access.
#pragma once

#include "CoreMinimal.h"
#include "Core/HLTypes.h"
#include "Net/HLRewindRules.h"
#include "Replay/HLQuantPose.h"
#include "HLShotAutopsy.generated.h"

UENUM()
enum class EHLShotVerdict : uint8
{
	/** Registered: the server tested the target where the shooter saw it. */
	Hit,
	/** Registered, but the rewind was limited (cap or cover rule) and the target was still under the crosshair. */
	HitClamped,
	/** Missed on the shooter's own screen too. */
	MissAim,
	/** World geometry was in the way on the shooter's own screen too. */
	MissBlocked,
	/** A hit on the shooter's screen; the server tested the target where it is now (lag compensation off). */
	MissMoved,
	/** A hit on the shooter's screen, but the view was older than the rewind cap and the target had moved. */
	MissCapped,
	/** A hit on the shooter's screen, refused by the cover rule: the target had already reached cover. */
	MissCoverRule,
	RejectedFireRate,
	RejectedOrigin,
	RejectedTimestamp,
	RejectedNoAmmo,
	RejectedDead,
	RejectedOther,
	/** Projectile weapons are not traced (and not lag compensated). */
	Projectile,
	Count UMETA(Hidden)
};

/** What the classifier needs to know about one judged shot. */
struct FHLVerdictInputs
{
	/** NAME_None if the shot passed validation. */
	FName RejectReason;
	bool bProjectile = false;
	bool bLagCompensation = true;
	/** The server's test (at the rewound, clamped pose) hit a character. */
	bool bServerHit = false;
	/** At the shooter's own view time the ray hit a character before any wall. */
	bool bViewHit = false;
	/** At the shooter's own view time the ray would have hit a character if walls were ignored. */
	bool bViewHitThroughWorld = false;
	/** Clamp applied to the target the shot was about. */
	EHLRewindClamp Clamp = EHLRewindClamp::None;
};

struct HITREGLAB_API FHLShotAutopsy
{
	enum EFlags : uint8
	{
		FlagLagComp = 1 << 0,
		FlagCoverRule = 1 << 1,
		/** The target was out of the shooter's line of sight at its server position when the shot was judged. */
		FlagTargetInCover = 1 << 2,
		FlagKill = 1 << 3,
	};

	/** Server-wide order of the shot. */
	uint32 ShotId = 0;
	/** The shooter's own sequence number (matches FHLShotRequest::ShotSeq). */
	uint16 ShotSeq = 0;
	int32 ShooterId = INDEX_NONE;
	/** The character the shot was about (hit, or under the crosshair on the shooter's screen); INDEX_NONE if none. */
	int32 TargetId = INDEX_NONE;
	EHLShotVerdict Verdict = EHLShotVerdict::MissAim;
	EHLRewindClamp Clamp = EHLRewindClamp::None;
	EHLHitZone Zone = EHLHitZone::None;
	uint8 Flags = 0;
	uint8 WeaponIndex = 0;

	/** Server time the shot was judged. Every other time below is relative to it, in milliseconds. */
	double ServerTime = 0.0;
	/** How old the shooter's screen was: ServerTime - ViewTime. */
	float ViewAgeMs = 0.f;
	/** How far back the server actually tested the target. */
	float RewindMs = 0.f;
	float MaxRewindMs = 0.f;
	float CoverBudgetMs = 0.f;
	/** Round trips (server estimate) of the shooter and the target, for the "when did each screen see it" marks. */
	float ShooterRttMs = 0.f;
	float TargetRttMs = 0.f;
	float Damage = 0.f;
	/** Distance along the ray to the first wall (or the weapon range), and to the hit (0 if none). */
	float WorldBlockCm = 0.f;
	float HitCm = 0.f;

	/** The ray: Location = eye origin, Yaw/Pitch = shot direction (spread included). */
	FHLQuantPose Shot;
	/** Target at the shooter's view time, where the server tested it, and where it was when judged. */
	FHLQuantPose ViewPose;
	FHLQuantPose TracedPose;
	FHLQuantPose CurrentPose;
	/** The target's server track around the shot, evenly spaced from TrackStartMs (relative to ServerTime). */
	float TrackStartMs = 0.f;
	float TrackStepMs = 1000.f / 30.f;
	TArray<FHLQuantPose> Track;

	bool HasTarget() const { return TargetId != INDEX_NONE; }
	bool HasFlag(EFlags F) const { return (Flags & F) != 0; }
	void SetFlag(EFlags F, bool bOn) { Flags = uint8(bOn ? (Flags | F) : (Flags & ~F)); }
	bool IsRegistered() const { return Verdict == EHLShotVerdict::Hit || Verdict == EHLShotVerdict::HitClamped; }
	bool IsRejected() const { return Verdict >= EHLShotVerdict::RejectedFireRate && Verdict <= EHLShotVerdict::RejectedOther; }
	/** A registered hit on a target that had already left the shooter's line of sight on the server. */
	bool IsBehindCoverHit() const { return IsRegistered() && HasFlag(FlagTargetInCover); }

	FVector GetOrigin() const;
	FVector GetDirection() const;
	FVector GetRayEnd() const;
	float GetTrackEndMs() const { return TrackStartMs + TrackStepMs * FMath::Max(Track.Num() - 1, 0); }
	/** Target pose at a time relative to ServerTime (held at the ends of the track). False if there is no track. */
	bool SampleTrack(float RelMs, FHLPose& OutPose) const;

	friend FArchive& operator<<(FArchive& Ar, FHLShotAutopsy& A);
	TArray<uint8> ToBytes() const;
	bool FromBytes(const TArray<uint8>& Bytes);

	static FString CsvHeader();
	FString ToCsvRow() const;
};

namespace HLShotAutopsy
{
	HITREGLAB_API EHLShotVerdict Classify(const FHLVerdictInputs& In);
	HITREGLAB_API EHLShotVerdict VerdictForRejectReason(FName Reason);

	/** Short machine name used in CSVs ("miss_capped"). */
	HITREGLAB_API const TCHAR* VerdictKey(EHLShotVerdict V);
	/** One plain sentence for the viewer. */
	HITREGLAB_API const TCHAR* VerdictSentence(EHLShotVerdict V);
	HITREGLAB_API FColor VerdictColor(EHLShotVerdict V);

	/** Counts by verdict plus a few totals, as CSV ("verdict,count" rows). */
	HITREGLAB_API FString SummaryCsv(const TArray<FHLShotAutopsy>& Records);
	HITREGLAB_API TArray<int32> CountByVerdict(const TArray<FHLShotAutopsy>& Records);
}

// Hitreg Lab: shared gameplay and network types
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "HLTypes.generated.h"

UENUM(BlueprintType)
enum class EHLHitboxShape : uint8
{
	Sphere,
	Box
};

UENUM(BlueprintType)
enum class EHLHitZone : uint8
{
	None,
	Head,
	Torso,
	Legs
};

/** One rigid hitbox, in actor space. Characters are yaw-only rigid bodies, so a pose is location + yaw. */
USTRUCT(BlueprintType)
struct FHLHitbox
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Hitbox)
	EHLHitZone Zone = EHLHitZone::Torso;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Hitbox)
	EHLHitboxShape Shape = EHLHitboxShape::Box;

	/** Centre in actor space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Hitbox)
	FVector Center = FVector::ZeroVector;

	/** Box half extents, or X = sphere radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Hitbox)
	FVector Extent = FVector(10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Hitbox)
	float DamageMultiplier = 1.f;
};

/** Everything needed to place a character's hitboxes (and to draw it in a replay). */
USTRUCT(BlueprintType)
struct FHLPose
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	float Yaw = 0.f;

	/** Aim pitch: not used for hit testing (hitboxes don't pitch), used to draw the aim and for kill cams. */
	UPROPERTY()
	float Pitch = 0.f;

	static FHLPose Lerp(const FHLPose& A, const FHLPose& B, float Alpha);
};

/** Server -> clients state for one character. Clients render simulated proxies by interpolating these. */
USTRUCT()
struct FHLNetSnapshot
{
	GENERATED_BODY()

	/** Server world time this state belongs to (seconds). */
	UPROPERTY()
	double ServerTime = 0.0;

	UPROPERTY()
	FVector_NetQuantize10 Location;

	UPROPERTY()
	float Yaw = 0.f;

	UPROPERTY()
	float Pitch = 0.f;
};

/** Client -> server: one fired shot. The direction already includes spread and any bullet magnetism. */
USTRUCT()
struct FHLShotRequest
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize10 Origin;

	UPROPERTY()
	FVector_NetQuantizeNormal Direction;

	/** Server time at which the shooter's screen showed the other players (render time of its interpolation). */
	UPROPERTY()
	double ViewTime = 0.0;

	/** Server time at which the client fired (its synced clock). */
	UPROPERTY()
	double FireTime = 0.0;

	UPROPERTY()
	uint16 ShotSeq = 0;

	UPROPERTY()
	uint8 WeaponIndex = 0;
};

/** Server -> shooting client: what the server decided, plus enough to draw the rewind. */
USTRUCT()
struct FHLShotResult
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 ShotSeq = 0;

	UPROPERTY()
	EHLHitZone Zone = EHLHitZone::None;

	UPROPERTY()
	bool bKill = false;

	UPROPERTY()
	float Damage = 0.f;

	/** Rejected by validation (fire rate, origin, rewind too old...). */
	UPROPERTY()
	bool bRejected = false;

	UPROPERTY()
	FName RejectReason;

	/** Debug: the target where the server traced it (rewound if lag compensation was on) and where it is now. */
	UPROPERTY()
	bool bHasTarget = false;

	UPROPERTY()
	FHLPose TracedPose;

	UPROPERTY()
	FHLPose CurrentPose;

	UPROPERTY()
	float RewindMs = 0.f;
};

USTRUCT()
struct FHLKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Killer;

	UPROPERTY()
	FString Victim;

	UPROPERTY()
	FString Weapon;

	UPROPERTY()
	bool bHeadshot = false;

	UPROPERTY()
	double ServerTime = 0.0;
};

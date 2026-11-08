// Hitreg Lab: ray tests against rigid hitboxes (pure functions, no world access)
#pragma once

#include "CoreMinimal.h"
#include "Core/HLTypes.h"

struct FHLHitboxHit
{
	float Distance = TNumericLimits<float>::Max();
	EHLHitZone Zone = EHLHitZone::None;
	float DamageMultiplier = 1.f;
	int32 HitboxIndex = INDEX_NONE;
};

namespace HLHitboxMath
{
	/** World transform of a pose: translation + yaw. */
	HITREGLAB_API FTransform PoseToTransform(const FHLPose& Pose);

	/** Ray (Origin, unit Dir) vs sphere. Returns the entry distance in [0, MaxDist]. */
	HITREGLAB_API bool RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Center, float Radius, float MaxDist, float& OutDist);

	/** Ray vs oriented box (slab test in the box's local space). */
	HITREGLAB_API bool RayBox(const FVector& Origin, const FVector& Dir, const FTransform& BoxToWorld, const FVector& HalfExtent, float MaxDist, float& OutDist);

	/** Nearest hit of a ray against a set of hitboxes placed at Pose. */
	HITREGLAB_API bool RayHitboxes(const FVector& Origin, const FVector& Dir, float MaxDist, const TArray<FHLHitbox>& Hitboxes, const FHLPose& Pose, FHLHitboxHit& OutHit);

	/** World-space centre of one hitbox at a pose. */
	HITREGLAB_API FVector HitboxCenter(const FHLHitbox& Hitbox, const FHLPose& Pose);
}

// Hitreg Lab: ray tests against rigid hitboxes
#include "Net/HLHitboxMath.h"

namespace HLHitboxMath
{
	FTransform PoseToTransform(const FHLPose& Pose)
	{
		return FTransform(FRotator(0.f, Pose.Yaw, 0.f), Pose.Location);
	}

	FVector HitboxCenter(const FHLHitbox& Hitbox, const FHLPose& Pose)
	{
		return PoseToTransform(Pose).TransformPosition(Hitbox.Center);
	}

	bool RaySphere(const FVector& Origin, const FVector& Dir, const FVector& Center, float Radius, float MaxDist, float& OutDist)
	{
		// |O + tD - C|^2 = r^2 with unit D:  t^2 + 2bt + c = 0, b = D.(O-C), c = |O-C|^2 - r^2
		const FVector M = Origin - Center;
		const double B = FVector::DotProduct(M, Dir);
		const double C = M.SizeSquared() - double(Radius) * Radius;
		if (C > 0.0 && B > 0.0)
		{
			return false; // outside and pointing away
		}
		const double Disc = B * B - C;
		if (Disc < 0.0)
		{
			return false;
		}
		double T = -B - FMath::Sqrt(Disc);
		if (T < 0.0)
		{
			T = 0.0; // origin inside the sphere
		}
		if (T > MaxDist)
		{
			return false;
		}
		OutDist = float(T);
		return true;
	}

	bool RayBox(const FVector& Origin, const FVector& Dir, const FTransform& BoxToWorld, const FVector& HalfExtent, float MaxDist, float& OutDist)
	{
		// Slab test in the box's own space
		const FVector O = BoxToWorld.InverseTransformPosition(Origin);
		const FVector D = BoxToWorld.InverseTransformVector(Dir);
		double TMin = 0.0;
		double TMax = MaxDist;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Ext = HalfExtent[Axis];
			if (FMath::Abs(D[Axis]) < UE_SMALL_NUMBER)
			{
				if (O[Axis] < -Ext || O[Axis] > Ext)
				{
					return false; // parallel to this slab and outside it
				}
				continue;
			}
			const double Inv = 1.0 / D[Axis];
			double T1 = (-Ext - O[Axis]) * Inv;
			double T2 = (Ext - O[Axis]) * Inv;
			if (T1 > T2)
			{
				Swap(T1, T2);
			}
			TMin = FMath::Max(TMin, T1);
			TMax = FMath::Min(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}
		OutDist = float(TMin);
		return true;
	}

	bool RayHitboxes(const FVector& Origin, const FVector& Dir, float MaxDist, const TArray<FHLHitbox>& Hitboxes, const FHLPose& Pose, FHLHitboxHit& OutHit)
	{
		const FTransform ActorXf = PoseToTransform(Pose);
		bool bAny = false;
		for (int32 i = 0; i < Hitboxes.Num(); ++i)
		{
			const FHLHitbox& Box = Hitboxes[i];
			float Dist = 0.f;
			bool bHit = false;
			if (Box.Shape == EHLHitboxShape::Sphere)
			{
				bHit = RaySphere(Origin, Dir, ActorXf.TransformPosition(Box.Center), Box.Extent.X, MaxDist, Dist);
			}
			else
			{
				const FTransform BoxXf(ActorXf.GetRotation(), ActorXf.TransformPosition(Box.Center));
				bHit = RayBox(Origin, Dir, BoxXf, Box.Extent, MaxDist, Dist);
			}
			if (bHit && Dist < OutHit.Distance)
			{
				OutHit.Distance = Dist;
				OutHit.Zone = Box.Zone;
				OutHit.DamageMultiplier = Box.DamageMultiplier;
				OutHit.HitboxIndex = i;
				bAny = true;
			}
		}
		return bAny;
	}
}

// Hitreg Lab: aim assist maths (pure: no world access, so it runs in tests and in the bot-thumb benchmark).
#pragma once

#include "CoreMinimal.h"
#include "Aim/HLAimAssistSettings.h"

struct FHLAimTarget
{
	/** Point the assist aims at (torso centre), and the head centre for the acquire test / bullet magnetism. */
	FVector Point = FVector::ZeroVector;
	FVector Head = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
};

struct FHLAimInput
{
	FVector CamLoc = FVector::ZeroVector;
	FRotator CamRot = FRotator::ZeroRotator;
	FVector PlayerVelocity = FVector::ZeroVector;
	/** Look stick after the response curve, [-1, 1]. */
	FVector2D Look = FVector2D::ZeroVector;
	/** Move stick / keys magnitude, [0, 1]. */
	float MoveInput = 0.f;
	bool bAds = false;
	float WeaponScale = 1.f;
};

struct FHLAimOutput
{
	/** Multiply the stick look rate by this. */
	float SlowdownScale = 1.f;
	/** Degrees per second to add (X = yaw, Y = pitch). */
	FVector2D PullRate = FVector2D::ZeroVector;
	int32 TargetIndex = INDEX_NONE;
	float TargetAngleDeg = 0.f;
	float SlowdownBubbleDeg = 0.f;
	float PullBubbleDeg = 0.f;
};

namespace HLAimAssist
{
	/** Radial dead zones + response exponent. Keeps the direction, reshapes the magnitude. */
	HITREGLAB_API FVector2D ApplyStickCurve(const UHLAimAssistSettings& S, const FVector2D& Raw);

	/** Angle in degrees between the camera's forward and the direction to Point. */
	HITREGLAB_API float AngleTo(const FVector& CamLoc, const FRotator& CamRot, const FVector& Point);

	/** Yaw/pitch (degrees) the camera would need to look straight at Point. */
	HITREGLAB_API FRotator LookAt(const FVector& CamLoc, const FVector& Point);

	/** Slowdown and pull for this frame. */
	HITREGLAB_API FHLAimOutput Compute(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, const FHLAimInput& In, TArrayView<const FHLAimTarget> Targets);

	/** Bends a shot direction onto the nearest head/torso centre inside the magnetism cone (Full mode only). */
	HITREGLAB_API FVector ApplyBulletMagnetism(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, const FVector& CamLoc, const FVector& ShotDir, TArrayView<const FHLAimTarget> Targets, float WeaponScale);

	/** Look rate (deg/s) from a curved stick value, with ADS scale and the edge-hold acceleration boost. */
	HITREGLAB_API FVector2D LookRate(const UHLAimAssistSettings& S, const FVector2D& Curved, bool bAds, float EdgeHoldSeconds);
}

/** "Bot thumb" benchmark: a scripted player tries to put the crosshair on a strafing target's head. */
struct FHLAimBenchResult
{
	/** Seconds until the crosshair stayed on the head for 100 ms (or the trial length if never). */
	float TimeToAcquire = 0.f;
	/** Largest angle the crosshair passed the target by, after first crossing it (degrees). */
	float OvershootDeg = 0.f;
	/** Fraction of the trial (after first acquire) spent on the head. */
	float TrackingFraction = 0.f;
	/** Total camera rotation produced while the thumb gave no input (must be zero). */
	float IdleRotationDeg = 0.f;
	bool bAcquired = false;
};

namespace HLAimBench
{
	/**
	 * One seeded trial: target 20 m away strafing at running speed, crosshair starting off to the side.
	 * The thumb is a PD controller on the stick with a 200 ms reaction delay, noise and saturation.
	 */
	HITREGLAB_API FHLAimBenchResult RunTrial(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, int32 Seed, float TrialSeconds = 3.f);

	/** Median time-to-acquire etc. over N seeded trials. */
	HITREGLAB_API FHLAimBenchResult RunMedian(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, int32 Trials, int32 FirstSeed = 1);

	/** Writes all modes x trials to CSV (Saved/Measure/aim_<date>.csv) and returns a summary line. */
	HITREGLAB_API FString RunToCsv(const UHLAimAssistSettings& S, int32 Trials, const FString& Directory);
}

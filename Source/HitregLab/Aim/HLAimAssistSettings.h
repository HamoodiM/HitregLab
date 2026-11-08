// Hitreg Lab: controller look and aim assist tuning (one data asset, edited live from the Aim Assist Tuner tab).
// Structure follows Insomniac's "Techniques for Building Aim Assist in Console Shooters" (GDC 2013):
// stick response curve -> slowdown (friction) near targets -> rotational pull while the player is moving
// -> bullet magnetism inside a tiny cone.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HLAimAssistSettings.generated.h"

UENUM(BlueprintType)
enum class EHLAimAssistMode : uint8
{
	Off,
	SlowdownOnly,
	Full
};

UCLASS(BlueprintType)
class HITREGLAB_API UHLAimAssistSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// --- Stick response ---

	/** Radial dead zone: stick deflection below this reads as zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick", meta = (ClampMin = "0", ClampMax = "0.5"))
	float InnerDeadzone = 0.12f;

	/** Deflection above this reads as full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick", meta = (ClampMin = "0.5", ClampMax = "1"))
	float OuterDeadzone = 0.95f;

	/** 1 = linear, 2 = squared (fine control near the centre). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick", meta = (ClampMin = "1", ClampMax = "4"))
	float ResponseExponent = 2.f;

	/** Degrees per second at full deflection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick")
	float MaxYawRate = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick")
	float MaxPitchRate = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick", meta = (ClampMin = "0.1", ClampMax = "1"))
	float AdsRateScale = 0.55f;

	/** Holding the stick at the edge this long starts the turn boost... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick")
	float AccelDelay = 0.15f;

	/** ...which ramps up over this long... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick")
	float AccelRampTime = 0.35f;

	/** ...to this many extra degrees per second of yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stick")
	float AccelExtraYawRate = 160.f;

	// --- Target selection ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
	float MaxRangeCm = 6000.f;

	/** Assist never reaches further than this from the crosshair, however close the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Targeting")
	float MaxAssistAngleDeg = 8.f;

	// --- Slowdown (friction) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slowdown")
	bool bSlowdown = true;

	/** Size of the slowdown bubble around the target, in cm (so it shrinks on screen with distance). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slowdown")
	float SlowdownRadiusCm = 70.f;

	/** Look rate multiplier at the target's centre (hip / ADS). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slowdown", meta = (ClampMin = "0.05", ClampMax = "1"))
	float SlowdownMinScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slowdown", meta = (ClampMin = "0.05", ClampMax = "1"))
	float SlowdownMinScaleAds = 0.38f;

	// --- Rotational pull (magnetism) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull")
	bool bPull = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull")
	float PullRadiusCm = 90.f;

	/** Fraction of the target's angular velocity (relative to the player) added to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull", meta = (ClampMin = "0", ClampMax = "1"))
	float PullTrackingStrength = 0.5f;

	/** Degrees per second of correction per degree of aim error. Kept small: this is the "aimbot" knob. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull")
	float PullCenteringRate = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull")
	float PullMaxDegPerSec = 24.f;

	/** Pull only while the player is giving look or move input: no input, no assist. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pull")
	float MinInputForPull = 0.1f;

	// --- Bullet magnetism ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bullet Magnetism")
	bool bBulletMagnetism = true;

	/** Shots within this cone of a hitbox centre are bent onto it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bullet Magnetism")
	float BulletMagnetismConeDeg = 0.9f;
};

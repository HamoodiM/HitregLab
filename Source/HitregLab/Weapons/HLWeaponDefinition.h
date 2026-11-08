// Hitreg Lab: one weapon's tuning, as a data asset. Everything a designer would change lives here.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HLWeaponDefinition.generated.h"

UENUM(BlueprintType)
enum class EHLFireMode : uint8
{
	Semi,
	Auto
};

UCLASS(BlueprintType)
class HITREGLAB_API UHLWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName = FText::FromString(TEXT("Rifle"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	EHLFireMode FireMode = EHLFireMode::Auto;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "30"))
	float RoundsPerMinute = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = "0"))
	float ReloadTime = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FLinearColor Color = FLinearColor(0.15f, 0.16f, 0.18f);

	// --- Damage ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float Damage = 25.f;

	/** Head hits multiply by this on top of the hitbox's own multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float HeadshotMultiplier = 1.f;

	/** Full damage up to FalloffStart, linear down to Damage * MinDamageScale at FalloffEnd (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float FalloffStart = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float FalloffEnd = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = "0", ClampMax = "1"))
	float MinDamageScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage")
	float Range = 15000.f;

	// --- Projectile (if not hitscan) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile")
	bool bProjectile = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bProjectile"))
	float ProjectileSpeed = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bProjectile"))
	float ProjectileGravityScale = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Projectile", meta = (EditCondition = "bProjectile"))
	float SplashRadius = 250.f;

	// --- Spread (degrees, cone half-angle) ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadBase = 0.4f;

	/** Added per shot (bloom). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadPerShot = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadMax = 3.f;

	/** How fast bloom decays when not firing (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadRecovery = 8.f;

	/** Extra spread at full running speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadMoving = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread")
	float SpreadInAir = 3.f;

	/** All spread is scaled by this while aiming down sights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = "0", ClampMax = "1"))
	float AdsSpreadScale = 0.3f;

	// --- Recoil ---

	/** Per-shot camera kick in degrees: X = pitch up, Y = yaw right. Shot N uses entry N (the last entry repeats). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2D> RecoilPattern;

	/** Kick is applied over this long, so it reads as a motion rather than a teleport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float RecoilApplyTime = 0.06f;

	/** After you stop firing, the uncompensated part of the pitch kick returns at this rate (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	float RecoilRecoveryRate = 9.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = "0", ClampMax = "1"))
	float AdsRecoilScale = 0.75f;

	// --- Aim down sights ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	float AdsFov = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	float AdsTime = 0.18f;

	/** Per-weapon scale on aim assist strength (snipers want less). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ADS")
	float AimAssistScale = 1.f;

	// --- Helpers (pure, tested) ---

	float GetShotInterval() const { return 60.f / FMath::Max(RoundsPerMinute, 1.f); }

	/** Damage at a distance, before hit-zone multipliers. */
	float DamageAtDistance(float DistanceCm) const;

	/** Recoil kick for the Nth consecutive shot (0-based). */
	FVector2D RecoilForShot(int32 ShotIndex) const;

	/** Target spread for the current state (before bloom). */
	float SpreadFor(float SpeedAlpha, bool bInAir, bool bAds) const;

	/** Makes an in-memory definition with code defaults, for tests and as a fallback when no assets are set. */
	static UHLWeaponDefinition* MakeTransient(UObject* Outer, const FString& Preset);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId(TEXT("HLWeapon"), GetFName()); }
};

/** Frame-rate independent fire timing. Works out which shots fall inside this frame, at their exact times. */
struct HITREGLAB_API FHLFireClock
{
	double NextShotTime = 0.0;
	bool bWasHeld = false;

	/**
	 * Call every frame. Appends the time of each shot due in (previous frame, Now] to OutShotTimes.
	 * On a fresh trigger press the first shot is immediate (no shots "saved up" while idle).
	 * Semi-auto fires once per press, still respecting the minimum interval.
	 */
	void Advance(double Now, bool bTriggerHeld, double Interval, bool bAuto, TArray<double>& OutShotTimes, int32 MaxShotsPerFrame = 8);
};

/**
 * Server-side fire-rate check: a token bucket refilled at the weapon's rate (+10% slack), holding at most a
 * few tokens so packets that arrive bunched together are fine but a modified client can't out-fire the gun.
 */
struct HITREGLAB_API FHLRateLimiter
{
	float Tokens = 3.f;
	double LastTime = -1.0;
	float Burst = 3.f;

	bool TryConsume(double Now, float ShotsPerSecond)
	{
		if (LastTime >= 0.0)
		{
			Tokens = FMath::Min(Burst, Tokens + float(Now - LastTime) * ShotsPerSecond * 1.1f);
		}
		LastTime = Now;
		if (Tokens < 1.f)
		{
			return false;
		}
		Tokens -= 1.f;
		return true;
	}
};

/** Spread bloom state: grows per shot, decays over time toward the stance's base value. */
struct HITREGLAB_API FHLSpreadModel
{
	float Bloom = 0.f; // degrees added on top of the stance spread

	void OnShot(const UHLWeaponDefinition& Weapon);
	void Recover(const UHLWeaponDefinition& Weapon, float DeltaSeconds);
	float Current(const UHLWeaponDefinition& Weapon, float SpeedAlpha, bool bInAir, bool bAds) const;

	/** Deterministic direction inside a cone of HalfAngleDeg around Dir (uniform over the cone's solid angle). */
	static FVector SampleCone(const FVector& Dir, float HalfAngleDeg, int32 Seed);
};

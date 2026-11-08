// Hitreg Lab: weapon definition and its pure helpers
#include "Weapons/HLWeaponDefinition.h"

float UHLWeaponDefinition::DamageAtDistance(float DistanceCm) const
{
	if (DistanceCm <= FalloffStart || FalloffEnd <= FalloffStart)
	{
		return Damage;
	}
	const float Alpha = FMath::Clamp((DistanceCm - FalloffStart) / (FalloffEnd - FalloffStart), 0.f, 1.f);
	return Damage * FMath::Lerp(1.f, MinDamageScale, Alpha);
}

FVector2D UHLWeaponDefinition::RecoilForShot(int32 ShotIndex) const
{
	if (RecoilPattern.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}
	return RecoilPattern[FMath::Clamp(ShotIndex, 0, RecoilPattern.Num() - 1)];
}

float UHLWeaponDefinition::SpreadFor(float SpeedAlpha, bool bInAir, bool bAds) const
{
	float Spread = SpreadBase + SpreadMoving * FMath::Clamp(SpeedAlpha, 0.f, 1.f);
	if (bInAir)
	{
		Spread += SpreadInAir;
	}
	return bAds ? Spread * AdsSpreadScale : Spread;
}

UHLWeaponDefinition* UHLWeaponDefinition::MakeTransient(UObject* Outer, const FString& Preset)
{
	UHLWeaponDefinition* W = NewObject<UHLWeaponDefinition>(Outer ? Outer : GetTransientPackage());
	if (Preset == TEXT("SMG"))
	{
		W->DisplayName = FText::FromString(TEXT("SMG"));
		W->RoundsPerMinute = 900.f;
		W->MagazineSize = 32;
		W->Damage = 18.f;
		W->FalloffStart = 1200.f;
		W->FalloffEnd = 3000.f;
		W->SpreadBase = 0.8f;
		W->SpreadPerShot = 0.18f;
		W->RecoilPattern = { {0.35, 0.05}, {0.4, -0.1}, {0.4, 0.15}, {0.35, -0.2}, {0.3, 0.2} };
	}
	else if (Preset == TEXT("Marksman"))
	{
		W->DisplayName = FText::FromString(TEXT("Marksman"));
		W->FireMode = EHLFireMode::Semi;
		W->RoundsPerMinute = 240.f;
		W->MagazineSize = 10;
		W->Damage = 55.f;
		W->HeadshotMultiplier = 1.25f;
		W->FalloffStart = 6000.f;
		W->FalloffEnd = 9000.f;
		W->SpreadBase = 0.15f;
		W->SpreadPerShot = 0.9f;
		W->RecoilPattern = { {2.2, 0.3} };
		W->AdsFov = 40.f;
		W->AimAssistScale = 0.5f;
	}
	else if (Preset == TEXT("Launcher"))
	{
		W->DisplayName = FText::FromString(TEXT("Launcher"));
		W->FireMode = EHLFireMode::Semi;
		W->RoundsPerMinute = 70.f;
		W->MagazineSize = 4;
		W->Damage = 90.f;
		W->bProjectile = true;
		W->SpreadBase = 0.f;
		W->SpreadMoving = 0.f;
		W->RecoilPattern = { {3.5, 0.0} };
		W->AimAssistScale = 0.f;
	}
	else
	{
		W->DisplayName = FText::FromString(TEXT("Rifle"));
		W->RecoilPattern = { {0.55, 0.0}, {0.6, 0.1}, {0.6, 0.15}, {0.55, -0.1}, {0.5, -0.25}, {0.5, -0.2}, {0.45, 0.2}, {0.45, 0.3} };
	}
	return W;
}

void FHLFireClock::Advance(double Now, bool bTriggerHeld, double Interval, bool bAuto, TArray<double>& OutShotTimes, int32 MaxShotsPerFrame)
{
	const bool bPressed = bTriggerHeld && !bWasHeld;
	bWasHeld = bTriggerHeld;
	if (!bTriggerHeld)
	{
		return;
	}
	if (bPressed)
	{
		// Don't bank shots while idle, but still respect the interval if the press comes right after a shot
		NextShotTime = FMath::Max(NextShotTime, Now);
	}
	if (!bAuto)
	{
		if (bPressed && NextShotTime <= Now)
		{
			OutShotTimes.Add(NextShotTime);
			NextShotTime += Interval;
		}
		return;
	}
	int32 Shots = 0;
	while (NextShotTime <= Now && Shots < MaxShotsPerFrame)
	{
		OutShotTimes.Add(NextShotTime);
		NextShotTime += Interval;
		++Shots;
	}
	if (Shots == MaxShotsPerFrame && NextShotTime <= Now)
	{
		NextShotTime = Now; // a huge hitch: drop the backlog instead of machine-gunning it out
	}
}

void FHLSpreadModel::OnShot(const UHLWeaponDefinition& Weapon)
{
	Bloom = FMath::Min(Bloom + Weapon.SpreadPerShot, Weapon.SpreadMax);
}

void FHLSpreadModel::Recover(const UHLWeaponDefinition& Weapon, float DeltaSeconds)
{
	Bloom = FMath::Max(0.f, Bloom - Weapon.SpreadRecovery * DeltaSeconds);
}

float FHLSpreadModel::Current(const UHLWeaponDefinition& Weapon, float SpeedAlpha, bool bInAir, bool bAds) const
{
	const float Total = Weapon.SpreadFor(SpeedAlpha, bInAir, false) + Bloom;
	return FMath::Min(Total, Weapon.SpreadMax + Weapon.SpreadInAir) * (bAds ? Weapon.AdsSpreadScale : 1.f);
}

FVector FHLSpreadModel::SampleCone(const FVector& Dir, float HalfAngleDeg, int32 Seed)
{
	if (HalfAngleDeg <= 0.f)
	{
		return Dir.GetSafeNormal();
	}
	FRandomStream Stream(Seed);
	return Stream.VRandCone(Dir.GetSafeNormal(), FMath::DegreesToRadians(HalfAngleDeg));
}

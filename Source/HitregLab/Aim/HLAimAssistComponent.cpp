// Hitreg Lab: aim assist component
#include "Aim/HLAimAssistComponent.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameMode.h"
#include "Net/HLHitboxMath.h"
#include "Weapons/HLWeaponComponent.h"
#include "Weapons/HLWeaponDefinition.h"
#include "EngineUtils.h"

EHLAimAssistMode UHLAimAssistComponent::LocalMode = EHLAimAssistMode::Full;

UHLAimAssistComponent::UHLAimAssistComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

EHLAimAssistMode UHLAimAssistComponent::GetLocalMode(const UObject* WorldContext)
{
	return LocalMode;
}

void UHLAimAssistComponent::SetLocalMode(EHLAimAssistMode Mode)
{
	LocalMode = Mode;
}

UHLAimAssistSettings* UHLAimAssistComponent::GetSettings() const
{
	if (!CachedSettings)
	{
		// The asset path comes from the game mode's config, readable on clients through the class default object
		CachedSettings = Cast<UHLAimAssistSettings>(GetDefault<AHLGameMode>()->AimAssistSettings.TryLoad());
		if (!CachedSettings)
		{
			CachedSettings = NewObject<UHLAimAssistSettings>(const_cast<UHLAimAssistComponent*>(this));
		}
	}
	return CachedSettings;
}

float UHLAimAssistComponent::GetWeaponScale() const
{
	const AHLCharacter* Char = Cast<AHLCharacter>(GetOwner());
	const UHLWeaponDefinition* Weapon = Char && Char->Weapon ? Char->Weapon->GetCurrentWeapon() : nullptr;
	return Weapon ? Weapon->AimAssistScale : 1.f;
}

bool UHLAimAssistComponent::WasUsedRecently() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() - LastUseTime < 2.0;
}

void UHLAimAssistComponent::GatherTargets(float DeltaSeconds)
{
	Targets.Reset();
	const AHLCharacter* Self = Cast<AHLCharacter>(GetOwner());
	TMap<TWeakObjectPtr<AActor>, FVector> Current;
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		AHLCharacter* Other = *It;
		if (Other == Self || !Other->IsAlive())
		{
			continue;
		}
		// Use the pose this client is displaying: that's what the player is aiming at
		const FHLPose Pose = Other->GetPose();
		FHLAimTarget T;
		T.Point = Pose.Location + FVector(0.f, 0.f, 22.f); // torso centre
		T.Head = Pose.Location + FVector(0.f, 0.f, 66.f);
		if (const FVector* Prev = PreviousPoints.Find(Other))
		{
			T.Velocity = DeltaSeconds > 0.f ? (T.Point - *Prev) / DeltaSeconds : FVector::ZeroVector;
		}
		Current.Add(Other, T.Point);
		Targets.Add(T);
	}
	PreviousPoints = MoveTemp(Current);
}

FVector2D UHLAimAssistComponent::ProcessStick(const FVector2D& RawStick, float MoveInput, float DeltaSeconds)
{
	const UHLAimAssistSettings& S = *GetSettings();
	const AHLCharacter* Char = Cast<AHLCharacter>(GetOwner());
	GatherTargets(DeltaSeconds);

	const FVector2D Curved = HLAimAssist::ApplyStickCurve(S, RawStick);
	EdgeHold = FMath::Abs(Curved.X) > 0.95f ? EdgeHold + DeltaSeconds : 0.f;
	const bool bAds = Char && Char->IsAiming();

	FHLAimInput In;
	In.CamLoc = Char ? Char->GetEyeLocation() : FVector::ZeroVector;
	In.CamRot = Char ? Char->GetViewRotation() : FRotator::ZeroRotator;
	In.PlayerVelocity = Char ? Char->GetVelocity() : FVector::ZeroVector;
	In.Look = Curved;
	In.MoveInput = MoveInput;
	In.bAds = bAds;
	In.WeaponScale = GetWeaponScale();
	LastOutput = HLAimAssist::Compute(S, LocalMode, In, Targets);
	if (!RawStick.IsNearlyZero() || MoveInput > 0.f)
	{
		LastUseTime = GetWorld()->GetTimeSeconds();
	}

	const FVector2D Rate = HLAimAssist::LookRate(S, Curved, bAds, EdgeHold) * LastOutput.SlowdownScale + LastOutput.PullRate;
	return Rate * DeltaSeconds;
}

FVector UHLAimAssistComponent::BendShot(const FVector& CamLoc, const FVector& ShotDir) const
{
	return HLAimAssist::ApplyBulletMagnetism(*GetSettings(), LocalMode, CamLoc, ShotDir, Targets, GetWeaponScale());
}

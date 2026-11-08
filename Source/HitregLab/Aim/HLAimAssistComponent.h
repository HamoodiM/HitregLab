// Hitreg Lab: runs controller look + aim assist for a locally controlled character (gamepad only).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Aim/HLAimAssist.h"
#include "HLAimAssistComponent.generated.h"

class AHLCharacter;

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLAimAssistComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLAimAssistComponent();

	/**
	 * Turns this frame's raw right-stick value into a camera rotation delta (degrees: X yaw, Y pitch),
	 * applying the response curve, slowdown and pull. MoveInput is the left-stick magnitude.
	 */
	FVector2D ProcessStick(const FVector2D& RawStick, float MoveInput, float DeltaSeconds);

	/** Bullet magnetism for a shot fired with the gamepad. */
	FVector BendShot(const FVector& CamLoc, const FVector& ShotDir) const;

	UHLAimAssistSettings* GetSettings() const;

	/** Mode is a local player setting (F6 cycles it). */
	static EHLAimAssistMode GetLocalMode(const UObject* WorldContext);
	static void SetLocalMode(EHLAimAssistMode Mode);

	/** Last frame's result, for the debug HUD. */
	const FHLAimOutput& GetLastOutput() const { return LastOutput; }
	const TArray<FHLAimTarget>& GetLastTargets() const { return Targets; }
	bool WasUsedRecently() const;

private:
	void GatherTargets(float DeltaSeconds);
	float GetWeaponScale() const;

	UPROPERTY(Transient)
	mutable TObjectPtr<UHLAimAssistSettings> CachedSettings;

	TArray<FHLAimTarget> Targets;
	TMap<TWeakObjectPtr<AActor>, FVector> PreviousPoints;
	FHLAimOutput LastOutput;
	float EdgeHold = 0.f;
	double LastUseTime = -100.0;
	static EHLAimAssistMode LocalMode;
};

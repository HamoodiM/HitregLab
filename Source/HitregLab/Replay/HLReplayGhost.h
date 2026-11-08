// Hitreg Lab: a local-only stand-in for a player during a kill cam (same primitive body, not replicated).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/HLTypes.h"
#include "HLReplayGhost.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

UCLASS(NotPlaceable)
class HITREGLAB_API AHLReplayGhost : public AActor
{
	GENERATED_BODY()

public:
	AHLReplayGhost();

	void SetColor(const FLinearColor& Color);
	void ApplyPose(const FHLPose& Pose, bool bAlive);
	/** First-person view: hide the body, keep a view model. */
	void SetFirstPerson(bool bFirstPerson);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

private:
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY()
	TObjectPtr<USceneComponent> AimPivot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Gun;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ViewModel;
};

// Hitreg Lab: simple server-side bots. "strafe" = range dummy, "fight" = strafes and shoots back with a
// reaction delay and aim error, "hold" = stands still facing its spawn direction.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "HLBotController.generated.h"

class AHLCharacter;

UCLASS()
class HITREGLAB_API AHLBotController : public AAIController
{
	GENERATED_BODY()

public:
	AHLBotController();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	UPROPERTY(EditAnywhere, Category = "Bot")
	FString Mode = TEXT("strafe");

	UPROPERTY(EditAnywhere, Category = "Bot")
	FTransform HomeTransform;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float StrafeAmplitude = 250.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float StrafePeriod = 2.6f;

	/** Fight bots: seconds from first seeing you to starting to shoot. */
	UPROPERTY(EditAnywhere, Category = "Bot")
	float ReactionTime = 0.45f;

	/** Fight bots: aim error right after reacting (degrees), shrinking as they track you. */
	UPROPERTY(EditAnywhere, Category = "Bot")
	float InitialAimError = 4.f;

	UPROPERTY(EditAnywhere, Category = "Bot")
	float TurnRate = 220.f;

private:
	AHLCharacter* FindVisibleEnemy() const;
	float Phase = 0.f;
	double SeenSince = -1.0;
	TWeakObjectPtr<AHLCharacter> Target;
	FRotator AimNoise = FRotator::ZeroRotator;
};

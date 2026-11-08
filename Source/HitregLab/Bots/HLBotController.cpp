// Hitreg Lab: bots
#include "Bots/HLBotController.h"
#include "Core/HLCharacter.h"
#include "Weapons/HLWeaponComponent.h"
#include "EngineUtils.h"

AHLBotController::AHLBotController()
{
	bWantsPlayerState = true;
	// We steer the control rotation ourselves
	bSetControlRotationFromPawnOrientation = false;
	PrimaryActorTick.bCanEverTick = true;
}

void AHLBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	SetControlRotation(HomeTransform.Rotator());
	Phase = FMath::FRandRange(0.f, StrafePeriod);
	SeenSince = -1.0;
}

AHLCharacter* AHLBotController::FindVisibleEnemy() const
{
	const AHLCharacter* Me = Cast<AHLCharacter>(GetPawn());
	if (!Me)
	{
		return nullptr;
	}
	AHLCharacter* Best = nullptr;
	float BestDist = 6000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLBotSight), true, Me);
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		AHLCharacter* Other = *It;
		if (Other == Me || !Other->IsAlive() || !Other->IsPlayerControlled())
		{
			continue;
		}
		const float Dist = FVector::Dist(Other->GetActorLocation(), Me->GetActorLocation());
		if (Dist > BestDist)
		{
			continue;
		}
		Params.ClearIgnoredActors();
		Params.AddIgnoredActor(Me);
		Params.AddIgnoredActor(Other);
		if (!GetWorld()->LineTraceTestByChannel(Me->GetEyeLocation(), Other->GetEyeLocation(), ECC_Visibility, Params))
		{
			Best = Other;
			BestDist = Dist;
		}
	}
	return Best;
}

void AHLBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AHLCharacter* Me = Cast<AHLCharacter>(GetPawn());
	if (!Me || !Me->IsAlive())
	{
		return;
	}
	Phase += DeltaSeconds;

	// Strafe left/right around home, sideways to the home facing
	FVector2D Move = FVector2D::ZeroVector;
	if (Mode == TEXT("strafe") || Mode == TEXT("fight"))
	{
		const FVector Right = FRotationMatrix(HomeTransform.Rotator()).GetUnitAxis(EAxis::Y);
		const float Desired = StrafeAmplitude * FMath::Sin(2.f * UE_PI * Phase / StrafePeriod);
		const float Current = FVector::DotProduct(Me->GetActorLocation() - HomeTransform.GetLocation(), Right);
		const float Lateral = FMath::Clamp((Desired - Current) / 60.f, -1.f, 1.f);
		// Movement input is relative to the control yaw; convert the world-space wish into it
		const FVector Wish = Right * Lateral;
		const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
		Move.Y = FVector::DotProduct(Wish, FRotationMatrix(Yaw).GetUnitAxis(EAxis::X));
		Move.X = FVector::DotProduct(Wish, FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y));
	}

	bool bFire = false;
	if (Mode == TEXT("fight"))
	{
		AHLCharacter* Enemy = FindVisibleEnemy();
		const double Now = GetWorld()->GetTimeSeconds();
		if (Enemy != Target.Get())
		{
			Target = Enemy;
			SeenSince = Enemy ? Now : -1.0;
			AimNoise = FRotator(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f) * InitialAimError;
		}
		if (Enemy)
		{
			const FRotator Want = (Enemy->GetActorLocation() + FVector(0.f, 0.f, 22.f) - Me->GetEyeLocation()).Rotation() + AimNoise;
			const FRotator NewRot = FMath::RInterpConstantTo(GetControlRotation(), Want, DeltaSeconds, TurnRate);
			SetControlRotation(NewRot);
			AimNoise = FMath::RInterpTo(AimNoise, FRotator::ZeroRotator, DeltaSeconds, 1.5f);
			const float Error = FMath::Abs(FRotator::NormalizeAxis(NewRot.Yaw - Want.Yaw)) + FMath::Abs(FRotator::NormalizeAxis(NewRot.Pitch - Want.Pitch));
			bFire = Now - SeenSince > ReactionTime && Error < 2.5f;
		}
		else
		{
			SetControlRotation(FMath::RInterpConstantTo(GetControlRotation(), HomeTransform.Rotator(), DeltaSeconds, 90.f));
		}
		if (Me->Weapon && Me->Weapon->GetAmmo() <= 0)
		{
			Me->Weapon->RequestReload();
		}
	}
	Me->SetBotInputs(Move, bFire, false);
}

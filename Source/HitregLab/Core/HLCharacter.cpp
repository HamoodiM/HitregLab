// Hitreg Lab: character
#include "Core/HLCharacter.h"
#include "Aim/HLAimAssistComponent.h"
#include "Core/HLGameMode.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Net/HLServerClockComponent.h"
#include "Weapons/HLWeaponComponent.h"
#include "Weapons/HLWeaponDefinition.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CapsuleRadius = 34.f;
	constexpr float CapsuleHalfHeight = 88.f;
	const FVector ViewModelHip(32.f, 14.f, -16.f);
	const FVector ViewModelAds(30.f, 0.f, -9.5f);
}

TArray<FHLHitbox> AHLCharacter::MakeDefaultHitboxes()
{
	// Actor origin is the capsule centre; the capsule spans z = -88..88
	TArray<FHLHitbox> Boxes;
	FHLHitbox Head;
	Head.Zone = EHLHitZone::Head;
	Head.Shape = EHLHitboxShape::Sphere;
	Head.Center = FVector(0.f, 0.f, 66.f);
	Head.Extent = FVector(16.f);
	Head.DamageMultiplier = 2.f;
	Boxes.Add(Head);

	FHLHitbox Torso;
	Torso.Zone = EHLHitZone::Torso;
	Torso.Center = FVector(0.f, 0.f, 22.f);
	Torso.Extent = FVector(16.f, 24.f, 28.f);
	Torso.DamageMultiplier = 1.f;
	Boxes.Add(Torso);

	FHLHitbox Legs;
	Legs.Zone = EHLHitZone::Legs;
	Legs.Center = FVector(0.f, 0.f, -46.f);
	Legs.Extent = FVector(12.f, 20.f, 40.f);
	Legs.DamageMultiplier = 0.75f;
	Boxes.Add(Legs);
	return Boxes;
}

FLinearColor AHLCharacter::ColorForPlayer(int32 PlayerId)
{
	static const FLinearColor Palette[] = {
		FLinearColor(0.85f, 0.32f, 0.22f), FLinearColor(0.20f, 0.55f, 0.90f), FLinearColor(0.95f, 0.75f, 0.20f),
		FLinearColor(0.35f, 0.75f, 0.40f), FLinearColor(0.70f, 0.40f, 0.85f), FLinearColor(0.20f, 0.80f, 0.80f),
		FLinearColor(0.95f, 0.50f, 0.65f), FLinearColor(0.55f, 0.55f, 0.58f)
	};
	return Palette[FMath::Abs(PlayerId) % UE_ARRAY_COUNT(Palette)];
}

AHLCharacter::AHLCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	// Remote players are driven by our own snapshots + interpolation, not the engine's replicated movement
	SetReplicateMovement(false);
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(60.f);

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->MaxWalkSpeed = 600.f;
	Move->MaxAcceleration = 4096.f;
	Move->BrakingDecelerationWalking = 4096.f;
	Move->GroundFriction = 10.f;
	Move->JumpZVelocity = 480.f;
	Move->AirControl = 0.35f;
	Move->NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, EyeHeight));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(BaseFov);

	Weapon = CreateDefaultSubobject<UHLWeaponComponent>(TEXT("Weapon"));
	AimAssist = CreateDefaultSubobject<UHLAimAssistComponent>(TEXT("AimAssist"));

	Hitboxes = MakeDefaultHitboxes();
	BuildBody();
}

void AHLCharacter::BuildBody()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	auto MakePart = [&](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* InMesh, const FVector& Loc, const FVector& Scale, const FRotator& Rot = FRotator::ZeroRotator)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(InMesh);
		Part->SetRelativeLocation(Loc);
		Part->SetRelativeRotation(Rot);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	};

	// One mesh per hitbox: basic shapes are 100 cm across, so scale = extent * 2 / 100
	for (const FHLHitbox& Box : Hitboxes)
	{
		const FString Name = FString::Printf(TEXT("Body_%d"), BodyParts.Num());
		UStaticMeshComponent* Part = Box.Shape == EHLHitboxShape::Sphere
			? MakePart(*Name, GetCapsuleComponent(), SphereMesh.Object, Box.Center, FVector(Box.Extent.X * 2.f / 100.f))
			: MakePart(*Name, GetCapsuleComponent(), CubeMesh.Object, Box.Center, Box.Extent * 2.f / 100.f);
		Part->SetOwnerNoSee(true);
		BodyParts.Add(Part);
	}
	// Visor: shows which way the character faces
	UStaticMeshComponent* Visor = MakePart(TEXT("Visor"), GetCapsuleComponent(), CubeMesh.Object, FVector(12.f, 0.f, 68.f), FVector(0.1f, 0.26f, 0.08f));
	Visor->SetOwnerNoSee(true);
	BodyParts.Add(Visor);

	// Gun held at the shoulder, pitched with the aim
	AimPivot = CreateDefaultSubobject<USceneComponent>(TEXT("AimPivot"));
	AimPivot->SetupAttachment(GetCapsuleComponent());
	AimPivot->SetRelativeLocation(FVector(0.f, 22.f, 40.f));
	ThirdPersonGun = MakePart(TEXT("ThirdPersonGun"), AimPivot, CubeMesh.Object, FVector(26.f, 0.f, 0.f), FVector(0.55f, 0.07f, 0.1f));
	ThirdPersonGun->SetOwnerNoSee(true);

	// First-person view model, owner only
	ViewModelRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewModelRoot"));
	ViewModelRoot->SetupAttachment(Camera);
	ViewModelRoot->SetRelativeLocation(ViewModelHip);
	ViewModelBody = MakePart(TEXT("ViewModelBody"), ViewModelRoot, CubeMesh.Object, FVector(0.f, 0.f, 0.f), FVector(0.5f, 0.07f, 0.1f));
	ViewModelBarrel = MakePart(TEXT("ViewModelBarrel"), ViewModelRoot, CylinderMesh.Object, FVector(32.f, 0.f, 2.f), FVector(0.035f, 0.035f, 0.2f), FRotator(-90.f, 0.f, 0.f));
	for (UStaticMeshComponent* VM : { ViewModelBody.Get(), ViewModelBarrel.Get() })
	{
		VM->SetOnlyOwnerSee(true);
		VM->SetCastShadow(false);
	}
}

void AHLCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHLCharacter, Health);
	DOREPLIFETIME(AHLCharacter, bDead);
	DOREPLIFETIME(AHLCharacter, KilledBy);
	DOREPLIFETIME(AHLCharacter, DeathServerTime);
	DOREPLIFETIME(AHLCharacter, bKilledByHeadshot);
	DOREPLIFETIME_CONDITION(AHLCharacter, Snapshot, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AHLCharacter, bAds, COND_SkipOwner);
}

void AHLCharacter::BeginPlay()
{
	Super::BeginPlay();

	static const FName ColorParam(TEXT("Color"));
	for (UStaticMeshComponent* Part : BodyParts)
	{
		BodyMaterials.Add(Part->CreateDynamicMaterialInstance(0));
	}
	BodyMaterials.Add(ThirdPersonGun->CreateDynamicMaterialInstance(0));
	if (UMaterialInstanceDynamic* M = ViewModelBody->CreateDynamicMaterialInstance(0))
	{
		M->SetVectorParameterValue(ColorParam, FLinearColor(0.08f, 0.09f, 0.1f));
	}
	if (UMaterialInstanceDynamic* M = ViewModelBarrel->CreateDynamicMaterialInstance(0))
	{
		M->SetVectorParameterValue(ColorParam, FLinearColor(0.04f, 0.04f, 0.045f));
	}
	ApplyBodyColor();

	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		// We place simulated proxies ourselves from snapshots; the movement component must not fight that
		GetCharacterMovement()->SetComponentTickEnabled(false);
		InterpBuffer.Push(UHLServerClockComponent::ServerNow(this), GetPose());
	}
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		SetNetUpdateFrequency(float(GS->ServerTickRate));
		SetMinNetUpdateFrequency(float(GS->ServerTickRate));
	}
}

void AHLCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	ApplyBodyColor();
}

void AHLCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	ApplyBodyColor();
}

int32 AHLCharacter::GetHLPlayerId() const
{
	return GetPlayerState() ? GetPlayerState()->GetPlayerId() : 0;
}

void AHLCharacter::ApplyBodyColor()
{
	BodyColor = ColorForPlayer(GetHLPlayerId());
	static const FName ColorParam(TEXT("Color"));
	for (int32 i = 0; i < BodyMaterials.Num(); ++i)
	{
		if (BodyMaterials[i])
		{
			// Visor and gun stay dark; the hitbox parts carry the player colour
			const bool bAccent = i >= Hitboxes.Num();
			BodyMaterials[i]->SetVectorParameterValue(ColorParam, bAccent ? FLinearColor(0.05f, 0.05f, 0.06f) : BodyColor);
		}
	}
}

FHLPose AHLCharacter::GetPose() const
{
	FHLPose Pose;
	Pose.Location = GetActorLocation();
	Pose.Yaw = GetActorRotation().Yaw;
	Pose.Pitch = GetViewRotation().Pitch;
	return Pose;
}

FRotator AHLCharacter::GetViewRotation() const
{
	if (Controller)
	{
		return Super::GetViewRotation();
	}
	// Simulated proxy: yaw from the actor, pitch from the snapshot stream
	return FRotator(AimPivot ? AimPivot->GetRelativeRotation().Pitch : 0.f, GetActorRotation().Yaw, 0.f);
}

FVector AHLCharacter::GetEyeLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, EyeHeight);
}

float AHLCharacter::GetSpeedAlpha() const
{
	return FMath::Clamp(GetVelocity().Size2D() / FMath::Max(GetCharacterMovement()->MaxWalkSpeed, 1.f), 0.f, 1.f);
}

double AHLCharacter::GetRemoteRenderTime(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return 0.0;
	}
	const double Now = UHLServerClockComponent::ServerNow(WorldContext);
	if (World->GetNetMode() != NM_Client)
	{
		return Now; // the server (and listen host) see everyone at their current position
	}
	// Snapshots stamped at server time T arrive here at about T + one-way latency, so the newest data we can
	// have is (Now - RTT/2). Render a fixed interpolation delay behind that, so there's always a pair to blend.
	const AHLGameState* GS = World->GetGameState<AHLGameState>();
	const AHLPlayerController* PC = Cast<AHLPlayerController>(World->GetFirstPlayerController());
	const int32 InterpTicks = PC ? PC->InterpTicks : 2;
	const double OneWay = (PC && PC->Clock) ? PC->Clock->GetBestRttMs() / 2000.0 : 0.0;
	return Now - OneWay - (GS ? GS->GetInterpDelaySeconds(InterpTicks) : 0.05);
}

void AHLCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		Snapshot.ServerTime = GetWorld()->GetTimeSeconds();
		Snapshot.Location = GetActorLocation();
		Snapshot.Yaw = GetActorRotation().Yaw;
		Snapshot.Pitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
		if (AimPivot && !IsLocallyControlled())
		{
			AimPivot->SetRelativeRotation(FRotator(Snapshot.Pitch, 0.f, 0.f));
		}
		// Bots: apply their movement input on the server
		if (!IsPlayerControlled() && Controller && IsAlive())
		{
			const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);
			AddMovementInput(FRotationMatrix(YawRot).GetUnitAxis(EAxis::X), MoveInput.Y);
			AddMovementInput(FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y), MoveInput.X);
		}
	}

	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		UpdateSimulatedProxy(DeltaSeconds);
	}
	if (IsLocallyControlled())
	{
		UpdateLocalView(DeltaSeconds);
	}

	// ADS blend (all machines, for the third-person gun and FOV)
	const UHLWeaponDefinition* Def = Weapon ? Weapon->GetCurrentWeapon() : nullptr;
	const float AdsTime = Def ? FMath::Max(Def->AdsTime, 0.01f) : 0.18f;
	AdsAlpha = FMath::FInterpConstantTo(AdsAlpha, bAds ? 1.f : 0.f, DeltaSeconds, 1.f / AdsTime);
}

void AHLCharacter::OnRep_Snapshot()
{
	FHLPose Pose;
	Pose.Location = Snapshot.Location;
	Pose.Yaw = Snapshot.Yaw;
	Pose.Pitch = Snapshot.Pitch;
	InterpBuffer.Push(Snapshot.ServerTime, Pose);
}

void AHLCharacter::UpdateSimulatedProxy(float DeltaSeconds)
{
	if (InterpBuffer.Num() == 0)
	{
		return;
	}
	FHLPose Pose;
	InterpBuffer.Sample(GetRemoteRenderTime(this), Pose); // clamps (holds the newest) if snapshots run dry
	SetActorLocationAndRotation(Pose.Location, FRotator(0.f, Pose.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	AimPivot->SetRelativeRotation(FRotator(Pose.Pitch, 0.f, 0.f));
}

void AHLCharacter::UpdateLocalView(float DeltaSeconds)
{
	AHLPlayerController* PC = Cast<AHLPlayerController>(Controller);
	const UHLWeaponDefinition* Def = Weapon ? Weapon->GetCurrentWeapon() : nullptr;

	// Gamepad look goes through the response curve and aim assist every frame (not just on input events)
	if (AimAssist && IsAlive())
	{
		const FVector2D Delta = AimAssist->ProcessStick(StickInput, MoveInput.Size(), DeltaSeconds);
		if (!Delta.IsNearlyZero())
		{
			AddControllerYawInput(Delta.X);
			AddControllerPitchInput(Delta.Y);
			if (Delta.Y < 0.f)
			{
				RecoilRecoveryPitch = FMath::Max(0.f, RecoilRecoveryPitch + Delta.Y); // player pulled down: that part is compensated
			}
		}
	}

	// Recoil: apply the kick over RecoilApplyTime, then recover the uncompensated pitch after firing stops
	if (Def)
	{
		if (!PendingRecoil.IsNearlyZero())
		{
			const float Alpha = FMath::Min(1.f, DeltaSeconds / FMath::Max(Def->RecoilApplyTime, 0.001f));
			const FVector2D Step = PendingRecoil * Alpha;
			AddControllerPitchInput(Step.X);
			AddControllerYawInput(Step.Y);
			PendingRecoil -= Step;
			RecoilRecoveryPitch += Step.X;
		}
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastShotTime > 0.12 && RecoilRecoveryPitch > 0.f)
		{
			const float Rec = FMath::Min(Def->RecoilRecoveryRate * DeltaSeconds, RecoilRecoveryPitch);
			AddControllerPitchInput(-Rec);
			RecoilRecoveryPitch -= Rec;
		}
	}

	// FOV and view model: hip <-> ADS, plus a kick-back that settles
	const float TargetFov = Def ? FMath::Lerp(BaseFov, Def->AdsFov, AdsAlpha) : BaseFov;
	Camera->SetFieldOfView(TargetFov);
	ViewKick = FMath::FInterpTo(ViewKick, 0.f, DeltaSeconds, 18.f);
	FVector VM = FMath::Lerp(ViewModelHip, ViewModelAds, FMath::SmoothStep(0.f, 1.f, AdsAlpha));
	VM.X -= ViewKick * 3.f;
	VM.Z += ViewKick * 0.4f;
	ViewModelRoot->SetRelativeLocation(VM);
	if (Weapon && Weapon->IsReloading())
	{
		ViewModelRoot->SetRelativeRotation(FRotator(-25.f * FMath::Sin(Weapon->GetReloadAlpha() * UE_PI), 0.f, 15.f * FMath::Sin(Weapon->GetReloadAlpha() * UE_PI)));
	}
	else
	{
		ViewModelRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AHLCharacter::AddRecoil(const FVector2D& KickDeg)
{
	PendingRecoil += KickDeg;
	LastShotTime = GetWorld()->GetTimeSeconds();
}

void AHLCharacter::KickViewModel(float Amount)
{
	ViewKick += Amount;
}

void AHLCharacter::SetBotInputs(const FVector2D& Move, bool bFire, bool bWantAds)
{
	MoveInput = Move;
	bAds = bWantAds;
	if (Weapon)
	{
		Weapon->SetTriggerHeld(bFire);
	}
}

bool AHLCharacter::ServerApplyDamage(float Damage, AController* InstigatorController, EHLHitZone Zone, const FString& WeaponName)
{
	if (!HasAuthority() || bDead || Damage <= 0.f)
	{
		return false;
	}
	Health = FMath::Max(0.f, Health - Damage);
	if (Health > 0.f)
	{
		return false;
	}
	bDead = true;
	KilledBy = InstigatorController ? InstigatorController->PlayerState : nullptr;
	DeathServerTime = GetWorld()->GetTimeSeconds();
	bKilledByHeadshot = Zone == EHLHitZone::Head;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->DisableMovement();
	if (Weapon)
	{
		Weapon->SetTriggerHeld(false);
	}
	OnRep_Dead();
	if (AHLGameMode* GM = GetWorld()->GetAuthGameMode<AHLGameMode>())
	{
		GM->OnCharacterKilled(this, InstigatorController, bKilledByHeadshot, WeaponName);
	}
	return true;
}

void AHLCharacter::OnRep_Health()
{
}

void AHLCharacter::OnRep_Dead()
{
	if (bDead)
	{
		SetBodyVisible(false);
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OnDied.Broadcast(this);
	}
}

void AHLCharacter::SetBodyVisible(bool bVisible)
{
	for (UStaticMeshComponent* Part : BodyParts)
	{
		Part->SetVisibility(bVisible);
	}
	ThirdPersonGun->SetVisibility(bVisible);
	ViewModelRoot->SetVisibility(bVisible, true);
}

// --- Input ---

void AHLCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	AHLPlayerController* PC = Cast<AHLPlayerController>(Controller);
	if (!EIC || !PC)
	{
		return;
	}
	const FHLInputActions& A = PC->GetActions();
	EIC->BindAction(A.Move, ETriggerEvent::Triggered, this, &AHLCharacter::InputMove);
	EIC->BindAction(A.Move, ETriggerEvent::Completed, this, &AHLCharacter::InputMoveReleased);
	EIC->BindAction(A.LookMouse, ETriggerEvent::Triggered, this, &AHLCharacter::InputLookMouse);
	EIC->BindAction(A.LookStick, ETriggerEvent::Triggered, this, &AHLCharacter::InputLookStick);
	EIC->BindAction(A.LookStick, ETriggerEvent::Completed, this, &AHLCharacter::InputLookStickReleased);
	EIC->BindAction(A.Fire, ETriggerEvent::Started, this, &AHLCharacter::InputFirePressed);
	EIC->BindAction(A.Fire, ETriggerEvent::Completed, this, &AHLCharacter::InputFireReleased);
	EIC->BindAction(A.Ads, ETriggerEvent::Started, this, &AHLCharacter::InputAdsPressed);
	EIC->BindAction(A.Ads, ETriggerEvent::Completed, this, &AHLCharacter::InputAdsReleased);
	EIC->BindAction(A.Reload, ETriggerEvent::Started, this, &AHLCharacter::InputReload);
	EIC->BindAction(A.Jump, ETriggerEvent::Started, this, &AHLCharacter::InputJump);
	EIC->BindAction(A.NextWeapon, ETriggerEvent::Started, this, &AHLCharacter::InputNextWeapon);
	if (A.WeaponSlots.Num() >= 4)
	{
		EIC->BindAction(A.WeaponSlots[0], ETriggerEvent::Started, this, &AHLCharacter::InputSlot1);
		EIC->BindAction(A.WeaponSlots[1], ETriggerEvent::Started, this, &AHLCharacter::InputSlot2);
		EIC->BindAction(A.WeaponSlots[2], ETriggerEvent::Started, this, &AHLCharacter::InputSlot3);
		EIC->BindAction(A.WeaponSlots[3], ETriggerEvent::Started, this, &AHLCharacter::InputSlot4);
	}
}

void AHLCharacter::InputMove(const FInputActionValue& Value)
{
	if (!IsAlive())
	{
		return;
	}
	MoveInput = Value.Get<FVector2D>();
	const FRotator YawRot(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRot).GetUnitAxis(EAxis::X), MoveInput.Y);
	AddMovementInput(FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y), MoveInput.X);
}

void AHLCharacter::InputMoveReleased(const FInputActionValue& Value)
{
	MoveInput = FVector2D::ZeroVector;
}

void AHLCharacter::InputLookMouse(const FInputActionValue& Value)
{
	if (!IsAlive())
	{
		return;
	}
	const AHLPlayerController* PC = Cast<AHLPlayerController>(Controller);
	const float Sens = PC ? PC->MouseSensitivity : 0.07f;
	const FVector2D V = Value.Get<FVector2D>() * Sens * (bAds ? 0.8f : 1.f);
	AddControllerYawInput(V.X);
	AddControllerPitchInput(V.Y);
	if (V.Y < 0.f)
	{
		RecoilRecoveryPitch = FMath::Max(0.f, RecoilRecoveryPitch + V.Y);
	}
}

void AHLCharacter::InputLookStick(const FInputActionValue& Value)
{
	StickInput = Value.Get<FVector2D>(); // consumed every frame in UpdateLocalView
}

void AHLCharacter::InputLookStickReleased(const FInputActionValue& Value)
{
	StickInput = FVector2D::ZeroVector;
}

void AHLCharacter::InputFirePressed(const FInputActionValue& Value)
{
	if (Weapon && IsAlive())
	{
		Weapon->SetTriggerHeld(true);
	}
}

void AHLCharacter::InputFireReleased(const FInputActionValue& Value)
{
	if (Weapon)
	{
		Weapon->SetTriggerHeld(false);
	}
}

void AHLCharacter::InputAdsPressed(const FInputActionValue& Value)
{
	bAds = true;
	if (!HasAuthority())
	{
		Server_SetAds(true);
	}
}

void AHLCharacter::InputAdsReleased(const FInputActionValue& Value)
{
	bAds = false;
	if (!HasAuthority())
	{
		Server_SetAds(false);
	}
}

void AHLCharacter::Server_SetAds_Implementation(bool bNewAds)
{
	bAds = bNewAds;
}

void AHLCharacter::InputReload(const FInputActionValue& Value)
{
	if (Weapon && IsAlive())
	{
		Weapon->RequestReload();
	}
}

void AHLCharacter::InputJump(const FInputActionValue& Value)
{
	if (IsAlive())
	{
		Jump();
	}
}

void AHLCharacter::InputNextWeapon(const FInputActionValue& Value)
{
	if (Weapon)
	{
		Weapon->CycleWeapon();
	}
}

void AHLCharacter::InputWeaponSlot(int32 Slot)
{
	if (Weapon)
	{
		Weapon->SelectWeapon(Slot);
	}
}

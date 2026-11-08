// Hitreg Lab: launcher projectile
#include "Weapons/HLProjectile.h"
#include "Core/HLCharacter.h"
#include "Weapons/HLWeaponDefinition.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AHLProjectile::AHLProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(8.f);
	Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.16f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->InitialSpeed = 4000.f;
	Movement->MaxSpeed = 4000.f;
	Movement->ProjectileGravityScale = 0.6f;
	Movement->bRotationFollowsVelocity = true;

	InitialLifeSpan = 6.f;
}

void AHLProjectile::Init(const UHLWeaponDefinition* Def)
{
	if (Def)
	{
		Damage = Def->Damage;
		SplashRadius = Def->SplashRadius;
		WeaponName = Def->DisplayName.ToString();
		Movement->ProjectileGravityScale = Def->ProjectileGravityScale;
		Movement->Velocity = GetActorForwardVector() * Def->ProjectileSpeed;
	}
	if (APawn* Inst = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Inst, true);
	}
	if (HasAuthority())
	{
		Collision->OnComponentHit.AddDynamic(this, &AHLProjectile::OnHit);
	}
}

void AHLProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bExploded || !HasAuthority())
	{
		return;
	}
	bExploded = true;
	const FVector Where = Hit.ImpactPoint;
	AController* InstController = GetInstigatorController();
	// Splash: full damage at the centre, falling off linearly, measured to each character's torso
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		AHLCharacter* Char = *It;
		if (!Char->IsAlive())
		{
			continue;
		}
		const float Dist = FVector::Dist(Char->GetActorLocation() + FVector(0, 0, 22.f), Where);
		if (Dist < SplashRadius)
		{
			const float Scale = 1.f - Dist / SplashRadius;
			const bool bSelf = Char == GetInstigator();
			Char->ServerApplyDamage(Damage * Scale * (bSelf ? 0.5f : 1.f), InstController, EHLHitZone::Torso, WeaponName);
		}
	}
	Multicast_Explode(Where, SplashRadius);
	SetLifeSpan(0.1f);
	Mesh->SetVisibility(false);
	Movement->StopMovementImmediately();
}

void AHLProjectile::Multicast_Explode_Implementation(FVector_NetQuantize Where, float Radius)
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		DrawDebugSphere(GetWorld(), Where, Radius, 20, FColor(255, 140, 40), false, 0.4f, 0, 1.5f);
	}
}

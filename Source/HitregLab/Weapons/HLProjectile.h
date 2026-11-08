// Hitreg Lab: launcher projectile. Server-authoritative and replicated; deliberately NOT lag compensated
// (projectiles travel in server time), which the README explains.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UHLWeaponDefinition;

UCLASS()
class HITREGLAB_API AHLProjectile : public AActor
{
	GENERATED_BODY()

public:
	AHLProjectile();

	void Init(const UHLWeaponDefinition* Def);

protected:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_Explode(FVector_NetQuantize Where, float Radius);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

	float Damage = 90.f;
	float SplashRadius = 250.f;
	FString WeaponName = TEXT("Launcher");
	bool bExploded = false;
};

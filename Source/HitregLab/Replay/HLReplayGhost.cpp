// Hitreg Lab: replay ghost
#include "Replay/HLReplayGhost.h"
#include "Core/HLCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AHLReplayGhost::AHLReplayGhost()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	int32 Index = 0;
	for (const FHLHitbox& Box : AHLCharacter::MakeDefaultHitboxes())
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Part_%d"), Index++));
		Part->SetupAttachment(Root);
		const bool bSphere = Box.Shape == EHLHitboxShape::Sphere;
		Part->SetStaticMesh(bSphere ? SphereMesh.Object : CubeMesh.Object);
		Part->SetRelativeLocation(Box.Center);
		Part->SetRelativeScale3D(bSphere ? FVector(Box.Extent.X * 2.f / 100.f) : Box.Extent * 2.f / 100.f);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Parts.Add(Part);
	}
	UStaticMeshComponent* Visor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visor"));
	Visor->SetupAttachment(Root);
	Visor->SetStaticMesh(CubeMesh.Object);
	Visor->SetRelativeLocation(FVector(12.f, 0.f, 68.f));
	Visor->SetRelativeScale3D(FVector(0.1f, 0.26f, 0.08f));
	Visor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Parts.Add(Visor);

	AimPivot = CreateDefaultSubobject<USceneComponent>(TEXT("AimPivot"));
	AimPivot->SetupAttachment(Root);
	AimPivot->SetRelativeLocation(FVector(0.f, 22.f, 40.f));
	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(AimPivot);
	Gun->SetStaticMesh(CubeMesh.Object);
	Gun->SetRelativeLocation(FVector(26.f, 0.f, 0.f));
	Gun->SetRelativeScale3D(FVector(0.55f, 0.07f, 0.1f));
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(0.f, 0.f, AHLCharacter::EyeHeight));
	Camera->SetFieldOfView(90.f);

	ViewModel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ViewModel"));
	ViewModel->SetupAttachment(Camera);
	ViewModel->SetStaticMesh(CubeMesh.Object);
	ViewModel->SetRelativeLocation(FVector(32.f, 14.f, -16.f));
	ViewModel->SetRelativeScale3D(FVector(0.5f, 0.07f, 0.1f));
	ViewModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ViewModel->SetCastShadow(false);
	ViewModel->SetVisibility(false);
}

void AHLReplayGhost::SetColor(const FLinearColor& Color)
{
	static const FName ColorParam(TEXT("Color"));
	for (int32 i = 0; i < Parts.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* M = Parts[i]->CreateDynamicMaterialInstance(0))
		{
			M->SetVectorParameterValue(ColorParam, i < Parts.Num() - 1 ? Color : FLinearColor(0.05f, 0.05f, 0.06f));
		}
	}
	for (UStaticMeshComponent* Dark : { Gun.Get(), ViewModel.Get() })
	{
		if (UMaterialInstanceDynamic* M = Dark->CreateDynamicMaterialInstance(0))
		{
			M->SetVectorParameterValue(ColorParam, FLinearColor(0.06f, 0.06f, 0.07f));
		}
	}
}

void AHLReplayGhost::ApplyPose(const FHLPose& Pose, bool bAlive)
{
	SetActorLocationAndRotation(Pose.Location, FRotator(0.f, Pose.Yaw, 0.f));
	AimPivot->SetRelativeRotation(FRotator(Pose.Pitch, 0.f, 0.f));
	Camera->SetRelativeRotation(FRotator(Pose.Pitch, 0.f, 0.f));
	SetActorHiddenInGame(!bAlive);
}

void AHLReplayGhost::SetFirstPerson(bool bFirstPerson)
{
	for (UStaticMeshComponent* Part : Parts)
	{
		Part->SetVisibility(!bFirstPerson);
	}
	Gun->SetVisibility(!bFirstPerson);
	ViewModel->SetVisibility(bFirstPerson);
}

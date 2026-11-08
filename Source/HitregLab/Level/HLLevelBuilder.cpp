// Hitreg Lab: level builder
#include "Level/HLLevelBuilder.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor Floor(0.12f, 0.13f, 0.15f);
	const FLinearColor FloorLine(0.55f, 0.57f, 0.6f);
	const FLinearColor Wall(0.3f, 0.32f, 0.35f);
	const FLinearColor Cover(0.16f, 0.22f, 0.32f);
	const FLinearColor Accent(0.95f, 0.55f, 0.2f);
	constexpr float SpawnZ = 100.f; // capsule half height + a little
}

AHLLevelBuilder::AHLLevelBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = Cube.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ColorMat(TEXT("/Game/HitregLab/Materials/M_HLColor.M_HLColor"));
	ColorMaterial = ColorMat.Object;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetRelativeRotation(FRotator(-48.f, 35.f, 0.f));
	Sun->SetIntensity(7.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetMobility(EComponentMobility::Movable);

	Sky = CreateDefaultSubobject<USkyLightComponent>(TEXT("Sky"));
	Sky->SetupAttachment(Root);
	Sky->bRealTimeCapture = true;
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->SetIntensity(1.2f);

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.004f);
}

void AHLLevelBuilder::Clear()
{
	for (UStaticMeshComponent* C : Built)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	Built.Reset();
	PlayerSpawns.Reset();
	BotSpots.Reset();
	Peek = FHLPeekSetup();
}

UStaticMeshComponent* AHLLevelBuilder::AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
	C->SetStaticMesh(CubeMesh);
	if (ColorMaterial)
	{
		C->SetMaterial(0, ColorMaterial);
	}
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(Center);
	C->SetRelativeScale3D(Size / 100.f);
	C->SetCollisionProfileName(TEXT("BlockAll"));
	C->SetMobility(EComponentMobility::Static);
	C->RegisterComponent();
	if (UMaterialInstanceDynamic* M = C->CreateDynamicMaterialInstance(0))
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
	}
	Built.Add(C);
	return C;
}

void AHLLevelBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AHLLevelBuilder::BeginPlay()
{
	Super::BeginPlay();
	if (Built.Num() == 0)
	{
		Rebuild();
	}
}

void AHLLevelBuilder::Rebuild()
{
	Clear();
	switch (Layout)
	{
	case EHLLayout::Range: BuildRange(); break;
	case EHLLayout::Peek: BuildPeek(); break;
	case EHLLayout::Arena: BuildArena(); break;
	}
}

void AHLLevelBuilder::BuildRange()
{
	// A 60 m lane: you at x = 0, strafing dummies at 10, 25 and 50 m
	AddBox(FVector(2700.f, 0.f, -10.f), FVector(6000.f, 2600.f, 20.f), Floor);
	AddBox(FVector(5700.f, 0.f, 300.f), FVector(40.f, 2600.f, 600.f), Wall);
	AddBox(FVector(2700.f, 1300.f, 60.f), FVector(6000.f, 40.f, 120.f), Wall);
	AddBox(FVector(2700.f, -1300.f, 60.f), FVector(6000.f, 40.f, 120.f), Wall);
	AddBox(FVector(-320.f, 0.f, 150.f), FVector(40.f, 2600.f, 300.f), Wall);
	for (const float X : { 1000.f, 2500.f, 5000.f })
	{
		AddBox(FVector(X, 0.f, 0.5f), FVector(16.f, 2400.f, 1.f), FloorLine);
		AddBox(FVector(X, -1250.f, 120.f), FVector(30.f, 30.f, 240.f), Accent);
		AddBox(FVector(X, 1250.f, 120.f), FVector(30.f, 30.f, 240.f), Accent);
	}
	PlayerSpawns.Add(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, SpawnZ)));
	const float Lanes[3][2] = { { 1000.f, 0.f }, { 2500.f, -350.f }, { 5000.f, 350.f } };
	for (const auto& L : Lanes)
	{
		FHLBotSpot Spot;
		Spot.Mode = TEXT("strafe");
		Spot.Transform = FTransform(FRotator(0.f, 180.f, 0.f), FVector(L[0], L[1], SpawnZ));
		BotSpots.Add(Spot);
	}
}

void AHLLevelBuilder::BuildPeek()
{
	// A thin wall along X ending at the corner (0,0). The peeker starts south of it and strafes +X past the
	// corner; the holder stands north-east, aiming at the corner.
	AddBox(FVector(0.f, 0.f, -10.f), FVector(4000.f, 4000.f, 20.f), Floor);
	AddBox(FVector(-600.f, 0.f, 175.f), FVector(1200.f, 40.f, 350.f), Wall);
	AddBox(FVector(0.f, 0.f, 360.f), FVector(12.f, 44.f, 20.f), Accent); // corner marker
	AddBox(FVector(-250.f, -150.f, 0.5f), FVector(10.f, 10.f, 1.f), FloorLine);
	AddBox(FVector(350.f, -150.f, 0.5f), FVector(10.f, 10.f, 1.f), FloorLine);
	Peek.PeekerStart = FVector(-250.f, -150.f, SpawnZ);
	Peek.PeekerEnd = FVector(350.f, -150.f, SpawnZ);
	Peek.HolderSpot = FVector(300.f, 800.f, SpawnZ);
	Peek.Corner = FVector(0.f, 0.f, SpawnZ);
	PlayerSpawns.Add(FTransform(FRotator(0.f, 0.f, 0.f), Peek.PeekerStart));
	PlayerSpawns.Add(FTransform((Peek.Corner - Peek.HolderSpot).Rotation(), Peek.HolderSpot));
}

void AHLLevelBuilder::BuildArena()
{
	// 36 m square with waist-high and full-height cover
	AddBox(FVector(0.f, 0.f, -10.f), FVector(3600.f, 3600.f, 20.f), Floor);
	for (const FVector& W : { FVector(1800.f, 0.f, 0.f), FVector(-1800.f, 0.f, 0.f) })
	{
		AddBox(W + FVector(0.f, 0.f, 200.f), FVector(40.f, 3600.f, 400.f), Wall);
	}
	for (const FVector& W : { FVector(0.f, 1800.f, 0.f), FVector(0.f, -1800.f, 0.f) })
	{
		AddBox(W + FVector(0.f, 0.f, 200.f), FVector(3600.f, 40.f, 400.f), Wall);
	}
	AddBox(FVector(0.f, 0.f, 150.f), FVector(400.f, 400.f, 300.f), Cover);
	const FVector Low[] = { FVector(700.f, 700.f, 0.f), FVector(-700.f, -700.f, 0.f), FVector(700.f, -700.f, 0.f), FVector(-700.f, 700.f, 0.f) };
	for (const FVector& C : Low)
	{
		AddBox(C + FVector(0.f, 0.f, 55.f), FVector(240.f, 80.f, 110.f), Cover);
	}
	AddBox(FVector(0.f, 1150.f, 150.f), FVector(80.f, 500.f, 300.f), Cover);
	AddBox(FVector(0.f, -1150.f, 150.f), FVector(80.f, 500.f, 300.f), Cover);
	AddBox(FVector(1150.f, 0.f, 150.f), FVector(500.f, 80.f, 300.f), Cover);
	AddBox(FVector(-1150.f, 0.f, 150.f), FVector(500.f, 80.f, 300.f), Cover);
	for (const FVector& C : { FVector(-1500.f, -1500.f, 0.f), FVector(1500.f, 1500.f, 0.f), FVector(-1500.f, 1500.f, 0.f), FVector(1500.f, -1500.f, 0.f) })
	{
		AddBox(C + FVector(0.f, 0.f, 0.5f), FVector(120.f, 120.f, 1.f), Accent);
		PlayerSpawns.Add(FTransform((-C).Rotation(), C + FVector(0.f, 0.f, SpawnZ)));
	}
	for (const FVector& C : { FVector(1500.f, 1500.f, 0.f), FVector(1500.f, -1500.f, 0.f) })
	{
		FHLBotSpot Spot;
		Spot.Mode = TEXT("fight");
		Spot.Transform = FTransform((-C).Rotation(), C + FVector(0.f, 0.f, SpawnZ));
		BotSpots.Add(Spot);
	}
}

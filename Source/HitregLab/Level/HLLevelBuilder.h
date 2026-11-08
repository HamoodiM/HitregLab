// Hitreg Lab: builds a whole test level (geometry, lights, spawn spots) from a layout enum, in the
// construction script. Each map is just one of these actors, so layouts are reviewable as code.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HLLevelBuilder.generated.h"

class UStaticMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;

UENUM(BlueprintType)
enum class EHLLayout : uint8
{
	Range,
	Peek,
	Arena
};

USTRUCT(BlueprintType)
struct FHLBotSpot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FString Mode = TEXT("strafe");

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FTransform Transform;
};

/** Named spots the peek test uses. */
USTRUCT(BlueprintType)
struct FHLPeekSetup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector PeekerStart = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector PeekerEnd = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector HolderSpot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Corner = FVector::ZeroVector;
};

UCLASS()
class HITREGLAB_API AHLLevelBuilder : public AActor
{
	GENERATED_BODY()

public:
	AHLLevelBuilder();

	virtual void OnConstruction(const FTransform& Transform) override;
	/** Built geometry is transient (it's code), so a loaded level rebuilds it when play starts. */
	virtual void BeginPlay() override;
	void Rebuild();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	EHLLayout Layout = EHLLayout::Range;

	const TArray<FTransform>& GetPlayerSpawns() const { return PlayerSpawns; }
	const TArray<FHLBotSpot>& GetBotSpots() const { return BotSpots; }
	const FHLPeekSetup& GetPeekSetup() const { return Peek; }

private:
	void Clear();
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& Size, const FLinearColor& Color);
	void BuildRange();
	void BuildPeek();
	void BuildArena();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> Sky;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<class UPostProcessComponent> Post;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Built;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;

	UPROPERTY(VisibleAnywhere, Category = "Layout")
	TArray<FTransform> PlayerSpawns;

	UPROPERTY(VisibleAnywhere, Category = "Layout")
	TArray<FHLBotSpot> BotSpots;

	UPROPERTY(VisibleAnywhere, Category = "Layout")
	FHLPeekSetup Peek;
};

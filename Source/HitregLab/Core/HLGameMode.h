// Hitreg Lab: game mode. Spawns players and bots at the level builder's spots, hands out the loadout, respawns.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "HLGameMode.generated.h"

class AHLCharacter;
class AHLLevelBuilder;
class UHLWeaponDefinition;
class UHLAimAssistSettings;

UCLASS(Config = Game)
class HITREGLAB_API AHLGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHLGameMode();

	virtual void StartPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Weapons every character gets (data assets). Falls back to code presets when empty or missing. */
	UPROPERTY(Config, EditAnywhere, Category = "Lab")
	TArray<FSoftObjectPath> Loadout;

	UPROPERTY(Config, EditAnywhere, Category = "Lab")
	FSoftObjectPath AimAssistSettings;

	UPROPERTY(EditAnywhere, Category = "Lab")
	float RespawnDelay = 6.f;

	/** Resolved loadout (loaded assets, or transient presets). */
	const TArray<TObjectPtr<UHLWeaponDefinition>>& GetLoadout();
	UHLAimAssistSettings* GetAimAssistSettings();

	/** Called by a character's server code when it dies. */
	void OnCharacterKilled(AHLCharacter* Victim, AController* Killer, bool bHeadshot, const FString& WeaponName);

	/** Adds a server-controlled bot. Mode: "strafe" (target dummy), "fight", "peek", "hold". */
	AHLCharacter* SpawnBot(const FString& Mode, const FTransform& Where);

	AHLLevelBuilder* GetBuilder() const;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHLWeaponDefinition>> ResolvedLoadout;

	UPROPERTY(Transient)
	TObjectPtr<UHLAimAssistSettings> ResolvedAimAssist;

	int32 NextSpawnIndex = 0;
	int32 BotCounter = 0;
};

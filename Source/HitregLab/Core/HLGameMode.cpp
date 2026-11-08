// Hitreg Lab: game mode
#include "Core/HLGameMode.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameState.h"
#include "Core/HLHUD.h"
#include "Core/HLPlayerController.h"
#include "Core/HLPlayerState.h"
#include "Aim/HLAimAssistSettings.h"
#include "Bots/HLBotController.h"
#include "Level/HLLevelBuilder.h"
#include "Weapons/HLWeaponDefinition.h"
#include "HitregLab.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AHLGameMode::AHLGameMode()
{
	DefaultPawnClass = AHLCharacter::StaticClass();
	PlayerControllerClass = AHLPlayerController::StaticClass();
	PlayerStateClass = AHLPlayerState::StaticClass();
	GameStateClass = AHLGameState::StaticClass();
	HUDClass = AHLHUD::StaticClass();
	bUseSeamlessTravel = false;
}

AHLLevelBuilder* AHLGameMode::GetBuilder() const
{
	for (TActorIterator<AHLLevelBuilder> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void AHLGameMode::StartPlay()
{
	Super::StartPlay();

	if (AHLGameState* GS = GetGameState<AHLGameState>())
	{
		GS->ServerApplyNetSettings(GS->ServerTickRate, GS->SimulatedRttMs, GS->SimulatedJitterMs);
	}
	// Bots the level asks for (range dummies, arena opponents)
	if (const AHLLevelBuilder* Builder = GetBuilder())
	{
		for (const FHLBotSpot& Spot : Builder->GetBotSpots())
		{
			SpawnBot(Spot.Mode, Spot.Transform);
		}
	}
}

const TArray<TObjectPtr<UHLWeaponDefinition>>& AHLGameMode::GetLoadout()
{
	if (ResolvedLoadout.Num() == 0)
	{
		for (const FSoftObjectPath& Path : Loadout)
		{
			if (UHLWeaponDefinition* Def = Cast<UHLWeaponDefinition>(Path.TryLoad()))
			{
				ResolvedLoadout.Add(Def);
			}
		}
		if (ResolvedLoadout.Num() == 0)
		{
			UE_LOG(LogHitreg, Warning, TEXT("No weapon assets found; using code presets"));
			for (const TCHAR* Preset : { TEXT("Rifle"), TEXT("SMG"), TEXT("Marksman"), TEXT("Launcher") })
			{
				ResolvedLoadout.Add(UHLWeaponDefinition::MakeTransient(this, Preset));
			}
		}
	}
	return ResolvedLoadout;
}

UHLAimAssistSettings* AHLGameMode::GetAimAssistSettings()
{
	if (!ResolvedAimAssist)
	{
		ResolvedAimAssist = Cast<UHLAimAssistSettings>(AimAssistSettings.TryLoad());
		if (!ResolvedAimAssist)
		{
			ResolvedAimAssist = NewObject<UHLAimAssistSettings>(this);
		}
	}
	return ResolvedAimAssist;
}

AActor* AHLGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return Super::ChoosePlayerStart_Implementation(Player);
}

void AHLGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}
	AHLLevelBuilder* Builder = GetBuilder();
	if (Builder && Builder->GetPlayerSpawns().Num() > 0)
	{
		const TArray<FTransform>& Spawns = Builder->GetPlayerSpawns();
		const FTransform Where = Spawns[NextSpawnIndex++ % Spawns.Num()];
		RestartPlayerAtTransform(NewPlayer, Where);
		return;
	}
	Super::RestartPlayer(NewPlayer);
}

AHLCharacter* AHLGameMode::SpawnBot(const FString& Mode, const FTransform& Where)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AHLBotController* Bot = GetWorld()->SpawnActor<AHLBotController>(AHLBotController::StaticClass(), Where, Params);
	if (!Bot)
	{
		return nullptr;
	}
	Bot->Mode = Mode;
	Bot->HomeTransform = Where;
	if (Bot->PlayerState)
	{
		Bot->PlayerState->SetPlayerName(FString::Printf(TEXT("Bot %d"), ++BotCounter));
	}
	AHLCharacter* Pawn = GetWorld()->SpawnActor<AHLCharacter>(AHLCharacter::StaticClass(), Where, Params);
	if (Pawn)
	{
		Bot->Possess(Pawn);
	}
	return Pawn;
}

void AHLGameMode::OnCharacterKilled(AHLCharacter* Victim, AController* Killer, bool bHeadshot, const FString& WeaponName)
{
	if (AHLGameState* GS = GetGameState<AHLGameState>())
	{
		FHLKillFeedEntry Entry;
		Entry.Killer = (Killer && Killer->PlayerState) ? Killer->PlayerState->GetPlayerName() : TEXT("World");
		Entry.Victim = (Victim && Victim->GetPlayerState()) ? Victim->GetPlayerState()->GetPlayerName() : TEXT("?");
		Entry.Weapon = WeaponName;
		Entry.bHeadshot = bHeadshot;
		Entry.ServerTime = GetWorld()->GetTimeSeconds();
		GS->AddKill(Entry);
	}
	if (Killer && Killer->PlayerState)
	{
		Killer->PlayerState->SetScore(Killer->PlayerState->GetScore() + 1.f);
	}

	// Respawn after the kill cam has had time to play
	AController* VictimController = Victim ? Victim->GetController() : nullptr;
	if (!VictimController)
	{
		return;
	}
	TWeakObjectPtr<AController> WeakController = VictimController;
	TWeakObjectPtr<AHLCharacter> WeakVictim = Victim;
	const bool bIsBot = VictimController->IsA<AHLBotController>();
	const FTransform BotHome = bIsBot ? Cast<AHLBotController>(VictimController)->HomeTransform : FTransform::Identity;
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakController, WeakVictim, bIsBot, BotHome]()
	{
		AController* C = WeakController.Get();
		if (!C)
		{
			return;
		}
		if (AHLCharacter* Old = WeakVictim.Get())
		{
			C->UnPossess();
			Old->Destroy();
		}
		if (bIsBot)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (AHLCharacter* Pawn = GetWorld()->SpawnActor<AHLCharacter>(AHLCharacter::StaticClass(), BotHome, Params))
			{
				C->Possess(Pawn);
			}
		}
		else
		{
			RestartPlayer(C);
		}
	}), RespawnDelay, false);
}

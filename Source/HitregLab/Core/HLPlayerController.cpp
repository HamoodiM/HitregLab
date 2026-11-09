// Hitreg Lab: player controller
#include "Core/HLPlayerController.h"
#include "Aim/HLAimAssistComponent.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameMode.h"
#include "Core/HLGameState.h"
#include "Net/HLServerClockComponent.h"
#include "Replay/HLReplayComponent.h"
#include "Measure/HLPeekComponent.h"
#include "Measure/HLHitregBenchComponent.h"
#include "Measure/HLCoverComponent.h"
#include "Autopsy/HLAutopsyViewerComponent.h"
#include "Autopsy/HLShotAutopsySubsystem.h"
#include "Weapons/HLWeaponComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetDriver.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/Paths.h"

namespace
{
	const int32 RttPresets[] = { 0, 50, 100, 150, 250 };
	const int32 TickPresets[] = { 20, 30, 60, 128 };
	const TCHAR* PcAimModeNames[] = { TEXT("Off"), TEXT("Slowdown only"), TEXT("Full") };
}

AHLPlayerController::AHLPlayerController()
{
	Clock = CreateDefaultSubobject<UHLServerClockComponent>(TEXT("Clock"));
	Replay = CreateDefaultSubobject<UHLReplayComponent>(TEXT("Replay"));
	Peek = CreateDefaultSubobject<UHLPeekComponent>(TEXT("Peek"));
	HitregBench = CreateDefaultSubobject<UHLHitregBenchComponent>(TEXT("HitregBench"));
	Cover = CreateDefaultSubobject<UHLCoverComponent>(TEXT("Cover"));
	Autopsy = CreateDefaultSubobject<UHLAutopsyViewerComponent>(TEXT("Autopsy"));
}

const FHLInputActions& AHLPlayerController::GetActions()
{
	if (!Actions.Move)
	{
		CreateInput();
	}
	return Actions;
}

void AHLPlayerController::CreateInput()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	Actions.Move = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Actions.LookMouse = MakeAction(TEXT("IA_LookMouse"), EInputActionValueType::Axis2D);
	Actions.LookStick = MakeAction(TEXT("IA_LookStick"), EInputActionValueType::Axis2D);
	Actions.Fire = MakeAction(TEXT("IA_Fire"), EInputActionValueType::Boolean);
	Actions.Ads = MakeAction(TEXT("IA_Ads"), EInputActionValueType::Boolean);
	Actions.Reload = MakeAction(TEXT("IA_Reload"), EInputActionValueType::Boolean);
	Actions.Jump = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	Actions.NextWeapon = MakeAction(TEXT("IA_NextWeapon"), EInputActionValueType::Boolean);
	for (int32 i = 0; i < 4; ++i)
	{
		Actions.WeaponSlots.Add(MakeAction(*FString::Printf(TEXT("IA_Slot%d"), i + 1), EInputActionValueType::Boolean));
	}

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Lab"));
	// WASD -> one 2D move axis (X = right, Y = forward)
	Mapping->MapKey(Actions.Move, EKeys::D);
	Mapping->MapKey(Actions.Move, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(this));
	Mapping->MapKey(Actions.Move, EKeys::W).Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	{
		FEnhancedActionKeyMapping& S = Mapping->MapKey(Actions.Move, EKeys::S);
		S.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		S.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}
	Mapping->MapKey(Actions.Move, EKeys::Gamepad_Left2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Mapping->MapKey(Actions.LookMouse, EKeys::Mouse2D);
	// No engine dead zone on the look stick: the aim assist settings own the stick response
	Mapping->MapKey(Actions.LookStick, EKeys::Gamepad_Right2D);
	Mapping->MapKey(Actions.Fire, EKeys::LeftMouseButton);
	Mapping->MapKey(Actions.Fire, EKeys::Gamepad_RightTrigger);
	Mapping->MapKey(Actions.Ads, EKeys::RightMouseButton);
	Mapping->MapKey(Actions.Ads, EKeys::Gamepad_LeftTrigger);
	Mapping->MapKey(Actions.Reload, EKeys::R);
	Mapping->MapKey(Actions.Reload, EKeys::Gamepad_FaceButton_Left);
	Mapping->MapKey(Actions.Jump, EKeys::SpaceBar);
	Mapping->MapKey(Actions.Jump, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(Actions.NextWeapon, EKeys::MouseScrollUp);
	Mapping->MapKey(Actions.NextWeapon, EKeys::Gamepad_FaceButton_Top);
	const FKey Slots[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	for (int32 i = 0; i < 4; ++i)
	{
		Mapping->MapKey(Actions.WeaponSlots[i], Slots[i]);
	}
}

void AHLPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		GetActions();
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Sub->AddMappingContext(Mapping, 0);
		}
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void AHLPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
}

void AHLPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
}

bool AHLPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (Params.IsGamepad())
	{
		bUsingGamepad = true;
	}
	else if (Params.Key.IsMouseButton() || Params.Key == EKeys::MouseX || Params.Key == EKeys::MouseY || !Params.Key.IsAnalog())
	{
		bUsingGamepad = false;
	}
	return Super::InputKey(Params);
}

void AHLPlayerController::ShowToast(const FString& Message)
{
	Toast = Message;
	ToastTime = GetWorld()->GetTimeSeconds();
}

void AHLPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	ApplyClientPacketSim();

	AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	if (WasInputKeyJustPressed(EKeys::F1))
	{
		bShowNetOverlay = !bShowNetOverlay;
	}
	if (WasInputKeyJustPressed(EKeys::F2) && GS)
	{
		HLLagComp(GS->bLagCompensation ? 0 : 1);
	}
	if (WasInputKeyJustPressed(EKeys::F3) && GS)
	{
		int32 Index = 0;
		for (int32 i = 0; i < UE_ARRAY_COUNT(RttPresets); ++i)
		{
			if (RttPresets[i] == GS->SimulatedRttMs)
			{
				Index = i;
			}
		}
		HLRtt(RttPresets[(Index + 1) % UE_ARRAY_COUNT(RttPresets)], GS->SimulatedJitterMs);
	}
	if (WasInputKeyJustPressed(EKeys::F4) && GS)
	{
		int32 Index = 0;
		for (int32 i = 0; i < UE_ARRAY_COUNT(TickPresets); ++i)
		{
			if (TickPresets[i] == GS->ServerTickRate)
			{
				Index = i;
			}
		}
		HLTickRate(TickPresets[(Index + 1) % UE_ARRAY_COUNT(TickPresets)]);
	}
	if (WasInputKeyJustPressed(EKeys::F5))
	{
		HLInterp(InterpTicks % 3 + 1);
	}
	if (WasInputKeyJustPressed(EKeys::F6))
	{
		HLAimAssist((int32(UHLAimAssistComponent::GetLocalMode(this)) + 1) % 3);
	}
	if (WasInputKeyJustPressed(EKeys::F7))
	{
		UHLWeaponComponent::bDrawRewind = !UHLWeaponComponent::bDrawRewind;
		ShowToast(FString::Printf(TEXT("Rewind debug draw %s"), UHLWeaponComponent::bDrawRewind ? TEXT("on") : TEXT("off")));
	}
	if (WasInputKeyJustPressed(EKeys::F8) && Replay)
	{
		ShowToast(Replay->SaveLastKillCam());
	}
	if (WasInputKeyJustPressed(EKeys::F9))
	{
		HLAimBench(50);
	}
	if (WasInputKeyJustPressed(EKeys::F10))
	{
		HLAddBot(TEXT("fight"));
	}
	if (WasInputKeyJustPressed(EKeys::F11) && GS)
	{
		HLCoverRule(GS->bCoverRule ? 0 : 1, GS->CoverRuleBudgetMs);
	}
	if (WasInputKeyJustPressed(EKeys::Tab))
	{
		HLAutopsy();
	}
}

void AHLPlayerController::ApplyClientPacketSim()
{
#if DO_ENABLE_NET_TEST
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!GS || !Driver || GetNetMode() != NM_Client)
	{
		return;
	}
	if (GS->SimulatedRttMs != AppliedRtt || GS->SimulatedJitterMs != AppliedJitter)
	{
		// The client delays its outgoing half of the round trip; the server delays the other half
		FPacketSimulationSettings Sim;
		Sim.PktLag = GS->SimulatedRttMs / 2;
		Sim.PktLagVariance = GS->SimulatedJitterMs;
		Driver->SetPacketSimulationSettings(Sim);
		AppliedRtt = GS->SimulatedRttMs;
		AppliedJitter = GS->SimulatedJitterMs;
	}
#endif
}

void AHLPlayerController::HLLagComp(int32 bEnable)
{
	Server_SetLagComp(bEnable != 0);
	ShowToast(FString::Printf(TEXT("Lag compensation %s"), bEnable ? TEXT("ON") : TEXT("OFF")));
}

void AHLPlayerController::Server_SetLagComp_Implementation(bool bEnable)
{
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->bLagCompensation = bEnable;
	}
}

void AHLPlayerController::HLTickRate(int32 Hz)
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	Server_SetNet(Hz, GS ? GS->SimulatedRttMs : 0, GS ? GS->SimulatedJitterMs : 0);
	ShowToast(FString::Printf(TEXT("Server tick rate %d Hz"), Hz));
}

void AHLPlayerController::HLRtt(int32 RttMs, int32 JitterMs)
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	Server_SetNet(GS ? GS->ServerTickRate : 60, RttMs, JitterMs);
	ShowToast(FString::Printf(TEXT("Simulated RTT %d ms (jitter %d)"), RttMs, JitterMs));
}

void AHLPlayerController::Server_SetNet_Implementation(int32 TickRate, int32 RttMs, int32 JitterMs)
{
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->ServerApplyNetSettings(TickRate, RttMs, JitterMs);
	}
}

void AHLPlayerController::HLInterp(int32 Ticks)
{
	InterpTicks = FMath::Clamp(Ticks, 1, 4);
	ShowToast(FString::Printf(TEXT("Interpolation delay: %d server ticks"), InterpTicks));
}

void AHLPlayerController::HLAimAssist(int32 Mode)
{
	const EHLAimAssistMode NewMode = EHLAimAssistMode(FMath::Clamp(Mode, 0, 2));
	UHLAimAssistComponent::SetLocalMode(NewMode);
	ShowToast(FString::Printf(TEXT("Aim assist: %s"), PcAimModeNames[int32(NewMode)]));
}

void AHLPlayerController::HLAimBench(int32 Trials)
{
	const AHLCharacter* Char = Cast<AHLCharacter>(GetPawn());
	UHLAimAssistSettings* Settings = Char && Char->AimAssist ? Char->AimAssist->GetSettings() : NewObject<UHLAimAssistSettings>(this);
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Measure");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Summary = HLAimBench::RunToCsv(*Settings, FMath::Clamp(Trials, 1, 1000), Dir);
	ShowToast(Summary);
	UE_LOG(LogTemp, Display, TEXT("[HitregLab] Aim bench: %s"), *Summary);
}

void AHLPlayerController::HLAddBot(const FString& Mode)
{
	Server_AddBot(Mode.IsEmpty() ? TEXT("fight") : Mode);
}

void AHLPlayerController::Server_AddBot_Implementation(const FString& Mode)
{
	AHLGameMode* GM = GetWorld()->GetAuthGameMode<AHLGameMode>();
	const APawn* Me = GetPawn();
	if (GM && Me)
	{
		const FVector Where = Me->GetActorLocation() + Me->GetActorForwardVector() * 1200.f;
		GM->SpawnBot(Mode, FTransform((Me->GetActorLocation() - Where).Rotation(), Where));
	}
}

void AHLPlayerController::HLPlayReplay(const FString& File)
{
	if (Replay)
	{
		ShowToast(Replay->LoadAndPlay(File));
	}
}

void AHLPlayerController::HLPeekSweep(int32 TrialsPerConfig, int32 Full)
{
	if (Peek)
	{
		Peek->Server_StartSweep(TrialsPerConfig, Full != 0);
	}
}

void AHLPlayerController::HLHitregBench(int32 ShotsPerConfig)
{
	if (HitregBench)
	{
		HitregBench->Start(ShotsPerConfig);
	}
}

void AHLPlayerController::HLSaveReplay()
{
	if (Replay)
	{
		ShowToast(Replay->SaveLastKillCam());
	}
}

void AHLPlayerController::HLCoverSweep(int32 TrialsPerConfig)
{
	if (Cover)
	{
		Cover->Server_StartSweep(TrialsPerConfig);
	}
}

void AHLPlayerController::HLCoverRule(int32 bEnable, float BudgetMs)
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	Server_SetRewindRule(GS ? GS->MaxRewindMs : 200.f, bEnable != 0, BudgetMs);
	ShowToast(bEnable ? FString::Printf(TEXT("Cover rule ON: a target already in cover is rewound at most %.0f ms"), BudgetMs)
		: FString(TEXT("Cover rule OFF: classic favour-the-shooter rewind")));
}

void AHLPlayerController::HLMaxRewind(float Ms)
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	Server_SetRewindRule(Ms, GS ? GS->bCoverRule : false, GS ? GS->CoverRuleBudgetMs : 100.f);
	ShowToast(FString::Printf(TEXT("Rewind cap %.0f ms"), Ms));
}

void AHLPlayerController::Server_SetRewindRule_Implementation(float MaxRewindMs, bool bCoverRule, float CoverBudgetMs)
{
	if (AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>())
	{
		GS->MaxRewindMs = FMath::Clamp(MaxRewindMs, 0.f, 900.f); // history holds 1 s
		GS->bCoverRule = bCoverRule;
		GS->CoverRuleBudgetMs = FMath::Clamp(CoverBudgetMs, 0.f, 900.f);
	}
}

void AHLPlayerController::HLAutopsy()
{
	if (Autopsy)
	{
		Autopsy->Toggle();
	}
}

void AHLPlayerController::HLAutopsyLoad(const FString& File)
{
	if (Autopsy)
	{
		ShowToast(Autopsy->LoadFile(File));
	}
}

void AHLPlayerController::HLAutopsyCsv()
{
	if (GetNetMode() == NM_Client)
	{
		Server_AutopsyCsv(); // the full log lives on the server
		ShowToast(TEXT("Shot autopsy CSV written on the server (Saved/Measure)"));
	}
	else if (const UHLShotAutopsySubsystem* Log = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>())
	{
		ShowToast(Log->WriteCsv());
	}
}

void AHLPlayerController::Server_AutopsyCsv_Implementation()
{
	if (const UHLShotAutopsySubsystem* Log = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>())
	{
		UE_LOG(LogTemp, Display, TEXT("[HitregLab] Shot autopsy: %s"), *Log->WriteCsv());
	}
}

void AHLPlayerController::Client_AutopsyRecord_Implementation(const TArray<uint8>& Bytes)
{
	FHLShotAutopsy Record;
	UHLShotAutopsySubsystem* Log = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>();
	if (Log && Record.FromBytes(Bytes))
	{
		Log->AddReceived(Record);
	}
}

void AHLPlayerController::HLSens(float DegPerCount)
{
	MouseSensitivity = FMath::Clamp(DegPerCount, 0.005f, 1.f);
	ShowToast(FString::Printf(TEXT("Mouse sensitivity %.3f deg/count"), MouseSensitivity));
}

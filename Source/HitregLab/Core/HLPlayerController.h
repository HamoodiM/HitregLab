// Hitreg Lab: player controller. Builds the input actions in code, tracks mouse vs gamepad, owns the
// local lab settings (interpolation delay, sensitivity) and the debug keys / console commands.
//
// Keys: F1 net overlay, F2 lag compensation, F3 simulated RTT, F4 tick rate, F5 interpolation ticks,
//       F6 aim assist mode, F7 rewind debug draw, F8 save kill cam replay, F9 aim benchmark, F10 add bot,
//       F11 cover rule, Tab Shot Autopsy viewer.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HLPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UHLServerClockComponent;
class UHLReplayComponent;

USTRUCT()
struct FHLInputActions
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UInputAction> Move;
	UPROPERTY() TObjectPtr<UInputAction> LookMouse;
	UPROPERTY() TObjectPtr<UInputAction> LookStick;
	UPROPERTY() TObjectPtr<UInputAction> Fire;
	UPROPERTY() TObjectPtr<UInputAction> Ads;
	UPROPERTY() TObjectPtr<UInputAction> Reload;
	UPROPERTY() TObjectPtr<UInputAction> Jump;
	UPROPERTY() TObjectPtr<UInputAction> NextWeapon;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> WeaponSlots;
};

UCLASS()
class HITREGLAB_API AHLPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AHLPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;

	/** The input actions (created on first use, so the pawn can bind them whenever it is set up). */
	const FHLInputActions& GetActions();
	bool IsUsingGamepad() const { return bUsingGamepad; }

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<UHLServerClockComponent> Clock;

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<UHLReplayComponent> Replay;

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<class UHLPeekComponent> Peek;

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<class UHLHitregBenchComponent> HitregBench;

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<class UHLCoverComponent> Cover;

	UPROPERTY(VisibleAnywhere, Category = "Lab")
	TObjectPtr<class UHLAutopsyViewerComponent> Autopsy;

	/** Degrees per mouse count. */
	UPROPERTY(EditAnywhere, Category = "Lab")
	float MouseSensitivity = 0.07f;

	/** Remote players are drawn this many server ticks in the past (the interpolation delay). */
	UPROPERTY(EditAnywhere, Category = "Lab")
	int32 InterpTicks = 2;

	bool bShowNetOverlay = true;

	/** Short message the HUD shows for a few seconds. */
	FString Toast;
	double ToastTime = -10.0;
	void ShowToast(const FString& Message);

	// --- Console commands (type in the ~ console) ---
	UFUNCTION(Exec) void HLLagComp(int32 bEnable);
	UFUNCTION(Exec) void HLTickRate(int32 Hz);
	UFUNCTION(Exec) void HLRtt(int32 RttMs, int32 JitterMs = 0);
	UFUNCTION(Exec) void HLInterp(int32 Ticks);
	UFUNCTION(Exec) void HLAimAssist(int32 Mode);
	UFUNCTION(Exec) void HLAimBench(int32 Trials = 50);
	UFUNCTION(Exec) void HLAddBot(const FString& Mode);
	UFUNCTION(Exec) void HLSens(float DegPerCount);
	UFUNCTION(Exec) void HLPlayReplay(const FString& File);
	/** Lag compensation benchmark: perfect aim at the drawn target, hit rate per RTT with compensation on/off. */
	UFUNCTION(Exec) void HLHitregBench(int32 ShotsPerConfig = 40);
	/** Peeker's-advantage sweep (needs 2 connected players). Full=1 runs the whole RTT x tick grid. */
	UFUNCTION(Exec) void HLPeekSweep(int32 TrialsPerConfig = 10, int32 Full = 1);
	UFUNCTION(Exec) void HLSaveReplay();
	/** Behind-cover deaths sweep (needs 2 connected players on Lvl_Peek). */
	UFUNCTION(Exec) void HLCoverSweep(int32 TrialsPerConfig = 12);
	/** Cover rule on/off; BudgetMs = how far back a target already in cover may still be rewound. */
	UFUNCTION(Exec) void HLCoverRule(int32 bEnable, float BudgetMs = 100.f);
	UFUNCTION(Exec) void HLMaxRewind(float Ms);
	/** Shot Autopsy: open/close the viewer, load the records saved in a .hlreplay, write this session's CSV. */
	UFUNCTION(Exec) void HLAutopsy();
	UFUNCTION(Exec) void HLAutopsyLoad(const FString& File);
	UFUNCTION(Exec) void HLAutopsyCsv();

	UFUNCTION(Server, Reliable)
	void Server_SetLagComp(bool bEnable);

	UFUNCTION(Server, Reliable)
	void Server_SetNet(int32 TickRate, int32 RttMs, int32 JitterMs);

	UFUNCTION(Server, Reliable)
	void Server_AddBot(const FString& Mode);

	UFUNCTION(Server, Reliable)
	void Server_SetRewindRule(float MaxRewindMs, bool bCoverRule, float CoverBudgetMs);

	UFUNCTION(Server, Reliable)
	void Server_AutopsyCsv();

	/** Server -> the two players involved in a shot: its finished Shot Autopsy record (serialized). */
	UFUNCTION(Client, Reliable)
	void Client_AutopsyRecord(const TArray<uint8>& Bytes);

private:
	void CreateInput();
	void ApplyClientPacketSim();

	UPROPERTY(Transient)
	FHLInputActions Actions;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> Mapping;

	bool bUsingGamepad = false;
	int32 AppliedRtt = -1;
	int32 AppliedJitter = -1;
};

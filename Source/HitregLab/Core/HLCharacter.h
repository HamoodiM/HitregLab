// Hitreg Lab: the player/bot character. A yaw-only rigid body built from primitives whose meshes match its
// hitboxes exactly (what you see is what you hit). Remote characters are drawn from interpolated server
// snapshots at a known delay, so the server can rewind to exactly what a shooter saw.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "Core/HLTypes.h"
#include "Net/HLPoseHistory.h"
#include "HLCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UHLWeaponComponent;
class UHLAimAssistComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FHLOnCharacterDied, class AHLCharacter*);

UCLASS()
class HITREGLAB_API AHLCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AHLCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual FRotator GetViewRotation() const override;
	virtual void OnRep_ReplicatedMovement() override;
	virtual void PostNetReceiveLocationAndRotation() override;

	// --- Components ---

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UHLWeaponComponent> Weapon;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UHLAimAssistComponent> AimAssist;

	/** Third-person body (hidden from the owner): one mesh per hitbox, plus a visor and a gun on an aim pivot. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TArray<TObjectPtr<UStaticMeshComponent>> BodyParts;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> AimPivot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ThirdPersonGun;

	/** First-person gun (owner only), attached to the camera. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> ViewModelRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ViewModelBody;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ViewModelBarrel;

	// --- Hitboxes ---

	UPROPERTY(EditAnywhere, Category = "Hitboxes")
	TArray<FHLHitbox> Hitboxes;

	static TArray<FHLHitbox> MakeDefaultHitboxes();

	/** Eye height above the actor origin (capsule centre). */
	static constexpr float EyeHeight = 64.f;

	// --- State ---

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Health")
	float Health = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "Health")
	bool bDead = false;

	/** Who killed us and when (server time); drives the kill cam on the victim's client. */
	UPROPERTY(Replicated)
	TObjectPtr<APlayerState> KilledBy;

	UPROPERTY(Replicated)
	double DeathServerTime = 0.0;

	UPROPERTY(Replicated)
	bool bKilledByHeadshot = false;

	FHLOnCharacterDied OnDied;

	bool IsAlive() const { return !bDead; }
	bool IsAiming() const { return bAds; }
	float GetAdsAlpha() const { return AdsAlpha; }

	/** Current pose (what this machine is displaying for this character). */
	FHLPose GetPose() const;
	FVector GetEyeLocation() const;
	int32 GetHLPlayerId() const;
	FLinearColor GetBodyColor() const { return BodyColor; }

	/** 0..1 horizontal speed relative to max walk speed (spread). */
	float GetSpeedAlpha() const;

	/** Server: apply a hit. Returns true if this killed us. */
	bool ServerApplyDamage(float Damage, AController* InstigatorController, EHLHitZone Zone, const FString& WeaponName);

	/** Local: the hit-marker / recoil hooks the weapon calls. */
	void AddRecoil(const FVector2D& KickDeg);
	void KickViewModel(float Amount);

	/** The render time this client uses for simulated proxies (server clock - interpolation delay). */
	static double GetRemoteRenderTime(const UObject* WorldContext);

	/** Client: the interpolation buffer for this (remote) character, for the kill cam recorder. */
	const FHLPoseHistory& GetInterpBuffer() const { return InterpBuffer; }

	/** Bots: steer the aim directly (no input). */
	void SetBotInputs(const FVector2D& Move, bool bFire, bool bWantAds);

	/** Body colour from the player id, so each player is recognisable in the arena and in replays. */
	static FLinearColor ColorForPlayer(int32 PlayerId);

protected:
	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Snapshot();

	UFUNCTION(Server, Reliable)
	void Server_SetAds(bool bNewAds);

	// Input
	void InputMove(const FInputActionValue& Value);
	void InputMoveReleased(const FInputActionValue& Value);
	void InputLookMouse(const FInputActionValue& Value);
	void InputLookStick(const FInputActionValue& Value);
	void InputLookStickReleased(const FInputActionValue& Value);
	void InputFirePressed(const FInputActionValue& Value);
	void InputFireReleased(const FInputActionValue& Value);
	void InputAdsPressed(const FInputActionValue& Value);
	void InputAdsReleased(const FInputActionValue& Value);
	void InputReload(const FInputActionValue& Value);
	void InputJump(const FInputActionValue& Value);
	void InputNextWeapon(const FInputActionValue& Value);
	void InputWeaponSlot(int32 Slot);
	void InputSlot1() { InputWeaponSlot(0); }
	void InputSlot2() { InputWeaponSlot(1); }
	void InputSlot3() { InputWeaponSlot(2); }
	void InputSlot4() { InputWeaponSlot(3); }

private:
	void BuildBody();
	void ApplyBodyColor();
	void UpdateSimulatedProxy(float DeltaSeconds);
	void UpdateLocalView(float DeltaSeconds);
	void SetBodyVisible(bool bVisible);

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FHLNetSnapshot Snapshot;

	UPROPERTY(Replicated)
	bool bAds = false;

	FHLPoseHistory InterpBuffer{64};
	float AdsAlpha = 0.f;
	FVector2D StickInput = FVector2D::ZeroVector;
	FVector2D MoveInput = FVector2D::ZeroVector;
	FVector2D PendingRecoil = FVector2D::ZeroVector;
	float RecoilRecoveryPitch = 0.f;
	double LastShotTime = -10.0;
	float ViewKick = 0.f;
	FLinearColor BodyColor = FLinearColor::White;
	float BaseFov = 90.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMaterials;
};

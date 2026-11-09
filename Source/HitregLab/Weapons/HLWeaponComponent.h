// Hitreg Lab: firing, ammo, spread, recoil and the client-predicted / server-validated shot pipeline.
//
// Local player fires -> spread + bullet magnetism -> predicted hit marker and tracer right away ->
// Server_Fire(origin, dir, view time) -> server validates (rate, origin, age) -> lag-compensated trace ->
// damage -> Client_ShotResult confirms or retracts the predicted marker.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/HLTypes.h"
#include "Weapons/HLWeaponDefinition.h"
#include "HLWeaponComponent.generated.h"

class AHLCharacter;

UENUM()
enum class EHLMarkerState : uint8
{
	None,
	Predicted,
	Confirmed,
	Rejected
};

struct FHLHitMarker
{
	double Time = -10.0;
	EHLMarkerState State = EHLMarkerState::None;
	EHLHitZone Zone = EHLHitZone::None;
	bool bKill = false;
	float Damage = 0.f;
};

/** Running counts for the HUD and the README: how often predictions matched the server. */
struct FHLShotStats
{
	int32 Fired = 0;
	int32 PredictedHits = 0;
	int32 ConfirmedHits = 0;
	int32 RetractedHits = 0;   // we showed a hit, the server said miss
	int32 SurpriseHits = 0;    // we showed nothing, the server said hit
	int32 Rejected = 0;
};

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Input (local player or bot on the server) ---
	void SetTriggerHeld(bool bHeld) { bTriggerHeld = bHeld; }
	void RequestReload();
	void SelectWeapon(int32 Index);
	void CycleWeapon();
	/**
	 * Measurement scripts: fire one shot right now, at the current view rotation, against the poses drawn this
	 * frame (call it after everything has moved, e.g. in TG_PostUpdateWork). Returns false if it can't fire.
	 */
	bool FireSingleNow();

	// --- Queries ---
	const UHLWeaponDefinition* GetCurrentWeapon() const;
	int32 GetCurrentIndex() const { return CurrentIndex; }
	int32 GetAmmo() const;
	bool IsReloading() const;
	float GetReloadAlpha() const;
	float GetCurrentSpreadDeg() const;
	const FHLHitMarker& GetHitMarker() const { return Marker; }
	const FHLShotStats& GetStats() const { return Stats; }
	const TArray<TObjectPtr<UHLWeaponDefinition>>& GetLoadout() const { return Loadout; }
	/** Last server result for this player's shots (for the rewind debug draw). */
	const FHLShotResult& GetLastResult() const { return LastResult; }
	double GetLastResultTime() const { return LastResultTime; }

	/** Debug draw toggles (local). */
	static bool bDrawTraces;
	static bool bDrawRewind;
	static bool bDrawSpread;

	/** Builds the loadout the same way on every machine (config asset paths, else code presets). */
	static TArray<TObjectPtr<UHLWeaponDefinition>> LoadLoadout(UObject* Outer);

protected:
	UFUNCTION(Server, Reliable)
	void Server_Fire(const FHLShotRequest& Request);

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	UFUNCTION(Server, Reliable)
	void Server_SelectWeapon(uint8 Index);

	UFUNCTION(Client, Reliable)
	void Client_ShotResult(const FHLShotResult& Result);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_ShotFX(FVector_NetQuantize Start, FVector_NetQuantize End, bool bHitCharacter);

	UFUNCTION()
	void OnRep_Ammo();

private:
	AHLCharacter* GetCharacter() const;
	bool IsLocallyDriven() const;
	void FireShot(double ShotTime);
	FHLShotResult ServerProcessShot(const FHLShotRequest& Request, bool bValidate);
	bool ServerValidate(const FHLShotRequest& Request, FName& OutReason);
	bool PredictHit(const FVector& Origin, const FVector& Dir, float Range, FVector& OutEnd, EHLHitZone& OutZone) const;
	void ApplyResult(const FHLShotResult& Result);
	void DrawTracer(const FVector& Start, const FVector& End, const FColor& Color, float Life) const;
	void RecordShotForReplay(const FVector& Start, const FVector& End, bool bHit) const;
	void FinishReload();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHLWeaponDefinition>> Loadout;

	UPROPERTY(Replicated)
	int32 CurrentIndex = 0;

	/** Server-authoritative ammo per weapon (owner only). Clients run a predicted copy. */
	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	TArray<int32> Ammo;

	TArray<int32> PredictedAmmo;

	UPROPERTY(Replicated)
	double ReloadEndTime = 0.0;

	double LocalReloadEnd = 0.0;

	bool bTriggerHeld = false;
	FHLFireClock FireClock;
	FHLSpreadModel Spread;
	int32 ConsecutiveShots = 0;
	uint16 NextShotSeq = 1;
	TMap<uint16, bool> PendingPredictions; // seq -> predicted hit?
	FHLHitMarker Marker;
	FHLShotStats Stats;
	FHLShotResult LastResult;
	double LastResultTime = -10.0;

	// Server-side rate limiting
	FHLRateLimiter RateLimiter;
};

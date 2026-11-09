// Hitreg Lab: Shot Autopsy log. The server judges every shot and records the verdict with its reason
// (UHLWeaponComponent::ServerProcessShot calls ServerRecord). A moment later, once the target's future
// positions are in the lag compensation history, the record gets a short track of the target and is sent to
// the shooter's and the target's clients, so each player can open "the shots I fired and the shots that hit
// me" in the viewer (Tab). The whole session is written to Saved/Measure/autopsy_*.csv when the world ends.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Autopsy/HLShotAutopsy.h"
#include "HLShotAutopsySubsystem.generated.h"

class AHLCharacter;
struct FHLShotRequest;
struct FHLTraceHit;

DECLARE_MULTICAST_DELEGATE_OneParam(FHLOnShotJudged, const FHLShotAutopsy&);

UCLASS()
class HITREGLAB_API UHLShotAutopsySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHLShotAutopsySubsystem, STATGROUP_Tickables); }

	/**
	 * Server: one judged shot. Trace is null for rejected and projectile shots. Broadcasts OnShotJudged right
	 * away (no track yet); the finished record is stored and sent to the clients a few hundred ms later.
	 */
	void ServerRecord(const AHLCharacter* Shooter, const FHLShotRequest& Request, double ViewTime, FName RejectReason, bool bProjectile,
		const FHLTraceHit* Trace, bool bServerHit, float Damage, bool bKill);

	/** Client: a finished record the server sent us. */
	void AddReceived(const FHLShotAutopsy& Record);

	/** Everything this machine knows about, oldest first (server: every shot; client: its own shots and shots at it). */
	const TArray<FHLShotAutopsy>& GetRecords() const { return Records; }
	/** Changes whenever a record is added (the viewer refreshes on it). */
	int32 GetRevision() const { return Revision; }

	/** Writes autopsy_<stamp>_shots.csv and autopsy_<stamp>_summary.csv; returns a status line. */
	FString WriteCsv() const;

	FHLOnShotJudged OnShotJudged;

	/** Records kept in memory per session (oldest dropped first). */
	static constexpr int32 MaxRecords = 50000;

private:
	struct FPending
	{
		FHLShotAutopsy Record;
		TWeakObjectPtr<AHLCharacter> Target;
		TWeakObjectPtr<AHLCharacter> Shooter;
		double FinalizeAt = 0.0;
	};

	void Finalize(FPending& P);
	void Store(const FHLShotAutopsy& Record);
	static float EstimateRttMs(const AHLCharacter* Character);

	TArray<FPending> Pending;
	TArray<FHLShotAutopsy> Records;
	uint32 NextShotId = 1;
	int32 Revision = 0;
};

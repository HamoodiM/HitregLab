// Hitreg Lab: weapon component
#include "Weapons/HLWeaponComponent.h"
#include "Aim/HLAimAssistComponent.h"
#include "Autopsy/HLShotAutopsySubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameMode.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Net/HLHitboxMath.h"
#include "Net/HLLagCompensationSubsystem.h"
#include "Net/HLServerClockComponent.h"
#include "Weapons/HLProjectile.h"
#include "Replay/HLReplayComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "HitregLab.h"
#include "Net/UnrealNetwork.h"

bool UHLWeaponComponent::bDrawTraces = true;
bool UHLWeaponComponent::bDrawRewind = true;
bool UHLWeaponComponent::bDrawSpread = false;

UHLWeaponComponent::UHLWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UHLWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHLWeaponComponent, CurrentIndex);
	DOREPLIFETIME_CONDITION(UHLWeaponComponent, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UHLWeaponComponent, ReloadEndTime, COND_OwnerOnly);
}

TArray<TObjectPtr<UHLWeaponDefinition>> UHLWeaponComponent::LoadLoadout(UObject* Outer)
{
	TArray<TObjectPtr<UHLWeaponDefinition>> Out;
	for (const FSoftObjectPath& Path : GetDefault<AHLGameMode>()->Loadout)
	{
		if (UHLWeaponDefinition* Def = Cast<UHLWeaponDefinition>(Path.TryLoad()))
		{
			Out.Add(Def);
		}
	}
	if (Out.Num() == 0)
	{
		for (const TCHAR* Preset : { TEXT("Rifle"), TEXT("SMG"), TEXT("Marksman"), TEXT("Launcher") })
		{
			Out.Add(UHLWeaponDefinition::MakeTransient(Outer, Preset));
		}
	}
	return Out;
}

void UHLWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	Loadout = LoadLoadout(this);
	if (GetOwnerRole() == ROLE_Authority)
	{
		Ammo.SetNum(Loadout.Num());
		for (int32 i = 0; i < Loadout.Num(); ++i)
		{
			Ammo[i] = Loadout[i]->MagazineSize;
		}
	}
	PredictedAmmo.SetNum(Loadout.Num());
	for (int32 i = 0; i < Loadout.Num(); ++i)
	{
		PredictedAmmo[i] = Loadout[i]->MagazineSize;
	}
}

AHLCharacter* UHLWeaponComponent::GetCharacter() const
{
	return Cast<AHLCharacter>(GetOwner());
}

bool UHLWeaponComponent::IsLocallyDriven() const
{
	const AHLCharacter* Char = GetCharacter();
	if (!Char)
	{
		return false;
	}
	// The local human, or a bot on the server
	return Char->IsLocallyControlled() || (Char->HasAuthority() && Char->GetController() && !Char->IsPlayerControlled());
}

const UHLWeaponDefinition* UHLWeaponComponent::GetCurrentWeapon() const
{
	return Loadout.IsValidIndex(CurrentIndex) ? Loadout[CurrentIndex].Get() : nullptr;
}

int32 UHLWeaponComponent::GetAmmo() const
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		return Ammo.IsValidIndex(CurrentIndex) ? Ammo[CurrentIndex] : 0;
	}
	return PredictedAmmo.IsValidIndex(CurrentIndex) ? PredictedAmmo[CurrentIndex] : 0;
}

bool UHLWeaponComponent::IsReloading() const
{
	const double Now = GetWorld()->GetTimeSeconds();
	return GetOwnerRole() == ROLE_Authority ? Now < ReloadEndTime : Now < LocalReloadEnd;
}

float UHLWeaponComponent::GetReloadAlpha() const
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	if (!Def || !IsReloading())
	{
		return 0.f;
	}
	const double End = GetOwnerRole() == ROLE_Authority ? ReloadEndTime : LocalReloadEnd;
	return 1.f - float((End - GetWorld()->GetTimeSeconds()) / FMath::Max(Def->ReloadTime, 0.01f));
}

float UHLWeaponComponent::GetCurrentSpreadDeg() const
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	const AHLCharacter* Char = GetCharacter();
	if (!Def || !Char)
	{
		return 0.f;
	}
	return Spread.Current(*Def, Char->GetSpeedAlpha(), Char->GetCharacterMovement()->IsFalling(), Char->IsAiming());
}

void UHLWeaponComponent::OnRep_Ammo()
{
	// Trust the server whenever we're not mid-burst (mid-burst its value lags our predicted one)
	if (!bTriggerHeld && Ammo.Num() == PredictedAmmo.Num())
	{
		PredictedAmmo = Ammo;
	}
}

void UHLWeaponComponent::RequestReload()
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	if (!Def || IsReloading() || GetAmmo() >= Def->MagazineSize)
	{
		return;
	}
	LocalReloadEnd = GetWorld()->GetTimeSeconds() + Def->ReloadTime;
	if (GetOwnerRole() == ROLE_Authority)
	{
		Server_Reload_Implementation();
	}
	else
	{
		Server_Reload();
	}
}

void UHLWeaponComponent::Server_Reload_Implementation()
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	if (!Def || GetWorld()->GetTimeSeconds() < ReloadEndTime)
	{
		return;
	}
	ReloadEndTime = GetWorld()->GetTimeSeconds() + Def->ReloadTime;
}

void UHLWeaponComponent::FinishReload()
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	if (!Def)
	{
		return;
	}
	if (GetOwnerRole() == ROLE_Authority && Ammo.IsValidIndex(CurrentIndex))
	{
		Ammo[CurrentIndex] = Def->MagazineSize;
	}
	if (PredictedAmmo.IsValidIndex(CurrentIndex))
	{
		PredictedAmmo[CurrentIndex] = Def->MagazineSize;
	}
}

void UHLWeaponComponent::SelectWeapon(int32 Index)
{
	if (!Loadout.IsValidIndex(Index) || Index == CurrentIndex)
	{
		return;
	}
	CurrentIndex = Index; // predicted locally, confirmed by replication
	ConsecutiveShots = 0;
	Spread.Bloom = 0.f;
	LocalReloadEnd = 0.0;
	if (GetOwnerRole() == ROLE_Authority)
	{
		ReloadEndTime = 0.0;
	}
	else
	{
		Server_SelectWeapon(uint8(Index));
	}
}

bool UHLWeaponComponent::FireSingleNow()
{
	const AHLCharacter* Char = GetCharacter();
	if (!GetCurrentWeapon() || !Char || !Char->IsAlive() || !IsLocallyDriven() || IsReloading() || GetAmmo() <= 0)
	{
		return false;
	}
	FireShot(GetWorld()->GetTimeSeconds());
	return true;
}

void UHLWeaponComponent::CycleWeapon()
{
	if (Loadout.Num() > 0)
	{
		SelectWeapon((CurrentIndex + 1) % Loadout.Num());
	}
}

void UHLWeaponComponent::Server_SelectWeapon_Implementation(uint8 Index)
{
	if (Loadout.IsValidIndex(Index))
	{
		CurrentIndex = Index;
		ReloadEndTime = 0.0;
	}
}

void UHLWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	AHLCharacter* Char = GetCharacter();
	if (!Def || !Char)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();

	// Reload completion (server and the predicting client each track their own timer)
	if (GetOwnerRole() == ROLE_Authority && ReloadEndTime > 0.0 && Now >= ReloadEndTime)
	{
		ReloadEndTime = 0.0;
		FinishReload();
	}
	if (GetOwnerRole() != ROLE_Authority && LocalReloadEnd > 0.0 && Now >= LocalReloadEnd)
	{
		LocalReloadEnd = 0.0;
		FinishReload();
	}

	if (!IsLocallyDriven() || !Char->IsAlive())
	{
		return;
	}

	Spread.Recover(*Def, DeltaTime);
	if (!bTriggerHeld)
	{
		ConsecutiveShots = 0;
	}

	const bool bCanFire = !IsReloading() && GetAmmo() > 0;
	TArray<double> ShotTimes;
	FireClock.Advance(Now, bTriggerHeld && bCanFire, Def->GetShotInterval(), Def->FireMode == EHLFireMode::Auto, ShotTimes);
	for (const double ShotTime : ShotTimes)
	{
		if (GetAmmo() <= 0)
		{
			break;
		}
		FireShot(ShotTime);
	}
	if (bTriggerHeld && GetAmmo() <= 0 && !IsReloading())
	{
		RequestReload(); // auto-reload on an empty trigger pull
	}
}

bool UHLWeaponComponent::PredictHit(const FVector& Origin, const FVector& Dir, float Range, FVector& OutEnd, EHLHitZone& OutZone) const
{
	// Same test the server runs, against the poses this client is displaying
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLPredict), true);
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	float Limit = Range;
	FHitResult WorldHit;
	if (World->LineTraceSingleByChannel(WorldHit, Origin, Origin + Dir * Range, ECC_Visibility, Params))
	{
		Limit = WorldHit.Distance;
	}
	OutEnd = Origin + Dir * Limit;
	OutZone = EHLHitZone::None;
	float Best = Limit;
	bool bHit = false;
	for (TActorIterator<AHLCharacter> It(World); It; ++It)
	{
		const AHLCharacter* Other = *It;
		if (Other == GetOwner() || !Other->IsAlive())
		{
			continue;
		}
		FHLHitboxHit Hit;
		if (HLHitboxMath::RayHitboxes(Origin, Dir, Best, Other->Hitboxes, Other->GetPose(), Hit))
		{
			Best = Hit.Distance;
			OutZone = Hit.Zone;
			OutEnd = Origin + Dir * Hit.Distance;
			bHit = true;
		}
	}
	return bHit;
}

void UHLWeaponComponent::FireShot(double ShotTime)
{
	const UHLWeaponDefinition* Def = GetCurrentWeapon();
	AHLCharacter* Char = GetCharacter();
	const bool bAuthority = GetOwnerRole() == ROLE_Authority;
	// Bots count as "locally controlled" on the server; cosmetics and hit markers are for humans only
	const bool bLocalHuman = Char->IsLocallyControlled() && Char->IsPlayerControlled();

	// Aim: camera forward, bent by bullet magnetism (gamepad + assist only), then spread
	const FVector Origin = Char->GetEyeLocation();
	FVector Dir = Char->GetViewRotation().Vector();
	const AHLPlayerController* PC = Cast<AHLPlayerController>(Char->GetController());
	if (PC && PC->IsUsingGamepad() && Char->AimAssist)
	{
		Dir = Char->AimAssist->BendShot(Origin, Dir);
	}
	const float SpreadDeg = GetCurrentSpreadDeg();
	const uint16 Seq = NextShotSeq++;
	const int32 Seed = int32(HashCombine(GetTypeHash(Char->GetHLPlayerId()), GetTypeHash(Seq)));
	Dir = FHLSpreadModel::SampleCone(Dir, SpreadDeg, Seed);

	// Local state: ammo, bloom, recoil, view kick
	if (PredictedAmmo.IsValidIndex(CurrentIndex))
	{
		--PredictedAmmo[CurrentIndex];
	}
	Spread.OnShot(*Def);
	if (bLocalHuman)
	{
		Char->AddRecoil(Def->RecoilForShot(ConsecutiveShots) * (Char->IsAiming() ? Def->AdsRecoilScale : 1.f));
		Char->KickViewModel(Def->bProjectile ? 3.f : 1.f);
	}
	++ConsecutiveShots;
	++Stats.Fired;

	FHLShotRequest Req;
	Req.Origin = Origin;
	Req.Direction = Dir;
	Req.FireTime = UHLServerClockComponent::ServerNow(this);
	Req.ViewTime = AHLCharacter::GetRemoteRenderTime(this);
	Req.ShotSeq = Seq;
	Req.WeaponIndex = uint8(CurrentIndex);

	if (!Def->bProjectile)
	{
		// Predict the hit marker and tracer now; the server will confirm or retract
		FVector End;
		EHLHitZone Zone;
		const bool bPredictedHit = PredictHit(Origin, Dir, Def->Range, End, Zone);
		if (!bAuthority)
		{
			PendingPredictions.Add(Seq, bPredictedHit);
			if (bPredictedHit)
			{
				++Stats.PredictedHits;
				Marker.Time = GetWorld()->GetTimeSeconds();
				Marker.State = EHLMarkerState::Predicted;
				Marker.Zone = Zone;
				Marker.bKill = false;
			}
		}
		if (bLocalHuman)
		{
			if (bDrawTraces)
			{
				DrawTracer(Origin + FVector(0, 0, -8), End, bPredictedHit ? FColor(255, 220, 80) : FColor(230, 230, 230), 0.08f);
			}
			RecordShotForReplay(Origin + FVector(0, 0, -8), End, bPredictedHit);
		}
	}

	if (bAuthority)
	{
		const FHLShotResult Result = ServerProcessShot(Req, false);
		if (bLocalHuman)
		{
			ApplyResult(Result);
		}
	}
	else
	{
		Server_Fire(Req);
	}
}

bool UHLWeaponComponent::ServerValidate(const FHLShotRequest& Request, FName& OutReason)
{
	AHLCharacter* Char = GetCharacter();
	const UHLWeaponDefinition* Def = Loadout.IsValidIndex(Request.WeaponIndex) ? Loadout[Request.WeaponIndex].Get() : nullptr;
	if (!Def || !Char || !Char->IsAlive())
	{
		OutReason = TEXT("dead_or_no_weapon");
		return false;
	}
	if (!Ammo.IsValidIndex(Request.WeaponIndex) || Ammo[Request.WeaponIndex] <= 0)
	{
		OutReason = TEXT("no_ammo");
		return false;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// Fire rate: token bucket at the weapon's rate, a little slack for packets that arrive bunched together
	if (!RateLimiter.TryConsume(Now, 1.f / Def->GetShotInterval()))
	{
		OutReason = TEXT("fire_rate");
		return false;
	}
	// Origin must be near where the server has the shooter's eyes (movement correction slack)
	if (FVector::Dist(Request.Origin, Char->GetEyeLocation()) > 200.f)
	{
		OutReason = TEXT("origin");
		return false;
	}
	// Views older than the rewind cap are traced AT the cap (TraceShot clamps, so a laggy player still gets
	// partial compensation). Only absurd timestamps (a broken clock or a forged packet) are refused.
	if (Now - Request.ViewTime > 1.0 || Request.ViewTime > Now + 0.1)
	{
		OutReason = TEXT("bad_timestamp");
		return false;
	}
	return true;
}

void UHLWeaponComponent::Server_Fire_Implementation(const FHLShotRequest& Request)
{
	const FHLShotResult Result = ServerProcessShot(Request, true);
	Client_ShotResult(Result);
}

FHLShotResult UHLWeaponComponent::ServerProcessShot(const FHLShotRequest& Request, bool bValidate)
{
	FHLShotResult Result;
	Result.ShotSeq = Request.ShotSeq;
	AHLCharacter* Char = GetCharacter();
	UHLShotAutopsySubsystem* Autopsy = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>();
	// The listen-server host and bots see the world as it is now: nothing to rewind
	const double ViewTime = bValidate ? Request.ViewTime : GetWorld()->GetTimeSeconds();
	if (bValidate)
	{
		FName Reason;
		if (!ServerValidate(Request, Reason))
		{
			Result.bRejected = true;
			Result.RejectReason = Reason;
			UE_LOG(LogHitreg, Verbose, TEXT("Shot %d rejected: %s"), Request.ShotSeq, *Reason.ToString());
			if (Autopsy)
			{
				Autopsy->ServerRecord(Char, Request, ViewTime, Reason, false, nullptr, false, 0.f, false);
			}
			return Result;
		}
	}
	const UHLWeaponDefinition* Def = Loadout.IsValidIndex(Request.WeaponIndex) ? Loadout[Request.WeaponIndex].Get() : GetCurrentWeapon();
	if (!Def || !Char)
	{
		Result.bRejected = true;
		return Result;
	}
	if (Ammo.IsValidIndex(Request.WeaponIndex))
	{
		--Ammo[Request.WeaponIndex];
	}

	const FVector Dir = FVector(Request.Direction).GetSafeNormal();
	if (Def->bProjectile)
	{
		FActorSpawnParameters Params;
		Params.Owner = Char;
		Params.Instigator = Char;
		if (AHLProjectile* P = GetWorld()->SpawnActor<AHLProjectile>(AHLProjectile::StaticClass(), FVector(Request.Origin) + Dir * 60.f, Dir.Rotation(), Params))
		{
			P->Init(Def);
		}
		if (Autopsy)
		{
			Autopsy->ServerRecord(Char, Request, ViewTime, NAME_None, true, nullptr, false, 0.f, false);
		}
		return Result;
	}

	UHLLagCompensationSubsystem* LagComp = GetWorld()->GetSubsystem<UHLLagCompensationSubsystem>();
	FHLTraceHit Hit;
	const bool bHit = LagComp && LagComp->TraceShot(Char, Request.Origin, Dir, Def->Range, ViewTime, Hit);
	Result.RewindMs = Hit.RewindSeconds * 1000.f;
	if (bHit && Hit.Target.IsValid())
	{
		const float Dist = Hit.Hit.Distance;
		float Damage = Def->DamageAtDistance(Dist) * Hit.Hit.DamageMultiplier;
		if (Hit.Hit.Zone == EHLHitZone::Head)
		{
			Damage *= Def->HeadshotMultiplier;
		}
		Result.Zone = Hit.Hit.Zone;
		Result.Damage = Damage;
		Result.bHasTarget = true;
		Result.TracedPose = Hit.TracedPose;
		Result.CurrentPose = Hit.CurrentPose;
		Result.bKill = Hit.Target->ServerApplyDamage(Damage, Char->GetController(), Hit.Hit.Zone, Def->DisplayName.ToString());
	}
	if (Autopsy && LagComp)
	{
		Autopsy->ServerRecord(Char, Request, ViewTime, NAME_None, false, &Hit, bHit && Hit.Target.IsValid(), Result.Damage, Result.bKill);
	}
	Multicast_ShotFX(FVector(Request.Origin) + FVector(0, 0, -8), Hit.End, bHit);
	return Result;
}

void UHLWeaponComponent::Client_ShotResult_Implementation(const FHLShotResult& Result)
{
	ApplyResult(Result);
}

void UHLWeaponComponent::ApplyResult(const FHLShotResult& Result)
{
	LastResult = Result;
	LastResultTime = GetWorld()->GetTimeSeconds();
	const bool* Predicted = PendingPredictions.Find(Result.ShotSeq);
	const bool bPredictedHit = Predicted && *Predicted;
	PendingPredictions.Remove(Result.ShotSeq);

	const bool bServerHit = Result.Zone != EHLHitZone::None;
	if (Result.bRejected)
	{
		++Stats.Rejected;
	}
	if (bServerHit)
	{
		++Stats.ConfirmedHits;
		if (!bPredictedHit && Predicted)
		{
			++Stats.SurpriseHits;
		}
		Marker.Time = GetWorld()->GetTimeSeconds();
		Marker.State = EHLMarkerState::Confirmed;
		Marker.Zone = Result.Zone;
		Marker.bKill = Result.bKill;
		Marker.Damage = Result.Damage;
	}
	else if (bPredictedHit)
	{
		++Stats.RetractedHits;
		Marker.Time = GetWorld()->GetTimeSeconds();
		Marker.State = EHLMarkerState::Rejected;
	}

	// Rewind debug: where the server tested the target (green) vs where it was at that moment (red)
	if (bDrawRewind && Result.bHasTarget)
	{
		const AHLCharacter* Char = GetCharacter();
		const TArray<FHLHitbox>& Boxes = Char ? Char->Hitboxes : AHLCharacter::MakeDefaultHitboxes();
		for (const FHLHitbox& Box : Boxes)
		{
			for (int32 Which = 0; Which < 2; ++Which)
			{
				const FHLPose& Pose = Which == 0 ? Result.TracedPose : Result.CurrentPose;
				const FColor Color = Which == 0 ? FColor(60, 230, 90) : FColor(230, 60, 60);
				const FVector C = HLHitboxMath::HitboxCenter(Box, Pose);
				if (Box.Shape == EHLHitboxShape::Sphere)
				{
					DrawDebugSphere(GetWorld(), C, Box.Extent.X, 12, Color, false, 1.5f, 0, 0.6f);
				}
				else
				{
					DrawDebugBox(GetWorld(), C, Box.Extent, FRotator(0.f, Pose.Yaw, 0.f).Quaternion(), Color, false, 1.5f, 0, 0.6f);
				}
			}
		}
	}
}

void UHLWeaponComponent::Multicast_ShotFX_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End, bool bHitCharacter)
{
	const AHLCharacter* Char = GetCharacter();
	if (GetNetMode() == NM_DedicatedServer || (Char && Char->IsLocallyControlled() && Char->IsPlayerControlled()))
	{
		return; // the shooter already drew its own predicted tracer
	}
	DrawTracer(Start, End, bHitCharacter ? FColor(255, 120, 60) : FColor(220, 220, 220), 0.08f);
	RecordShotForReplay(Start, End, bHitCharacter);
}

void UHLWeaponComponent::RecordShotForReplay(const FVector& Start, const FVector& End, bool bHit) const
{
	const APlayerController* LocalPC = GetWorld()->GetFirstPlayerController();
	if (UHLReplayComponent* Replay = LocalPC ? LocalPC->FindComponentByClass<UHLReplayComponent>() : nullptr)
	{
		const AHLCharacter* Char = GetCharacter();
		Replay->RecordShot(Char ? Char->GetHLPlayerId() : 0, Start, End, bHit);
	}
}

void UHLWeaponComponent::DrawTracer(const FVector& Start, const FVector& End, const FColor& Color, float Life) const
{
	DrawDebugLine(GetWorld(), Start, End, Color, false, Life, 0, 1.2f);
	DrawDebugPoint(GetWorld(), End, 7.f, Color, false, Life * 3.f);
}

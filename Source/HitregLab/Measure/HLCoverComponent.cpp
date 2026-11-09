// Hitreg Lab: behind-cover deaths, client side
#include "Measure/HLCoverComponent.h"
#include "Measure/HLCoverMeterSubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLPlayerController.h"
#include "Net/HLServerClockComponent.h"
#include "Weapons/HLWeaponComponent.h"
#include "EngineUtils.h"
#include "HitregLab.h"

namespace
{
	const FVector CoverTorso(0.f, 0.f, 22.f);
	const FVector CoverHead(0.f, 0.f, 66.f);
}

UHLCoverComponent::UHLCoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After everything has moved and remote players are drawn for this frame: sight checks see the final
	// positions, and the shooter's shot uses exactly the poses on its screen this frame
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

void UHLCoverComponent::Client_BeginTrial_Implementation(int32 TrialId, EHLCoverRole InRole, FVector Start, FRotator Facing, FVector InPathStart, FVector InPathEnd, float StrafeDelay, float LeadCm)
{
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	Role = InRole;
	ActiveTrial = TrialId;
	PathStart = InPathStart;
	PathEnd = InPathEnd;
	Lead = LeadCm;
	StrafeAt = GetWorld()->GetTimeSeconds() + StrafeDelay;
	bHadSight = bLostSight = bHit = false;
	bEdgeKnown = bFired = false;
	PC->SetControlRotation(Facing);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UHLCoverComponent::Client_EndTrial_Implementation()
{
	if (Role == EHLCoverRole::Runner)
	{
		Server_ReportRunner(ActiveTrial, bLostSight, LostSightTime, LostSightAlong, bHit, HitTime, HitAlong);
	}
	else if (Role == EHLCoverRole::Shooter)
	{
		if (!bFired)
		{
			UE_LOG(LogHitreg, Warning, TEXT("Cover trial %d: the shooter never fired (edge known %d)"), ActiveTrial, bEdgeKnown ? 1 : 0);
		}
		// One shot per trial: top the magazine up in the gap so the next trial never fires mid-reload
		const AHLCharacter* Me = Cast<AHLCharacter>(Cast<APlayerController>(GetOwner())->GetPawn());
		if (Me && Me->Weapon && Me->Weapon->GetAmmo() <= 5)
		{
			Me->Weapon->RequestReload();
		}
	}
	Role = EHLCoverRole::None;
	ActiveTrial = INDEX_NONE;
}

void UHLCoverComponent::Client_Hit_Implementation(int32 TrialId)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (TrialId != ActiveTrial || Role != EHLCoverRole::Runner || bHit || !Me)
	{
		return;
	}
	bHit = true;
	HitTime = UHLServerClockComponent::ServerNow(this);
	HitAlong = float(FVector::DotProduct(Me->GetActorLocation() - PathStart, (PathEnd - PathStart).GetSafeNormal2D()));
}

void UHLCoverComponent::Client_Summary_Implementation(const FString& Summary)
{
	LastSummary = Summary;
	if (AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner()))
	{
		PC->ShowToast(TEXT("Cover sweep: ") + Summary);
	}
}

void UHLCoverComponent::Server_ReportShot_Implementation(int32 TrialId, float LeadCm, bool bTorsoVisible)
{
	if (UHLCoverMeterSubsystem* Meter = GetWorld()->GetSubsystem<UHLCoverMeterSubsystem>())
	{
		Meter->ReportShot(Cast<AHLPlayerController>(GetOwner()), TrialId, LeadCm, bTorsoVisible);
	}
}

void UHLCoverComponent::Server_ReportRunner_Implementation(int32 TrialId, bool bInLostSight, double InLostSightTime, float InLostSightAlong, bool bInHit, double InHitTime, float InHitAlong)
{
	if (UHLCoverMeterSubsystem* Meter = GetWorld()->GetSubsystem<UHLCoverMeterSubsystem>())
	{
		Meter->ReportRunner(Cast<AHLPlayerController>(GetOwner()), TrialId, bInLostSight, InLostSightTime, InLostSightAlong, bInHit, InHitTime, InHitAlong);
	}
}

void UHLCoverComponent::Server_StartSweep_Implementation(int32 TrialsPerConfig)
{
	if (UHLCoverMeterSubsystem* Meter = GetWorld()->GetSubsystem<UHLCoverMeterSubsystem>())
	{
		Client_Summary(Meter->StartSweep(TrialsPerConfig));
	}
}

AHLCharacter* UHLCoverComponent::FindOther(const AHLCharacter* Me) const
{
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != Me && It->IsAlive())
		{
			return *It;
		}
	}
	return nullptr;
}

bool UHLCoverComponent::TorsoVisible(const AHLCharacter* Me, const FVector& Point) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLCoverSight), true);
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	return !GetWorld()->LineTraceTestByChannel(Me->GetEyeLocation(), Point, ECC_Visibility, Params);
}

void UHLCoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || Role == EHLCoverRole::None)
	{
		return;
	}
	AHLCharacter* Me = Cast<AHLCharacter>(PC->GetPawn());
	if (!Me || GetWorld()->GetTimeSeconds() < StrafeAt)
	{
		return;
	}
	if (Role == EHLCoverRole::Runner)
	{
		TickRunner(Me);
	}
	else
	{
		TickShooter(Me);
	}
}

void UHLCoverComponent::TickRunner(AHLCharacter* Me)
{
	const FVector Dir = (PathEnd - PathStart).GetSafeNormal2D();
	const float Along = float(FVector::DotProduct(Me->GetActorLocation() - PathStart, Dir));
	const float Length = float(FVector::Dist2D(PathStart, PathEnd));
	if (Along < Length - 20.f)
	{
		Me->AddMovementInput(Dir, 1.f); // full-speed strafe into cover, same input every trial
	}
	// When does MY screen lose sight of the shooter's head? (Same test as the peek meter.)
	const AHLCharacter* Other = FindOther(Me);
	if (!Other || bLostSight)
	{
		return;
	}
	const bool bSees = TorsoVisible(Me, Other->GetPose().Location + CoverHead);
	if (bSees)
	{
		bHadSight = true;
	}
	else if (bHadSight)
	{
		bLostSight = true;
		LostSightTime = UHLServerClockComponent::ServerNow(this);
		LostSightAlong = Along;
	}
}

void UHLCoverComponent::TickShooter(AHLCharacter* Me)
{
	AHLCharacter* Target = FindOther(Me);
	if (!Target || bFired || !Me->Weapon)
	{
		return;
	}
	const FVector Dir = (PathEnd - PathStart).GetSafeNormal2D();
	const FHLPose Drawn = Target->GetPose();
	const float Along = float(FVector::DotProduct(Drawn.Location - PathStart, Dir));
	if (!bEdgeKnown)
	{
		// Where along the path does the torso centre leave MY screen? Binary search with line traces.
		float Lo = 0.f;
		float Hi = float(FVector::Dist2D(PathStart, PathEnd));
		const FVector Base = FVector(PathStart.X, PathStart.Y, Drawn.Location.Z) + CoverTorso;
		if (!TorsoVisible(Me, Base) || TorsoVisible(Me, Base + Dir * Hi))
		{
			return; // geometry isn't what the trial expects; try again next frame
		}
		for (int32 i = 0; i < 24; ++i)
		{
			const float Mid = 0.5f * (Lo + Hi);
			(TorsoVisible(Me, Base + Dir * Mid) ? Lo : Hi) = Mid;
		}
		EdgeAlong = Lo;
		bEdgeKnown = true;
	}
	// Perfect aim at the torso as drawn on this screen, every frame
	const FVector Aim = Drawn.Location + CoverTorso;
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		PC->SetControlRotation((Aim - Me->GetEyeLocation()).Rotation());
	}
	// Fire at the last moment: when the drawn torso centre is Lead cm from leaving the screen
	if (Along >= EdgeAlong - Lead)
	{
		const bool bVisible = TorsoVisible(Me, Aim);
		bFired = true;
		if (bVisible && Me->Weapon->FireSingleNow())
		{
			Server_ReportShot(ActiveTrial, EdgeAlong - Along, true);
		}
		else
		{
			Server_ReportShot(ActiveTrial, EdgeAlong - Along, false);
			UE_LOG(LogHitreg, Warning, TEXT("Cover trial %d: no shot (torso visible %d)"), ActiveTrial, bVisible ? 1 : 0);
		}
	}
}

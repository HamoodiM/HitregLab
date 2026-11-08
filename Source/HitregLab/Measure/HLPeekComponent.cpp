// Hitreg Lab: peeker's-advantage measurement, client side
#include "Measure/HLPeekComponent.h"
#include "Measure/HLPeekMeterSubsystem.h"
#include "Core/HLCharacter.h"
#include "Core/HLPlayerController.h"
#include "Net/HLServerClockComponent.h"
#include "EngineUtils.h"

UHLPeekComponent::UHLPeekComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // check sight after this frame's positions are final
	SetIsReplicatedByDefault(true);
}

void UHLPeekComponent::Client_BeginTrial_Implementation(int32 TrialId, EHLPeekRole InRole, FVector Start, FRotator Facing, FVector End, float StrafeDelay, int32 InterpTicks)
{
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	Role = InRole;
	ActiveTrial = TrialId;
	EndPoint = End;
	bReported = false;
	StrafeAt = GetWorld()->GetTimeSeconds() + StrafeDelay;
	PC->InterpTicks = InterpTicks;
	PC->SetControlRotation(Facing);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UHLPeekComponent::Client_EndTrial_Implementation()
{
	Role = EHLPeekRole::None;
	ActiveTrial = INDEX_NONE;
}

void UHLPeekComponent::Client_Summary_Implementation(const FString& Summary)
{
	LastSummary = Summary;
	if (AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner()))
	{
		PC->ShowToast(TEXT("Peek sweep: ") + Summary);
	}
}

void UHLPeekComponent::Server_ReportSight_Implementation(int32 TrialId, double ServerTime)
{
	if (UHLPeekMeterSubsystem* Meter = GetWorld()->GetSubsystem<UHLPeekMeterSubsystem>())
	{
		Meter->ReportSight(Cast<AHLPlayerController>(GetOwner()), TrialId, ServerTime);
	}
}

void UHLPeekComponent::Server_StartSweep_Implementation(int32 TrialsPerConfig, bool bFullGrid)
{
	if (UHLPeekMeterSubsystem* Meter = GetWorld()->GetSubsystem<UHLPeekMeterSubsystem>())
	{
		const FString Status = Meter->StartSweep(TrialsPerConfig, bFullGrid);
		Client_Summary(Status);
	}
}

void UHLPeekComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || Role == EHLPeekRole::None)
	{
		return;
	}
	AHLCharacter* Me = Cast<AHLCharacter>(PC->GetPawn());
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Me || Now < StrafeAt)
	{
		return;
	}

	// The peeker strafes out at full speed (same input every trial)
	if (Role == EHLPeekRole::Peeker)
	{
		const FVector ToEnd = (EndPoint - Me->GetActorLocation()) * FVector(1, 1, 0);
		if (ToEnd.Size() > 20.f)
		{
			Me->AddMovementInput(ToEnd.GetSafeNormal(), 1.f);
		}
	}

	// First moment this client's own screen shows the other player's head
	if (bReported)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(HLPeekSight), true);
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<AHLCharacter> It(GetWorld()); It; ++It)
	{
		const AHLCharacter* Other = *It;
		if (Other == Me || !Other->IsAlive())
		{
			continue;
		}
		const FVector Head = Other->GetPose().Location + FVector(0.f, 0.f, 66.f);
		if (!GetWorld()->LineTraceTestByChannel(Me->GetEyeLocation(), Head, ECC_Visibility, Params))
		{
			bReported = true;
			Server_ReportSight(ActiveTrial, UHLServerClockComponent::ServerNow(this));
			break;
		}
	}
}

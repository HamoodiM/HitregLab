// Hitreg Lab: Shot Autopsy viewer
#include "Autopsy/HLAutopsyViewerComponent.h"
#include "Autopsy/HLShotAutopsySubsystem.h"
#include "Core/HLCharacter.h"
#include "Net/HLHitboxMath.h"
#include "Replay/HLReplayComponent.h"
#include "Replay/HLReplayData.h"
#include "Replay/HLReplayGhost.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "HitregLab.h"

const FColor UHLAutopsyViewerComponent::ViewColor(80, 170, 255);
const FColor UHLAutopsyViewerComponent::TracedColor(70, 225, 100);
const FColor UHLAutopsyViewerComponent::NowColor(245, 70, 60);
const FColor UHLAutopsyViewerComponent::VictimColor(235, 100, 235);

namespace
{
	constexpr float PlayRate = 0.25f;      // scrub playback speed (slow motion)
	constexpr float ScrubKeyRate = 0.3f;   // held arrow keys
	const FVector TorsoOffset(0.f, 0.f, 22.f);
}

UHLAutopsyViewerComponent::UHLAutopsyViewerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UHLAutopsyViewerComponent::BeginPlay()
{
	Super::BeginPlay();
	// Demo capture: "-AutopsyDemoAt=S" opens the viewer on the newest hit on this player S seconds in
	float DemoAt = 0.f;
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC && PC->IsLocalController() && FParse::Value(FCommandLine::Get(), TEXT("AutopsyDemoAt="), DemoAt) && DemoAt > 0.f)
	{
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { OpenOnLatestHitOnMe(true, true); }), DemoAt, false);
	}
}

void UHLAutopsyViewerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Close();
	Super::EndPlay(EndPlayReason);
}

int32 UHLAutopsyViewerComponent::GetLocalId() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC && PC->PlayerState ? PC->PlayerState->GetPlayerId() : INDEX_NONE;
}

const TCHAR* UHLAutopsyViewerComponent::GetFilterName() const
{
	switch (Filter)
	{
	case EFilter::Mine: return TEXT("shots I fired");
	case EFilter::AtMe: return TEXT("shots at me");
	default: return TEXT("all shots");
	}
}

bool UHLAutopsyViewerComponent::PassesFilter(const FHLShotAutopsy& Shot) const
{
	const int32 Me = GetLocalId();
	switch (Filter)
	{
	case EFilter::Mine: return Shot.ShooterId == Me;
	case EFilter::AtMe: return Shot.TargetId == Me;
	default: return true;
	}
}

void UHLAutopsyViewerComponent::Refresh()
{
	const TArray<FHLShotAutopsy>* Source = &FileShots;
	if (!bFile)
	{
		const UHLShotAutopsySubsystem* Log = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>();
		if (!Log)
		{
			return;
		}
		SeenRevision = Log->GetRevision();
		Source = &Log->GetRecords();
	}
	Shots.Reset();
	for (int32 i = Source->Num() - 1; i >= 0 && Shots.Num() < 500; --i)
	{
		if (PassesFilter((*Source)[i]))
		{
			Shots.Add((*Source)[i]);
		}
	}
	// Keep the same shot selected when new ones arrive
	Selected = Shots.IndexOfByPredicate([this](const FHLShotAutopsy& S) { return S.ShotId == SelectedShotId; });
	if (Selected == INDEX_NONE && Shots.Num() > 0)
	{
		Select(0, true);
	}
}

void UHLAutopsyViewerComponent::Select(int32 Index, bool bResetCamera)
{
	if (!Shots.IsValidIndex(Index))
	{
		return;
	}
	Selected = Index;
	const FHLShotAutopsy& S = Shots[Index];
	SelectedShotId = S.ShotId;
	ScrubMs = -S.ViewAgeMs; // start on what the shooter saw
	EndHold = 0.f;
	FocusGoal = S.HasTarget() ? S.TracedPose.Dequantize().Location : S.GetOrigin() + S.GetDirection() * FMath::Min(S.WorldBlockCm * 0.7f, 1500.f);
	if (bResetCamera)
	{
		// Behind the target looking back at the shooter, swung to the side so the ray reads in depth
		BaseYaw = S.Shot.Dequantize().Yaw + 180.f + 55.f;
		OrbitYaw = BaseYaw;
		OrbitPitch = -22.f;
		OrbitDist = 700.f;
		if (Focus.IsZero())
		{
			Focus = FocusGoal;
		}
	}
	if (Ghost)
	{
		Ghost->SetActorHiddenInGame(!S.HasTarget() || S.Track.Num() == 0);
	}
}

void UHLAutopsyViewerComponent::Toggle()
{
	if (bOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

void UHLAutopsyViewerComponent::Open()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (bOpen || !PC || !PC->IsLocalController())
	{
		return;
	}
	if (const UHLReplayComponent* Replay = PC->FindComponentByClass<UHLReplayComponent>(); Replay && Replay->IsPlaying())
	{
		return; // one replay view at a time
	}
	bOpen = true;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	if (Camera && Camera->GetCameraComponent())
	{
		Camera->GetCameraComponent()->SetFieldOfView(70.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	Ghost = GetWorld()->SpawnActor<AHLReplayGhost>(AHLReplayGhost::StaticClass(), FTransform::Identity, Params);
	if (Ghost)
	{
		Ghost->SetColor(FLinearColor(0.95f, 0.85f, 0.35f));
		Ghost->SetActorHiddenInGame(true);
	}
	Focus = FVector::ZeroVector;
	SelectedShotId = 0;
	Refresh();
	if (Shots.IsValidIndex(Selected))
	{
		Select(Selected, true);
	}
	UpdateCamera(1.f);
	if (APawn* Pawn = PC->GetPawn())
	{
		Pawn->DisableInput(PC);
	}
	if (Camera)
	{
		PC->SetViewTargetWithBlend(Camera, 0.f);
	}
}

void UHLAutopsyViewerComponent::Close()
{
	if (!bOpen)
	{
		return;
	}
	bOpen = false;
	bPlaying = false;
	bAutoOrbit = false;
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->EnableInput(PC);
			PC->SetViewTargetWithBlend(Pawn, 0.f);
		}
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	if (Ghost)
	{
		Ghost->Destroy();
		Ghost = nullptr;
	}
}

FString UHLAutopsyViewerComponent::LoadFile(const FString& Path)
{
	FString Full = Path;
	if (FPaths::IsRelative(Full))
	{
		Full = FPaths::ProjectSavedDir() / TEXT("Replays") / Path;
	}
	FHLReplayClip Clip;
	if (!Clip.LoadFromFile(Full))
	{
		return FString::Printf(TEXT("Could not load %s (missing, or written by a newer build)"), *Full);
	}
	if (Clip.Autopsies.Num() == 0)
	{
		return FString::Printf(TEXT("%s has no shot autopsy records (version 1 file?)"), *FPaths::GetCleanFilename(Full));
	}
	Close();
	FileShots = Clip.Autopsies;
	FileName = FPaths::GetCleanFilename(Full);
	bFile = true;
	Filter = EFilter::All;
	Open();
	return FString::Printf(TEXT("Shot autopsy: %d shots from %s"), FileShots.Num(), *FileName);
}

void UHLAutopsyViewerComponent::OpenOnLatestHitOnMe(bool bInAutoPlay, bool bInAutoOrbit)
{
	bFile = false;
	Filter = EFilter::AtMe;
	if (!bOpen)
	{
		Open();
	}
	Refresh();
	auto SelectNewestHit = [this]()
	{
		for (int32 i = 0; i < Shots.Num(); ++i)
		{
			if (Shots[i].IsRegistered())
			{
				Select(i, true);
				return true;
			}
		}
		return false;
	};
	if (!SelectNewestHit())
	{
		Filter = EFilter::Mine; // this player was the shooter: show its newest hit instead
		Refresh();
		SelectNewestHit();
	}
	bPlaying = bInAutoPlay;
	bAutoOrbit = bInAutoOrbit;
	AutoOrbitTime = 0.f;
	UE_LOG(LogHitreg, Display, TEXT("Autopsy viewer: %d shots at me, showing shot %u"), Shots.Num(), SelectedShotId);
}

void UHLAutopsyViewerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bOpen)
	{
		return;
	}
	if (!bFile)
	{
		const UHLShotAutopsySubsystem* Log = GetWorld()->GetSubsystem<UHLShotAutopsySubsystem>();
		if (Log && Log->GetRevision() != SeenRevision)
		{
			Refresh();
		}
	}
	HandleKeys(DeltaTime);

	if (const FHLShotAutopsy* S = GetSelectedShot())
	{
		const float Start = S->TrackStartMs;
		const float End = FMath::Max(S->GetTrackEndMs(), Start + 1.f);
		if (bPlaying)
		{
			if (ScrubMs >= End)
			{
				EndHold += DeltaTime;
				if (EndHold > 1.5f)
				{
					ScrubMs = Start;
					EndHold = 0.f;
				}
			}
			else
			{
				ScrubMs = FMath::Min(End, ScrubMs + DeltaTime * 1000.f * PlayRate);
			}
		}
		ScrubMs = FMath::Clamp(ScrubMs, Start, End);
		FHLPose Pose;
		if (Ghost && S->SampleTrack(ScrubMs, Pose))
		{
			Ghost->ApplyPose(Pose, true);
		}
	}
	UpdateCamera(DeltaTime);
	DrawWorld();
}

void UHLAutopsyViewerComponent::HandleKeys(float DeltaTime)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC->WasInputKeyJustPressed(EKeys::Escape))
	{
		Close();
		return;
	}
	if (PC->WasInputKeyJustPressed(EKeys::Up) && Selected > 0)
	{
		Select(Selected - 1, true);
	}
	if (PC->WasInputKeyJustPressed(EKeys::Down) && Selected + 1 < Shots.Num())
	{
		Select(Selected + 1, true);
	}
	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		Filter = EFilter((uint8(Filter) + 1) % 3);
		Refresh();
	}
	if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		bPlaying = !bPlaying;
		EndHold = 0.f;
	}
	const float Dir = (PC->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
	if (Dir != 0.f)
	{
		bPlaying = false;
		ScrubMs += Dir * DeltaTime * 1000.f * ScrubKeyRate;
	}
	// Orbit with the mouse, zoom with the wheel
	float Dx = 0.f, Dy = 0.f;
	PC->GetInputMouseDelta(Dx, Dy);
	if (Dx != 0.f || Dy != 0.f)
	{
		bAutoOrbit = false;
		OrbitYaw += Dx * 0.25f;
		OrbitPitch = FMath::Clamp(OrbitPitch + Dy * 0.25f, -80.f, 10.f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		OrbitDist = FMath::Max(200.f, OrbitDist * 0.85f);
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		OrbitDist = FMath::Min(4000.f, OrbitDist / 0.85f);
	}
}

void UHLAutopsyViewerComponent::UpdateCamera(float DeltaTime)
{
	if (!Camera)
	{
		return;
	}
	if (bAutoOrbit)
	{
		AutoOrbitTime += DeltaTime;
		OrbitYaw = BaseYaw - 30.f * FMath::Sin(AutoOrbitTime * 2.f * UE_PI / 18.f);
	}
	Focus = FMath::VInterpTo(Focus, FocusGoal, DeltaTime, 6.f);
	const FRotator Look(OrbitPitch, OrbitYaw, 0.f);
	Camera->SetActorLocationAndRotation(Focus - Look.Vector() * OrbitDist, Look);
}

void UHLAutopsyViewerComponent::DrawPose(const FHLPose& Pose, const FColor& Color, float Thickness) const
{
	for (const FHLHitbox& Box : AHLCharacter::MakeDefaultHitboxes())
	{
		const FVector C = HLHitboxMath::HitboxCenter(Box, Pose);
		if (Box.Shape == EHLHitboxShape::Sphere)
		{
			DrawDebugSphere(GetWorld(), C, Box.Extent.X, 12, Color, false, -1.f, SDPG_World, Thickness);
		}
		else
		{
			DrawDebugBox(GetWorld(), C, Box.Extent, FRotator(0.f, Pose.Yaw, 0.f).Quaternion(), Color, false, -1.f, SDPG_World, Thickness);
		}
	}
}

void UHLAutopsyViewerComponent::GetMarks(const FHLShotAutopsy& S, TArray<FMark>& Out) const
{
	Out.Reset();
	auto Add = [&Out](float Ms, const FString& Label, const FColor& Color, const FHLQuantPose* Pose)
	{
		FMark M;
		M.Ms = Ms;
		M.Label = Label;
		M.Color = Color;
		if (Pose)
		{
			M.World = Pose->Dequantize().Location + FVector(0.f, 0.f, 100.f);
			M.bHasPose = true;
		}
		Out.Add(M);
	};
	Add(-S.ViewAgeMs, TEXT("shooter's screen"), ViewColor, S.HasTarget() ? &S.ViewPose : nullptr);
	Add(-S.RewindMs, FString::Printf(TEXT("server tested (-%.0f ms)"), S.RewindMs), TracedColor, S.HasTarget() ? &S.TracedPose : nullptr);
	Add(0.f, TEXT("server decided"), NowColor, S.HasTarget() ? &S.CurrentPose : nullptr);
	if (S.HasTarget() && S.TargetRttMs > 0.f && S.Track.Num() > 0)
	{
		FHLPose P;
		S.SampleTrack(S.TargetRttMs, P);
		const FHLQuantPose Q = FHLQuantPose::Quantize(P);
		Add(S.TargetRttMs, TEXT("victim's own screen as the hit lands (est.)"), VictimColor, &Q);
	}
}

void UHLAutopsyViewerComponent::DrawWorld() const
{
	const FHLShotAutopsy* S = GetSelectedShot();
	if (!S)
	{
		return;
	}
	UWorld* World = GetWorld();
	const FVector Origin = S->GetOrigin();
	const FVector Dir = S->GetDirection();
	const FColor RayColor = HLShotAutopsy::VerdictColor(S->Verdict);
	const FVector End = S->GetRayEnd();
	// The shooter's eye and ray; past a hit, a faint line on to the first wall
	DrawDebugSphere(World, Origin, 10.f, 10, FColor::White, false, -1.f, SDPG_World, 1.5f);
	DrawDebugLine(World, Origin, End, RayColor, false, -1.f, SDPG_World, 2.5f);
	if (S->HitCm > 0.f && S->WorldBlockCm > S->HitCm)
	{
		DrawDebugLine(World, End, Origin + Dir * FMath::Min(S->WorldBlockCm, S->HitCm + 800.f), FColor(120, 120, 120), false, -1.f, SDPG_World, 0.8f);
	}
	DrawDebugPoint(World, Origin + Dir * S->WorldBlockCm, 9.f, FColor(150, 150, 150), false, -1.f);
	if (!S->HasTarget())
	{
		return;
	}
	TArray<FMark> Marks;
	GetMarks(*S, Marks);
	DrawPose(S->ViewPose.Dequantize(), ViewColor, 2.5f);
	DrawPose(S->TracedPose.Dequantize(), TracedColor, 1.8f);
	DrawPose(S->CurrentPose.Dequantize(), NowColor, 1.8f);
	if (Marks.Num() > 3)
	{
		FHLPose P;
		S->SampleTrack(Marks[3].Ms, P);
		DrawPose(P, VictimColor, 1.8f);
	}
	// The target's path on the server, with a tick every 25 ms
	FVector Prev = FVector::ZeroVector;
	for (int32 i = 0; i < S->Track.Num(); ++i)
	{
		const FVector P = S->Track[i].Dequantize().Location + TorsoOffset;
		if (i > 0)
		{
			DrawDebugLine(World, Prev, P, FColor(230, 230, 230), false, -1.f, SDPG_World, 1.f);
		}
		DrawDebugPoint(World, P, 4.f, FColor(230, 230, 230), false, -1.f);
		Prev = P;
	}
}

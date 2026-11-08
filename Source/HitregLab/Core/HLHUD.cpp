// Hitreg Lab: HUD
#include "Core/HLHUD.h"
#include "Aim/HLAimAssistComponent.h"
#include "Core/HLCharacter.h"
#include "Core/HLGameState.h"
#include "Core/HLPlayerController.h"
#include "Measure/HLPeekComponent.h"
#include "Net/HLServerClockComponent.h"
#include "Replay/HLReplayComponent.h"
#include "Weapons/HLWeaponComponent.h"
#include "Weapons/HLWeaponDefinition.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
	const FLinearColor White(0.95f, 0.95f, 0.95f);
	const FLinearColor Dim(0.75f, 0.78f, 0.82f);
	const FLinearColor Good(0.45f, 0.9f, 0.5f);
	const FLinearColor Bad(1.f, 0.35f, 0.3f);
	const FLinearColor Warn(1.f, 0.75f, 0.25f);
	const FLinearColor Panel(0.f, 0.f, 0.f, 0.45f);
	const TCHAR* AimModeNames[] = { TEXT("off"), TEXT("slowdown"), TEXT("full") };
}

float AHLHUD::Scale() const
{
	return Canvas ? FMath::Clamp(Canvas->ClipY / 900.f, 0.9f, 2.f) : 1.f;
}

void AHLHUD::Text(const FString& S, float X, float Y, const FLinearColor& Color, float InScale, bool bShadow)
{
	UFont* Font = GEngine->GetMediumFont();
	if (bShadow)
	{
		DrawText(S, FLinearColor(0.f, 0.f, 0.f, 0.8f), X + 1.f, Y + 1.f, Font, InScale * Scale());
	}
	DrawText(S, Color, X, Y, Font, InScale * Scale());
}

void AHLHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	AHLPlayerController* PC = Cast<AHLPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}
	if (PC->Replay && PC->Replay->IsPlaying())
	{
		DrawKillCam(PC);
		DrawToast(PC);
		return;
	}
	const AHLCharacter* Char = Cast<AHLCharacter>(PC->GetPawn());
	if (Char && Char->IsAlive())
	{
		DrawCrosshair(Char);
		DrawHitMarker(Char);
		DrawWeaponPanel(Char);
		DrawAimAssistDebug(Char);
	}
	else if (Char)
	{
		Text(TEXT("You died"), Canvas->ClipX * 0.5f - 60.f * Scale(), Canvas->ClipY * 0.45f, Bad, 1.6f);
	}
	if (PC->bShowNetOverlay)
	{
		DrawNetOverlay(PC, Char);
	}
	DrawKillFeed();
	DrawToast(PC);
}

void AHLHUD::DrawCrosshair(const AHLCharacter* Char)
{
	const UHLWeaponComponent* W = Char->Weapon;
	if (!W || !W->GetCurrentWeapon())
	{
		return;
	}
	const float Cx = Canvas->ClipX * 0.5f;
	const float Cy = Canvas->ClipY * 0.5f;
	const float Ads = Char->GetAdsAlpha();
	// The gap is the actual spread cone projected to the screen
	const float Fov = Char->Camera ? Char->Camera->FieldOfView : 90.f;
	const float Spread = W->GetCurrentSpreadDeg();
	const float Gap = FMath::Tan(FMath::DegreesToRadians(Spread)) / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * (Canvas->ClipX * 0.5f) + 4.f * Scale();
	const float Len = 10.f * Scale();
	const FLinearColor C(1.f, 1.f, 1.f, 1.f - Ads * 0.85f);
	DrawLine(Cx - Gap - Len, Cy, Cx - Gap, Cy, C, 2.f);
	DrawLine(Cx + Gap, Cy, Cx + Gap + Len, Cy, C, 2.f);
	DrawLine(Cx, Cy - Gap - Len, Cx, Cy - Gap, C, 2.f);
	DrawLine(Cx, Cy + Gap, Cx, Cy + Gap + Len, C, 2.f);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.9f), Cx - 1.f, Cy - 1.f, 2.f, 2.f);
}

void AHLHUD::DrawHitMarker(const AHLCharacter* Char)
{
	const FHLHitMarker& M = Char->Weapon->GetHitMarker();
	const float Age = float(GetWorld()->GetTimeSeconds() - M.Time);
	if (M.State == EHLMarkerState::None || Age > 0.35f)
	{
		return;
	}
	const float Cx = Canvas->ClipX * 0.5f;
	const float Cy = Canvas->ClipY * 0.5f;
	const float A = 1.f - Age / 0.35f;
	float Size = 9.f * Scale();
	FLinearColor C = White;
	float Thick = 2.f;
	switch (M.State)
	{
	case EHLMarkerState::Predicted:
		C = FLinearColor(1.f, 1.f, 1.f, 0.6f * A);
		break;
	case EHLMarkerState::Confirmed:
		C = M.bKill ? Bad : (M.Zone == EHLHitZone::Head ? Warn : White);
		C.A = A;
		Thick = M.bKill ? 3.5f : 2.5f;
		Size *= M.bKill ? 1.5f : 1.f;
		break;
	case EHLMarkerState::Rejected:
		C = FLinearColor(0.9f, 0.3f, 0.9f, A);
		Text(TEXT("hit not confirmed by server"), Cx + 20.f * Scale(), Cy + 18.f * Scale(), C, 0.8f);
		break;
	default:
		break;
	}
	const float In = 5.f * Scale();
	DrawLine(Cx - In - Size, Cy - In - Size, Cx - In, Cy - In, C, Thick);
	DrawLine(Cx + In + Size, Cy - In - Size, Cx + In, Cy - In, C, Thick);
	DrawLine(Cx - In - Size, Cy + In + Size, Cx - In, Cy + In, C, Thick);
	DrawLine(Cx + In + Size, Cy + In + Size, Cx + In, Cy + In, C, Thick);
}

void AHLHUD::DrawWeaponPanel(const AHLCharacter* Char)
{
	const UHLWeaponComponent* W = Char->Weapon;
	const UHLWeaponDefinition* Def = W->GetCurrentWeapon();
	if (!Def)
	{
		return;
	}
	const float S = Scale();
	const float X = Canvas->ClipX - 300.f * S;
	const float Y = Canvas->ClipY - 120.f * S;
	DrawRect(Panel, X - 12.f * S, Y - 10.f * S, 290.f * S, 100.f * S);
	Text(Def->DisplayName.ToString(), X, Y, Dim, 1.f);
	Text(FString::Printf(TEXT("%d / %d"), W->GetAmmo(), Def->MagazineSize), X, Y + 26.f * S, W->GetAmmo() > 0 ? White : Bad, 1.6f);
	if (W->IsReloading())
	{
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.2f), X, Y + 72.f * S, 250.f * S, 6.f * S);
		DrawRect(Warn, X, Y + 72.f * S, 250.f * S * W->GetReloadAlpha(), 6.f * S);
	}
	// Health, bottom left
	const float HX = 30.f * S;
	DrawRect(Panel, HX - 12.f * S, Y - 10.f * S, 260.f * S, 100.f * S);
	Text(TEXT("Health"), HX, Y, Dim, 1.f);
	Text(FString::Printf(TEXT("%.0f"), Char->Health), HX, Y + 26.f * S, Char->Health > 35.f ? White : Bad, 1.6f);
}

void AHLHUD::DrawNetOverlay(AHLPlayerController* PC, const AHLCharacter* Char)
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	if (!GS)
	{
		return;
	}
	const float S = Scale();
	float X = 24.f * S;
	float Y = 24.f * S;
	const float Line = 22.f * S;
	TArray<TPair<FString, FLinearColor>> Lines;
	const bool bClient = GetNetMode() == NM_Client;
	const double Rtt = PC->Clock ? PC->Clock->GetRttMs() : 0.0;
	Lines.Add({ FString::Printf(TEXT("Net: %s   RTT %.0f ms (sim %d, jitter %d)"), bClient ? TEXT("client") : TEXT("server/host"), Rtt, GS->SimulatedRttMs, GS->SimulatedJitterMs), White });
	Lines.Add({ FString::Printf(TEXT("Tick %d Hz   interp %d ticks = %.0f ms"), GS->ServerTickRate, PC->InterpTicks, GS->GetInterpDelaySeconds(PC->InterpTicks) * 1000.0), White });
	Lines.Add({ FString::Printf(TEXT("Lag compensation %s   max rewind %.0f ms"), GS->bLagCompensation ? TEXT("ON") : TEXT("OFF"), GS->MaxRewindMs), GS->bLagCompensation ? Good : Bad });
	if (PC->Clock && bClient)
	{
		Lines.Add({ FString::Printf(TEXT("Clock sync %s   engine GameState clock off by %+.0f ms"), PC->Clock->IsSynced() ? TEXT("ok") : TEXT("..."), PC->Clock->GetEngineClockErrorMs()), Dim });
	}
	Lines.Add({ FString::Printf(TEXT("Server trace cost %.1f us/shot"), GS->AvgTraceMicros), Dim });
	if (Char && Char->Weapon)
	{
		const FHLShotStats& St = Char->Weapon->GetStats();
		Lines.Add({ FString::Printf(TEXT("Shots %d   predicted hits %d   confirmed %d   retracted %d   rejected %d"), St.Fired, St.PredictedHits, St.ConfirmedHits, St.RetractedHits, St.Rejected), Dim });
		const FHLShotResult& R = Char->Weapon->GetLastResult();
		if (R.bHasTarget)
		{
			Lines.Add({ FString::Printf(TEXT("Last hit: rewound %.0f ms, target moved %.0f cm since"), R.RewindMs, FVector::Dist(R.TracedPose.Location, R.CurrentPose.Location)), Dim });
		}
	}
	Lines.Add({ FString::Printf(TEXT("Aim assist %s (%s)"), AimModeNames[int32(UHLAimAssistComponent::GetLocalMode(this))], PC->IsUsingGamepad() ? TEXT("gamepad") : TEXT("mouse: assist inactive")), Dim });
	if (PC->Peek && PC->Peek->GetRole() != EHLPeekRole::None)
	{
		Lines.Add({ PC->Peek->GetRole() == EHLPeekRole::Peeker ? TEXT("PEEK TEST: you are the peeker") : TEXT("PEEK TEST: you are the holder"), Warn });
	}
	Lines.Add({ TEXT("F1 overlay  F2 lag comp  F3 RTT  F4 tick  F5 interp  F6 assist  F7 rewind view  F8 save replay  F9 aim bench  F10 bot"), FLinearColor(0.7f, 0.7f, 0.7f, 0.8f) });

	DrawRect(Panel, X - 10.f * S, Y - 8.f * S, 860.f * S, Line * Lines.Num() + 12.f * S);
	for (const auto& L : Lines)
	{
		Text(L.Key, X, Y, L.Value, 0.8f);
		Y += Line;
	}
}

void AHLHUD::DrawAimAssistDebug(const AHLCharacter* Char)
{
	const AHLPlayerController* PC = Cast<AHLPlayerController>(Char->GetController());
	if (!Char->AimAssist || !PC || !PC->IsUsingGamepad() || !PC->bShowNetOverlay)
	{
		return;
	}
	const FHLAimOutput& Out = Char->AimAssist->GetLastOutput();
	const TArray<FHLAimTarget>& Targets = Char->AimAssist->GetLastTargets();
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		const FVector P = Canvas->Project(Targets[i].Point);
		if (P.Z <= 0.f)
		{
			continue;
		}
		const bool bSelected = i == Out.TargetIndex;
		const FLinearColor C = bSelected ? FLinearColor(0.4f, 0.9f, 1.f, 0.9f) : FLinearColor(0.4f, 0.9f, 1.f, 0.25f);
		// Bubble radius on screen: the bubble angle as a fraction of the half FOV
		const float Fov = Char->Camera ? Char->Camera->FieldOfView : 90.f;
		const float Dist = FVector::Dist(Targets[i].Point, Char->GetEyeLocation());
		const float BubbleDeg = FMath::RadiansToDegrees(FMath::Atan(Char->AimAssist->GetSettings()->SlowdownRadiusCm / FMath::Max(Dist, 1.f)));
		const float R = FMath::Tan(FMath::DegreesToRadians(BubbleDeg)) / FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f)) * (Canvas->ClipX * 0.5f);
		const int32 Segs = 24;
		for (int32 s = 0; s < Segs; ++s)
		{
			const float A0 = 2.f * UE_PI * s / Segs;
			const float A1 = 2.f * UE_PI * (s + 1) / Segs;
			DrawLine(P.X + R * FMath::Cos(A0), P.Y + R * FMath::Sin(A0), P.X + R * FMath::Cos(A1), P.Y + R * FMath::Sin(A1), C, bSelected ? 2.f : 1.f);
		}
		if (bSelected)
		{
			Text(FString::Printf(TEXT("slow x%.2f  pull %.1f/%.1f deg/s"), Out.SlowdownScale, Out.PullRate.X, Out.PullRate.Y), P.X + R + 6.f, P.Y - 8.f, C, 0.7f);
		}
	}
}

void AHLHUD::DrawKillFeed()
{
	const AHLGameState* GS = GetWorld()->GetGameState<AHLGameState>();
	if (!GS)
	{
		return;
	}
	const float S = Scale();
	float Y = 24.f * S;
	for (int32 i = GS->KillFeed.Num() - 1; i >= 0; --i)
	{
		const FHLKillFeedEntry& E = GS->KillFeed[i];
		const FString Line = FString::Printf(TEXT("%s  [%s%s]  %s"), *E.Killer, *E.Weapon, E.bHeadshot ? TEXT(", head") : TEXT(""), *E.Victim);
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, GEngine->GetMediumFont(), 0.8f * S);
		const float X = Canvas->ClipX - W - 30.f * S;
		DrawRect(Panel, X - 8.f * S, Y - 3.f * S, W + 16.f * S, H + 6.f * S);
		Text(Line, X, Y, White, 0.8f);
		Y += H + 10.f * S;
	}
}

void AHLHUD::DrawToast(AHLPlayerController* PC)
{
	const float Age = float(GetWorld()->GetTimeSeconds() - PC->ToastTime);
	if (PC->Toast.IsEmpty() || Age > 4.f)
	{
		return;
	}
	const float S = Scale();
	float W = 0.f, H = 0.f;
	GetTextSize(PC->Toast, W, H, GEngine->GetMediumFont(), 0.85f * S);
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY * 0.2f;
	const float A = FMath::Clamp(4.f - Age, 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f * A), X - 14.f * S, Y - 8.f * S, W + 28.f * S, H + 16.f * S);
	Text(PC->Toast, X, Y, FLinearColor(1.f, 1.f, 1.f, A), 0.85f);
}

void AHLHUD::DrawKillCam(AHLPlayerController* PC)
{
	const UHLReplayComponent* R = PC->Replay;
	const FHLReplayClip& Clip = R->GetClip();
	const float S = Scale();
	// Letterbox
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, 0.f, Canvas->ClipX, 70.f * S);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f, Canvas->ClipY - 110.f * S, Canvas->ClipX, 110.f * S);
	Text(FString::Printf(TEXT("KILL CAM   %s"), *R->GetKillerName()), 40.f * S, 20.f * S, Bad, 1.3f);
	Text(FString::Printf(TEXT("%.2f / %.2f s   x%.2g%s"), R->GetPlaybackTime(), Clip.Duration, R->GetSpeed(), R->IsPaused() ? TEXT("   PAUSED") : TEXT("")),
		Canvas->ClipX - 380.f * S, 24.f * S, White, 0.9f);

	// Timeline with the killing shot marked
	const float X0 = 60.f * S;
	const float X1 = Canvas->ClipX - 60.f * S;
	const float Y = Canvas->ClipY - 70.f * S;
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.25f), X0, Y, X1 - X0, 6.f * S);
	const float Played = Clip.Duration > 0.f ? R->GetPlaybackTime() / Clip.Duration : 0.f;
	DrawRect(White, X0, Y, (X1 - X0) * Played, 6.f * S);
	const float KillX = X0 + (X1 - X0) * (Clip.Duration > 0.f ? Clip.KillTime / Clip.Duration : 0.f);
	DrawRect(Bad, KillX - 2.f * S, Y - 8.f * S, 4.f * S, 22.f * S);
	for (const FHLReplayShot& Shot : Clip.Shots)
	{
		if (Shot.ShooterId == Clip.KillerId)
		{
			const float SX = X0 + (X1 - X0) * Shot.Time / FMath::Max(Clip.Duration, 0.01f);
			DrawRect(Warn, SX - 1.f, Y - 4.f * S, 2.f, 6.f * S);
		}
	}
	Text(TEXT("Space pause   Left/Right scrub   Up/Down speed   Enter skip"), X0, Y + 18.f * S, Dim, 0.75f);
}

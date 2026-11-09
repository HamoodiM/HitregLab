// Hitreg Lab: canvas HUD. Crosshair sized from the real spread cone, hit markers that show prediction vs
// server confirmation, the net/lab overlay, kill feed and kill cam timeline.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HLHUD.generated.h"

class AHLCharacter;
class AHLPlayerController;

UCLASS()
class HITREGLAB_API AHLHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCrosshair(const AHLCharacter* Char);
	void DrawHitMarker(const AHLCharacter* Char);
	void DrawWeaponPanel(const AHLCharacter* Char);
	void DrawNetOverlay(AHLPlayerController* PC, const AHLCharacter* Char);
	void DrawAimAssistDebug(const AHLCharacter* Char);
	void DrawKillFeed();
	void DrawToast(AHLPlayerController* PC);
	void DrawKillCam(AHLPlayerController* PC);
	void DrawAutopsy(AHLPlayerController* PC);
	void Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale = 1.f, bool bShadow = true);
	float Scale() const;
};

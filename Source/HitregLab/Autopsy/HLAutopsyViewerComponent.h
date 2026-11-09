// Hitreg Lab: Shot Autopsy viewer (local, on the PlayerController). Tab opens it.
//
// Pick any shot from this session (or from a loaded .hlreplay) and scrub it. The 3D view draws, side by side:
//   the shooter's ray;
//   blue  - the target where the SHOOTER'S screen showed it;
//   green - where the SERVER tested it (rewound, clamped by the cap or the cover rule);
//   red   - where the target REALLY was when the server decided;
//   pink  - where the VICTIM'S OWN screen had it when the hit landed (estimate: server track + its RTT);
//   a solid ghost at the scrub time, moving along the target's server track.
// Keys while open: Up/Down pick a shot, Left/Right scrub, Space play, F filter (all / mine / at me),
// mouse orbits, wheel zooms, Tab or Esc closes.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Autopsy/HLShotAutopsy.h"
#include "HLAutopsyViewerComponent.generated.h"

class ACameraActor;
class AHLReplayGhost;

UCLASS(ClassGroup = (HitregLab))
class HITREGLAB_API UHLAutopsyViewerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHLAutopsyViewerComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	enum class EFilter : uint8 { All, Mine, AtMe };

	void Toggle();
	void Open();
	void Close();
	bool IsOpen() const { return bOpen; }

	/** Loads the autopsy records saved in a .hlreplay and opens the viewer on them. Returns a status line. */
	FString LoadFile(const FString& Path);
	/** Opens on the newest registered shot that hit the local player (the "how did that hit me?" case). */
	void OpenOnLatestHitOnMe(bool bInAutoPlay, bool bInAutoOrbit);

	// --- For the HUD ---
	const TArray<FHLShotAutopsy>& GetShots() const { return Shots; }
	int32 GetSelectedIndex() const { return Selected; }
	const FHLShotAutopsy* GetSelectedShot() const { return Shots.IsValidIndex(Selected) ? &Shots[Selected] : nullptr; }
	float GetScrubMs() const { return ScrubMs; }
	bool IsPlaying() const { return bPlaying; }
	EFilter GetFilter() const { return Filter; }
	const TCHAR* GetFilterName() const;
	bool IsShowingFile() const { return bFile; }
	const FString& GetFileName() const { return FileName; }
	int32 GetLocalId() const;

	/** A moment on the shot's timeline (ms relative to when the server judged it), with its pose colour. */
	struct FMark
	{
		float Ms = 0.f;
		FString Label;
		FColor Color;
		FVector World = FVector::ZeroVector;
		bool bHasPose = false;
	};
	void GetMarks(const FHLShotAutopsy& Shot, TArray<FMark>& Out) const;

	static const FColor ViewColor;
	static const FColor TracedColor;
	static const FColor NowColor;
	static const FColor VictimColor;

private:
	void Refresh();
	void Select(int32 Index, bool bResetCamera);
	void HandleKeys(float DeltaTime);
	void UpdateCamera(float DeltaTime);
	void DrawWorld() const;
	void DrawPose(const FHLPose& Pose, const FColor& Color, float Thickness) const;
	bool PassesFilter(const FHLShotAutopsy& Shot) const;

	bool bOpen = false;
	bool bFile = false;
	FString FileName;
	TArray<FHLShotAutopsy> FileShots;
	/** The filtered list, newest first. */
	TArray<FHLShotAutopsy> Shots;
	int32 Selected = INDEX_NONE;
	uint32 SelectedShotId = 0;
	int32 SeenRevision = -1;
	EFilter Filter = EFilter::All;

	float ScrubMs = 0.f;
	bool bPlaying = false;
	float EndHold = 0.f;

	FVector Focus = FVector::ZeroVector;
	FVector FocusGoal = FVector::ZeroVector;
	float OrbitYaw = 0.f;
	float OrbitPitch = -22.f;
	float OrbitDist = 700.f;
	float BaseYaw = 0.f;
	bool bAutoOrbit = false;
	float AutoOrbitTime = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<AHLReplayGhost> Ghost;
};

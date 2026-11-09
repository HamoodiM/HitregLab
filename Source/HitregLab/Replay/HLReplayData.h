// Hitreg Lab: kill cam / replay clip format. Pure data: quantized poses per player at 30 Hz plus shot events,
// serializable to a file (bug repro) and hashable (round-trip tests).
#pragma once

#include "CoreMinimal.h"
#include "Core/HLTypes.h"
#include "Replay/HLQuantPose.h"
#include "Autopsy/HLShotAutopsy.h"


struct HITREGLAB_API FHLReplayKey
{
	float Time = 0.f; // seconds from the clip start
	FHLQuantPose Pose;
	bool bAlive = true;

	friend FArchive& operator<<(FArchive& Ar, FHLReplayKey& K);
};

struct HITREGLAB_API FHLReplayTrack
{
	int32 PlayerId = 0;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	TArray<FHLReplayKey> Keys;

	/** Interpolated pose at a clip time (holds the first/last key outside the range). */
	bool Sample(float Time, FHLPose& OutPose, bool& bOutAlive) const;

	friend FArchive& operator<<(FArchive& Ar, FHLReplayTrack& T);
};

struct HITREGLAB_API FHLReplayShot
{
	float Time = 0.f;
	int32 ShooterId = 0;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	bool bHit = false;

	friend FArchive& operator<<(FArchive& Ar, FHLReplayShot& S);
};

struct HITREGLAB_API FHLReplayClip
{
	static constexpr uint32 Magic = 0x484C5250; // "HLRP"
	/** 1: tracks and shots. 2: + Shot Autopsy records. Version 1 files still load (with no autopsies). */
	static constexpr int32 Version = 2;

	float Duration = 0.f;
	int32 KillerId = INDEX_NONE;
	int32 VictimId = INDEX_NONE;
	/** Clip time of the killing shot. */
	float KillTime = 0.f;
	TArray<FHLReplayTrack> Tracks;
	TArray<FHLReplayShot> Shots;
	/** Server verdicts for the shots this machine fired or was shot by (version 2+). */
	TArray<FHLShotAutopsy> Autopsies;

	const FHLReplayTrack* FindTrack(int32 PlayerId) const;
	bool IsValid() const { return Tracks.Num() > 0 && Duration > 0.f; }

	/** Serializes with a magic number and version; loading a newer version fails cleanly. Returns false on bad data. */
	bool Serialize(FArchive& Ar, int32 WriteVersion = Version);
	bool SaveToFile(const FString& Path) const;
	bool LoadFromFile(const FString& Path);
	/** WriteVersion < Version writes an older layout (tests use it to check old files still load). */
	TArray<uint8> ToBytes(int32 WriteVersion = Version) const;
	bool FromBytes(const TArray<uint8>& Bytes);

	/** Hash of the quantized content (equal clips hash equal). */
	uint32 ContentHash() const;

	/** Serialized bytes per second per player track. */
	float BytesPerSecondPerPlayer() const;
};

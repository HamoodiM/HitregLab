// Hitreg Lab: kill cam / replay clip format. Pure data: quantized poses per player at 30 Hz plus shot events,
// serializable to a file (bug repro) and hashable (round-trip tests).
#pragma once

#include "CoreMinimal.h"
#include "Core/HLTypes.h"

/** A pose packed into 16 bytes: position in millimetres, yaw in 1/65536 turns, pitch in 1/32767 of 90 degrees. */
struct HITREGLAB_API FHLQuantPose
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;
	uint16 Yaw = 0;
	int16 Pitch = 0;

	static FHLQuantPose Quantize(const FHLPose& Pose);
	FHLPose Dequantize() const;
	bool operator==(const FHLQuantPose& O) const { return X == O.X && Y == O.Y && Z == O.Z && Yaw == O.Yaw && Pitch == O.Pitch; }
	friend FArchive& operator<<(FArchive& Ar, FHLQuantPose& P);
};

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
	static constexpr int32 Version = 1;

	float Duration = 0.f;
	int32 KillerId = INDEX_NONE;
	int32 VictimId = INDEX_NONE;
	/** Clip time of the killing shot. */
	float KillTime = 0.f;
	TArray<FHLReplayTrack> Tracks;
	TArray<FHLReplayShot> Shots;

	const FHLReplayTrack* FindTrack(int32 PlayerId) const;
	bool IsValid() const { return Tracks.Num() > 0 && Duration > 0.f; }

	/** Serializes with a magic number and version; loading a newer version fails cleanly. Returns false on bad data. */
	bool Serialize(FArchive& Ar);
	bool SaveToFile(const FString& Path) const;
	bool LoadFromFile(const FString& Path);
	TArray<uint8> ToBytes() const;
	bool FromBytes(const TArray<uint8>& Bytes);

	/** Hash of the quantized content (equal clips hash equal). */
	uint32 ContentHash() const;

	/** Serialized bytes per second per player track. */
	float BytesPerSecondPerPlayer() const;
};

// Hitreg Lab: a pose packed into 16 bytes, shared by the kill cam recording and the shot autopsy records.
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

// Hitreg Lab: time-stamped pose ring buffer. The server keeps one per character for lag compensation,
// clients keep one per remote character as their interpolation buffer, and the kill cam records into them.
// Pure, no world access.
#pragma once

#include "CoreMinimal.h"
#include "Core/HLTypes.h"

class HITREGLAB_API FHLPoseHistory
{
public:
	explicit FHLPoseHistory(int32 InCapacity = 128);

	void Reset();

	/** Adds a pose. Times must not go backwards (older samples are dropped); the same time replaces the newest. */
	void Push(double Time, const FHLPose& Pose);

	/**
	 * Pose at Time, interpolated between the two stored samples around it.
	 * Outside the stored range it clamps to the oldest/newest sample and returns false.
	 */
	bool Sample(double Time, FHLPose& OutPose) const;

	int32 Num() const { return Count; }
	int32 Capacity() const { return Times.Num(); }
	double OldestTime() const;
	double NewestTime() const;
	const FHLPose& Newest() const;

	/** i = 0 is the oldest stored sample. */
	double TimeAt(int32 i) const { return Times[Slot(i)]; }
	const FHLPose& PoseAt(int32 i) const { return Poses[Slot(i)]; }

private:
	int32 Slot(int32 i) const { return (Head - Count + i + Times.Num()) % Times.Num(); }

	TArray<double> Times;
	TArray<FHLPose> Poses;
	int32 Head = 0; // next write slot
	int32 Count = 0;
};

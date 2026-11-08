// Hitreg Lab: pose ring buffer
#include "Net/HLPoseHistory.h"

FHLPoseHistory::FHLPoseHistory(int32 InCapacity)
{
	const int32 Cap = FMath::Max(2, InCapacity);
	Times.SetNumZeroed(Cap);
	Poses.SetNum(Cap);
}

void FHLPoseHistory::Reset()
{
	Head = 0;
	Count = 0;
}

void FHLPoseHistory::Push(double Time, const FHLPose& Pose)
{
	if (Count > 0)
	{
		const int32 NewestSlot = Slot(Count - 1);
		if (Time < Times[NewestSlot])
		{
			return; // out of order (a late snapshot): drop it
		}
		if (Time == Times[NewestSlot])
		{
			Poses[NewestSlot] = Pose;
			return;
		}
	}
	Times[Head] = Time;
	Poses[Head] = Pose;
	Head = (Head + 1) % Times.Num();
	Count = FMath::Min(Count + 1, Times.Num());
}

double FHLPoseHistory::OldestTime() const
{
	return Count > 0 ? Times[Slot(0)] : 0.0;
}

double FHLPoseHistory::NewestTime() const
{
	return Count > 0 ? Times[Slot(Count - 1)] : 0.0;
}

const FHLPose& FHLPoseHistory::Newest() const
{
	check(Count > 0);
	return Poses[Slot(Count - 1)];
}

bool FHLPoseHistory::Sample(double Time, FHLPose& OutPose) const
{
	if (Count == 0)
	{
		return false;
	}
	if (Time <= Times[Slot(0)])
	{
		OutPose = Poses[Slot(0)];
		return Time == Times[Slot(0)];
	}
	if (Time >= Times[Slot(Count - 1)])
	{
		OutPose = Poses[Slot(Count - 1)];
		return Time == Times[Slot(Count - 1)];
	}
	// Binary search for the pair of samples around Time
	int32 Lo = 0;
	int32 Hi = Count - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Times[Slot(Mid)] <= Time)
		{
			Lo = Mid;
		}
		else
		{
			Hi = Mid;
		}
	}
	const double T0 = Times[Slot(Lo)];
	const double T1 = Times[Slot(Hi)];
	const float Alpha = float((Time - T0) / FMath::Max(T1 - T0, 1e-9));
	OutPose = FHLPose::Lerp(Poses[Slot(Lo)], Poses[Slot(Hi)], Alpha);
	return true;
}

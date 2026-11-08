// Hitreg Lab: shared types
#include "Core/HLTypes.h"

FHLPose FHLPose::Lerp(const FHLPose& A, const FHLPose& B, float Alpha)
{
	FHLPose Out;
	Out.Location = FMath::Lerp(A.Location, B.Location, Alpha);
	// Shortest way round for angles, so 179 -> -179 doesn't spin the long way
	Out.Yaw = FRotator::NormalizeAxis(A.Yaw + FRotator::NormalizeAxis(B.Yaw - A.Yaw) * Alpha);
	Out.Pitch = A.Pitch + FRotator::NormalizeAxis(B.Pitch - A.Pitch) * Alpha;
	return Out;
}

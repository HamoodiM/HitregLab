// Hitreg Lab: rewind rules
#include "Net/HLRewindRules.h"

FHLRewindDecision HLRewindRules::Resolve(const FHLRewindInput& In)
{
	FHLRewindDecision Out;
	if (!In.bLagCompensation)
	{
		Out.TraceTime = In.Now; // no rewind: the target is tested where it is now
		return Out;
	}
	// A view time in the future (clock error) is traced now; an old one is traced at the cap
	Out.TraceTime = FMath::Min(In.ViewTime, In.Now);
	const double Oldest = In.Now - FMath::Max(In.MaxRewindSeconds, 0.0);
	if (Out.TraceTime < Oldest)
	{
		Out.TraceTime = Oldest;
		Out.Clamp = EHLRewindClamp::Cap;
	}
	if (In.bCoverRule && In.bTargetInCoverNow)
	{
		const double CoverOldest = In.Now - FMath::Max(In.CoverBudgetSeconds, 0.0);
		if (Out.TraceTime < CoverOldest)
		{
			Out.TraceTime = CoverOldest;
			Out.Clamp = EHLRewindClamp::CoverRule;
		}
	}
	return Out;
}

bool HLRewindRules::NeedsCoverTest(const FHLRewindInput& In)
{
	if (!In.bLagCompensation || !In.bCoverRule)
	{
		return false;
	}
	const double Rewind = In.Now - FMath::Max(FMath::Min(In.ViewTime, In.Now), In.Now - In.MaxRewindSeconds);
	return Rewind > In.CoverBudgetSeconds;
}

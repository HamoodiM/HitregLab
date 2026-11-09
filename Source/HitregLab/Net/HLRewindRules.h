// Hitreg Lab: how far back the server may rewind a target for one shot. Pure functions, no world access.
//
// Classic rule: rewind to the shooter's view time, but never more than MaxRewind (the cap).
// Cover rule (optional): if the target is ALREADY in cover from the shooter at the moment the server judges
// the shot, the rewind is limited further, to CoverBudget. A low-ping shooter (view age under the budget) is
// never affected; a high-ping shooter loses the last part of their compensation against a target that has
// already reached cover. That moves a little of the latency cost back from the victim to the shooter.
#pragma once

#include "CoreMinimal.h"
#include "HLRewindRules.generated.h"

/** Why the server traced a shot later than the shooter's view time. */
UENUM()
enum class EHLRewindClamp : uint8
{
	None,
	/** The view was older than the rewind cap: traced at the cap. */
	Cap,
	/** The target was already in cover and the view was older than the cover budget: traced at the budget. */
	CoverRule
};

struct FHLRewindInput
{
	/** Server time now (when the shot is judged). */
	double Now = 0.0;
	/** Server time the shooter's screen was showing the target at. */
	double ViewTime = 0.0;
	bool bLagCompensation = true;
	double MaxRewindSeconds = 0.2;
	bool bCoverRule = false;
	double CoverBudgetSeconds = 0.1;
	/** Target not visible from the shooter's eye at its CURRENT server position (head and torso both blocked). */
	bool bTargetInCoverNow = false;
};

struct FHLRewindDecision
{
	double TraceTime = 0.0;
	EHLRewindClamp Clamp = EHLRewindClamp::None;
};

namespace HLRewindRules
{
	/** Where to test the target's hitboxes for this shot. */
	HITREGLAB_API FHLRewindDecision Resolve(const FHLRewindInput& In);

	/** True when the cover rule could change the answer, i.e. the (costly) cover line traces are worth doing. */
	HITREGLAB_API bool NeedsCoverTest(const FHLRewindInput& In);
}

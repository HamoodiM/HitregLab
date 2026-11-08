// Hitreg Lab: aim assist tests (stick curve, the no-input rule, and the bot-thumb benchmark)
#include "Misc/AutomationTest.h"
#include "Aim/HLAimAssist.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLStickCurveTest, "HitregLab.Aim.StickCurve", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLStickCurveTest::RunTest(const FString& Parameters)
{
	UHLAimAssistSettings* S = NewObject<UHLAimAssistSettings>();
	TestTrue(TEXT("inside the dead zone reads zero"), HLAimAssist::ApplyStickCurve(*S, FVector2D(S->InnerDeadzone * 0.9f, 0.f)).IsNearlyZero());
	TestEqual(TEXT("past the outer dead zone reads full"), float(HLAimAssist::ApplyStickCurve(*S, FVector2D(0.99f, 0.f)).X), 1.f, 1e-4f);
	float Prev = -1.f;
	bool bMonotonic = true;
	for (int32 i = 0; i <= 100; ++i)
	{
		const float V = float(HLAimAssist::ApplyStickCurve(*S, FVector2D(i / 100.f, 0.f)).X);
		bMonotonic &= V >= Prev - 1e-6f;
		Prev = V;
	}
	TestTrue(TEXT("response is monotonic"), bMonotonic);
	const FVector2D Diag = HLAimAssist::ApplyStickCurve(*S, FVector2D(0.5f, 0.5f));
	TestEqual(TEXT("radial dead zone keeps the direction"), float(Diag.X), float(Diag.Y), 1e-5f);
	TestTrue(TEXT("negative deflection is symmetric"), HLAimAssist::ApplyStickCurve(*S, FVector2D(-0.6f, 0.f)).Equals(-HLAimAssist::ApplyStickCurve(*S, FVector2D(0.6f, 0.f))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLNoInputNoPullTest, "HitregLab.Aim.NoInputNoPull", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLNoInputNoPullTest::RunTest(const FString& Parameters)
{
	UHLAimAssistSettings* S = NewObject<UHLAimAssistSettings>();
	FHLAimTarget T;
	T.Point = FVector(1500.f, 30.f, -40.f);
	T.Head = FVector(1500.f, 30.f, 2.f);
	T.Velocity = FVector(0.f, 500.f, 0.f);
	FHLAimInput In; // no look, no move input
	const FHLAimOutput Idle = HLAimAssist::Compute(*S, EHLAimAssistMode::Full, In, MakeArrayView(&T, 1));
	TestTrue(TEXT("target found"), Idle.TargetIndex == 0);
	TestTrue(TEXT("no input -> no pull"), Idle.PullRate.IsNearlyZero());

	In.Look = FVector2D(0.3f, 0.f);
	const FHLAimOutput Active = HLAimAssist::Compute(*S, EHLAimAssistMode::Full, In, MakeArrayView(&T, 1));
	TestTrue(TEXT("input -> pull toward the moving target"), Active.PullRate.X > 0.f);
	TestTrue(TEXT("slowdown near the target"), Active.SlowdownScale < 1.f);
	TestTrue(TEXT("pull is capped"), FMath::Abs(Active.PullRate.X) <= S->PullMaxDegPerSec + 1e-3f);

	const FHLAimOutput Off = HLAimAssist::Compute(*S, EHLAimAssistMode::Off, In, MakeArrayView(&T, 1));
	TestTrue(TEXT("mode off does nothing"), Off.SlowdownScale == 1.f && Off.PullRate.IsNearlyZero());
	const FHLAimOutput SlowOnly = HLAimAssist::Compute(*S, EHLAimAssistMode::SlowdownOnly, In, MakeArrayView(&T, 1));
	TestTrue(TEXT("slowdown-only never pulls"), SlowOnly.PullRate.IsNearlyZero() && SlowOnly.SlowdownScale < 1.f);

	// Bullet magnetism bends only inside its tiny cone
	const FVector Near = (T.Head + FVector(0.f, 6.f, 0.f)).GetSafeNormal();
	const FVector Far = (T.Head + FVector(0.f, 200.f, 0.f)).GetSafeNormal();
	TestTrue(TEXT("near miss bent onto the head"), HLAimAssist::ApplyBulletMagnetism(*S, EHLAimAssistMode::Full, FVector::ZeroVector, Near, MakeArrayView(&T, 1), 1.f).Equals(T.Head.GetSafeNormal(), 1e-4f));
	TestTrue(TEXT("wide miss left alone"), HLAimAssist::ApplyBulletMagnetism(*S, EHLAimAssistMode::Full, FVector::ZeroVector, Far, MakeArrayView(&T, 1), 1.f).Equals(Far));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLAcquireTest, "HitregLab.Aim.Acquire", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLAcquireTest::RunTest(const FString& Parameters)
{
	UHLAimAssistSettings* S = NewObject<UHLAimAssistSettings>();
	const FHLAimBenchResult Off = HLAimBench::RunMedian(*S, EHLAimAssistMode::Off, 50);
	const FHLAimBenchResult Full = HLAimBench::RunMedian(*S, EHLAimAssistMode::Full, 50);
	AddInfo(FString::Printf(TEXT("Median time-to-acquire: off %.0f ms, full %.0f ms; overshoot off %.2f, full %.2f deg; tracking off %.0f%%, full %.0f%%"),
		Off.TimeToAcquire * 1000.f, Full.TimeToAcquire * 1000.f, Off.OvershootDeg, Full.OvershootDeg, Off.TrackingFraction * 100.f, Full.TrackingFraction * 100.f));
	TestTrue(TEXT("full assist acquires faster"), Full.TimeToAcquire < Off.TimeToAcquire);
	TestTrue(TEXT("assist doesn't play the game: tracking under 90%"), Full.TrackingFraction < 0.9f);
	TestEqual(TEXT("no rotation while the thumb is idle"), Full.IdleRotationDeg, 0.f, 1e-4f);
	return true;
}

#endif

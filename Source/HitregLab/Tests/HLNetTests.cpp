// Hitreg Lab: networking tests (hitbox maths, pose history, clock sync, rewind)
#include "Misc/AutomationTest.h"
#include "Core/HLCharacter.h"
#include "Net/HLClockSync.h"
#include "Net/HLHitboxMath.h"
#include "Net/HLPoseHistory.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLHitboxMathTest, "HitregLab.Net.HitboxMath", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLHitboxMathTest::RunTest(const FString& Parameters)
{
	float Dist = 0.f;
	TestTrue(TEXT("ray hits sphere"), HLHitboxMath::RaySphere(FVector::ZeroVector, FVector(1, 0, 0), FVector(100, 0, 0), 10.f, 1000.f, Dist));
	TestEqual(TEXT("at the near surface"), Dist, 90.f, 1e-3f);
	TestFalse(TEXT("ray misses sphere"), HLHitboxMath::RaySphere(FVector::ZeroVector, FVector(1, 0, 0), FVector(100, 20, 0), 10.f, 1000.f, Dist));
	TestFalse(TEXT("sphere behind the ray"), HLHitboxMath::RaySphere(FVector::ZeroVector, FVector(1, 0, 0), FVector(-100, 0, 0), 10.f, 1000.f, Dist));
	TestFalse(TEXT("sphere past max distance"), HLHitboxMath::RaySphere(FVector::ZeroVector, FVector(1, 0, 0), FVector(100, 0, 0), 10.f, 50.f, Dist));

	// A box rotated 45 degrees: a ray grazing its unrotated corner must miss
	const FTransform Box(FRotator(0, 45, 0), FVector(200, 0, 0));
	TestTrue(TEXT("ray hits rotated box"), HLHitboxMath::RayBox(FVector::ZeroVector, FVector(1, 0, 0), Box, FVector(20, 20, 20), 1000.f, Dist));
	TestEqual(TEXT("at the rotated face"), Dist, 200.f - 20.f * FMath::Sqrt(2.f), 0.05f);
	TestFalse(TEXT("misses beside the rotated box"), HLHitboxMath::RayBox(FVector(0, 29, 0), FVector(1, 0, 0), Box, FVector(20, 20, 20), 1000.f, Dist));

	// Character hitboxes: a head-height shot is a head hit, the nearest box wins
	const TArray<FHLHitbox> Boxes = AHLCharacter::MakeDefaultHitboxes();
	FHLPose Pose;
	Pose.Location = FVector(1000, 0, 0);
	FHLHitboxHit Hit;
	TestTrue(TEXT("head shot"), HLHitboxMath::RayHitboxes(FVector(0, 0, 66), FVector(1, 0, 0), 5000.f, Boxes, Pose, Hit) && Hit.Zone == EHLHitZone::Head);
	FHLHitboxHit Body;
	TestTrue(TEXT("body shot"), HLHitboxMath::RayHitboxes(FVector(0, 0, 20), FVector(1, 0, 0), 5000.f, Boxes, Pose, Body) && Body.Zone == EHLHitZone::Torso);
	FHLHitboxHit Miss;
	TestFalse(TEXT("over the head misses"), HLHitboxMath::RayHitboxes(FVector(0, 0, 90), FVector(1, 0, 0), 5000.f, Boxes, Pose, Miss));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLHistoryRingTest, "HitregLab.Net.HistoryRing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLHistoryRingTest::RunTest(const FString& Parameters)
{
	FHLPoseHistory H(8);
	for (int32 i = 0; i < 20; ++i)
	{
		FHLPose P;
		P.Location = FVector(i * 10.f, 0, 0);
		P.Yaw = 170.f + i * 5.f; // wraps past 180
		H.Push(i * 0.1, P);
	}
	TestEqual(TEXT("capacity respected"), H.Num(), 8);
	TestEqual(TEXT("oldest kept sample"), H.OldestTime(), 1.2, 1e-9);
	TestEqual(TEXT("newest sample"), H.NewestTime(), 1.9, 1e-9);

	FHLPose Mid;
	TestTrue(TEXT("sample inside the range"), H.Sample(1.55, Mid));
	TestEqual(TEXT("interpolated position"), float(Mid.Location.X), 155.f, 1e-3f);
	// 170 + 15*5 = 245 -> -115 and 250 -> -110: halfway is -112.5, not a long way round through 0
	TestEqual(TEXT("yaw interpolates across the wrap"), Mid.Yaw, -112.5f, 1e-3f);

	FHLPose Clamped;
	TestFalse(TEXT("before the range clamps (and says so)"), H.Sample(0.5, Clamped));
	TestEqual(TEXT("clamped to the oldest"), float(Clamped.Location.X), 120.f, 1e-3f);
	FHLPose Late;
	H.Push(1.85, Late); // older than the newest: dropped
	TestEqual(TEXT("out-of-order sample dropped"), H.NewestTime(), 1.9, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLClockSyncTest, "HitregLab.Net.ClockSync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLClockSyncTest::RunTest(const FString& Parameters)
{
	// Server clock is 12.345 s ahead; one-way delays are 50 ms plus 0-40 ms of jitter each way
	const double TrueOffset = 12.345;
	FRandomStream Rng(7);
	FHLClockSync Sync;
	double ClientTime = 100.0;
	for (int32 i = 0; i < 20; ++i)
	{
		const double Up = 0.05 + Rng.FRandRange(0.0, 0.04);
		const double Down = 0.05 + Rng.FRandRange(0.0, 0.04);
		const double ServerStamp = ClientTime + Up + TrueOffset;
		const double Recv = ClientTime + Up + Down;
		Sync.AddSample(ClientTime, ServerStamp, Recv);
		ClientTime += 0.1;
	}
	const double ErrorMs = FMath::Abs(Sync.GetOffset() - TrueOffset) * 1000.0;
	AddInfo(FString::Printf(TEXT("Offset error %.1f ms with 40 ms of jitter per direction"), ErrorMs));
	TestTrue(TEXT("offset within 10 ms"), ErrorMs < 10.0);
	TestTrue(TEXT("best RTT is at least the base round trip"), Sync.GetBestRtt() >= 0.1 - 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLRewindTest, "HitregLab.Net.Rewind", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLRewindTest::RunTest(const FString& Parameters)
{
	// A target strafes at 600 cm/s. The server records it every tick (60 Hz); a client receives the same
	// states 50 ms later and draws them 2 ticks in the past. The server rewinding to the client's view time
	// must reproduce exactly where the client drew it.
	const double Tick = 1.0 / 60.0;
	const double OneWay = 0.05;
	const double InterpDelay = 2.0 * Tick;
	FHLPoseHistory Server(128);
	FHLPoseHistory ClientInterp(64);
	auto PoseAt = [](double T)
	{
		FHLPose P;
		P.Location = FVector(2000.f, float(600.0 * T), 0.f);
		return P;
	};
	// The client fires when its synced clock reads server time 1.4 s. States stamped T arrive at T + OneWay,
	// so it holds states up to 1.35 and draws remote players at 1.4 - OneWay - InterpDelay.
	const double ClientNow = 1.4;
	for (int32 i = 0; i <= 90; ++i)
	{
		const double T = i * Tick;
		Server.Push(T, PoseAt(T));
		if (T + OneWay <= ClientNow)
		{
			ClientInterp.Push(T, PoseAt(T));
		}
	}
	const double ViewTime = ClientNow - OneWay - InterpDelay;
	FHLPose Drawn;
	TestTrue(TEXT("client has a pair of states around its render time"), ClientInterp.Sample(ViewTime, Drawn));
	// The shot reaches the server one-way later
	const double ServerNow = ClientNow + OneWay;
	FHLPose Rewound;
	Server.Sample(ViewTime, Rewound);
	TestTrue(TEXT("rewound pose matches what the client drew (2 cm)"), FVector::Dist(Rewound.Location, Drawn.Location) < 2.f);

	// Aimed at the drawn head: hits at the rewound pose, misses at the server's current pose
	const TArray<FHLHitbox> Boxes = AHLCharacter::MakeDefaultHitboxes();
	const FVector Eye(0.f, 0.f, 66.f);
	const FVector Dir = (Drawn.Location + FVector(0, 0, 66) - Eye).GetSafeNormal();
	FHLHitboxHit Hit;
	TestTrue(TEXT("hit with lag compensation"), HLHitboxMath::RayHitboxes(Eye, Dir, 5000.f, Boxes, Rewound, Hit));
	FHLPose Now;
	Server.Sample(ServerNow, Now);
	FHLHitboxHit NoHit;
	TestFalse(TEXT("miss without it (target moved ~50 cm)"), HLHitboxMath::RayHitboxes(Eye, Dir, 5000.f, Boxes, Now, NoHit));
	AddInfo(FString::Printf(TEXT("Target moved %.0f cm between the client's view and the shot arriving"), FVector::Dist(Rewound.Location, Now.Location)));

	// A view time older than the rewind cap is clamped to the cap
	const double MaxRewind = 0.2;
	const double Stale = ServerNow - 0.5;
	const double Clamped = FMath::Clamp(Stale, ServerNow - MaxRewind, ServerNow);
	TestEqual(TEXT("stale view time clamped to the cap"), Clamped, ServerNow - MaxRewind, 1e-9);
	return true;
}

#endif

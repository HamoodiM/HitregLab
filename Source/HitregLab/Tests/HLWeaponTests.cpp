// Hitreg Lab: weapon tests (fire timing, spread, falloff, server rate limit)
#include "Misc/AutomationTest.h"
#include "Weapons/HLWeaponDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 CountShots(double Fps, double Seconds, double Interval)
	{
		FHLFireClock Clock;
		TArray<double> Shots;
		const double Dt = 1.0 / Fps;
		for (double T = 0.0; T <= Seconds + 1e-9; T += Dt)
		{
			Clock.Advance(T, true, Interval, true, Shots);
		}
		return Shots.Num();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLFireRateTest, "HitregLab.Weapons.FireRate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLFireRateTest::RunTest(const FString& Parameters)
{
	// 600 RPM for one second: 11 shots (t = 0, 0.1, ... 1.0) whatever the frame rate
	const double Interval = 0.1;
	const int32 At30 = CountShots(30.0, 1.0, Interval);
	const int32 At60 = CountShots(60.0, 1.0, Interval);
	const int32 At144 = CountShots(144.0, 1.0, Interval);
	TestTrue(TEXT("60 vs 144 fps within one shot"), FMath::Abs(At60 - At144) <= 1);
	TestTrue(TEXT("30 vs 144 fps within one shot"), FMath::Abs(At30 - At144) <= 1);
	TestTrue(TEXT("about 11 shots in a second"), FMath::Abs(At144 - 11) <= 1);

	// Shot times are the true schedule, not the frame times
	FHLFireClock Clock;
	TArray<double> Shots;
	Clock.Advance(0.0, true, 0.05, true, Shots);
	Clock.Advance(0.033, true, 0.05, true, Shots);
	Clock.Advance(0.12, true, 0.05, true, Shots);
	TestEqual(TEXT("three shots due by 0.12 s"), Shots.Num(), 3);
	if (Shots.Num() == 3)
	{
		TestEqual(TEXT("second shot at 0.05"), Shots[1], 0.05, 1e-9);
		TestEqual(TEXT("third shot at 0.10"), Shots[2], 0.10, 1e-9);
	}

	// Semi-auto: one shot per press, and no banked shots after idling
	FHLFireClock Semi;
	TArray<double> SemiShots;
	Semi.Advance(0.0, true, 0.25, false, SemiShots);
	Semi.Advance(0.5, true, 0.25, false, SemiShots);   // still held: no new shot
	Semi.Advance(0.6, false, 0.25, false, SemiShots);
	Semi.Advance(0.7, true, 0.25, false, SemiShots);   // new press
	TestEqual(TEXT("semi fires once per press"), SemiShots.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLSpreadTest, "HitregLab.Weapons.Spread", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLSpreadTest::RunTest(const FString& Parameters)
{
	const FVector Forward(1.f, 0.f, 0.f);
	const float HalfAngle = 2.f;
	FVector Mean = FVector::ZeroVector;
	bool bAllInside = true;
	for (int32 Seed = 0; Seed < 2000; ++Seed)
	{
		const FVector D = FHLSpreadModel::SampleCone(Forward, HalfAngle, Seed);
		const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(D, Forward), -1.f, 1.f)));
		bAllInside &= Angle <= HalfAngle + 1e-3f;
		Mean += D;
	}
	Mean /= 2000.f;
	TestTrue(TEXT("every sample inside the cone"), bAllInside);
	TestTrue(TEXT("samples centred on the aim"), FMath::Abs(Mean.Y) < 0.002f && FMath::Abs(Mean.Z) < 0.002f);
	TestTrue(TEXT("same seed, same direction"), FHLSpreadModel::SampleCone(Forward, HalfAngle, 77).Equals(FHLSpreadModel::SampleCone(Forward, HalfAngle, 77)));

	UHLWeaponDefinition* Rifle = UHLWeaponDefinition::MakeTransient(nullptr, TEXT("Rifle"));
	FHLSpreadModel Model;
	const float Base = Model.Current(*Rifle, 0.f, false, false);
	for (int32 i = 0; i < 5; ++i)
	{
		Model.OnShot(*Rifle);
	}
	TestTrue(TEXT("bloom grows while firing"), Model.Current(*Rifle, 0.f, false, false) > Base);
	TestTrue(TEXT("ADS tightens spread"), Model.Current(*Rifle, 0.f, false, true) < Model.Current(*Rifle, 0.f, false, false));
	Model.Recover(*Rifle, 5.f);
	TestEqual(TEXT("bloom recovers fully"), Model.Current(*Rifle, 0.f, false, false), Base, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLFalloffTest, "HitregLab.Weapons.Falloff", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLFalloffTest::RunTest(const FString& Parameters)
{
	UHLWeaponDefinition* W = UHLWeaponDefinition::MakeTransient(nullptr, TEXT("Rifle"));
	W->Damage = 30.f;
	W->FalloffStart = 2000.f;
	W->FalloffEnd = 4000.f;
	W->MinDamageScale = 0.5f;
	TestEqual(TEXT("10 m: full damage"), W->DamageAtDistance(1000.f), 30.f, 1e-4f);
	TestEqual(TEXT("30 m: halfway down the falloff"), W->DamageAtDistance(3000.f), 22.5f, 1e-4f);
	TestEqual(TEXT("50 m: floor"), W->DamageAtDistance(5000.f), 15.f, 1e-4f);
	TestEqual(TEXT("recoil past the pattern repeats the last kick"), W->RecoilForShot(99), W->RecoilPattern.Last());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLRateCheatTest, "HitregLab.Net.FireRateCheat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLRateCheatTest::RunTest(const FString& Parameters)
{
	// Honest client at 10 shots/s, with packets arriving in bursts of three: never rejected
	FHLRateLimiter Honest;
	int32 HonestRejected = 0;
	for (int32 Burst = 0; Burst < 20; ++Burst)
	{
		const double T = Burst * 0.3;
		for (int32 i = 0; i < 3; ++i)
		{
			HonestRejected += Honest.TryConsume(T, 10.f) ? 0 : 1;
		}
	}
	TestEqual(TEXT("bunched-up honest shots accepted"), HonestRejected, 0);

	// Modified client firing at double rate for two seconds: about half rejected
	FHLRateLimiter Cheat;
	int32 Accepted = 0;
	for (int32 i = 0; i < 40; ++i)
	{
		Accepted += Cheat.TryConsume(i * 0.05, 10.f) ? 1 : 0;
	}
	TestTrue(TEXT("double-rate client capped near the real rate"), Accepted <= 26 && Accepted >= 20);
	return true;
}

#endif

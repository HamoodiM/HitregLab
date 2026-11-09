// Hitreg Lab: Shot Autopsy and cover rule tests (verdict classification, record and file round trips,
// the rewind rule maths)
#include "Misc/AutomationTest.h"
#include "Autopsy/HLShotAutopsy.h"
#include "Core/HLCharacter.h"
#include "Net/HLHitboxMath.h"
#include "Net/HLRewindRules.h"
#include "Replay/HLReplayData.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FHLShotAutopsy MakeRecord(uint32 Id, EHLShotVerdict Verdict)
	{
		FHLShotAutopsy R;
		R.ShotId = Id;
		R.ShotSeq = uint16(Id * 3);
		R.ShooterId = 257;
		R.TargetId = 258;
		R.Verdict = Verdict;
		R.Clamp = EHLRewindClamp::Cap;
		R.Zone = EHLHitZone::Torso;
		R.SetFlag(FHLShotAutopsy::FlagLagComp, true);
		R.SetFlag(FHLShotAutopsy::FlagTargetInCover, Id % 2 == 0);
		R.WeaponIndex = 1;
		R.ServerTime = 123.456789 + Id;
		R.ViewAgeMs = 233.f;
		R.RewindMs = 200.f;
		R.MaxRewindMs = 200.f;
		R.CoverBudgetMs = 100.f;
		R.ShooterRttMs = 200.f;
		R.TargetRttMs = 180.f;
		R.Damage = 25.f;
		R.WorldBlockCm = 1043.5f;
		R.HitCm = 980.25f;
		FHLPose P;
		P.Location = FVector(300.f, 800.f, 152.f);
		P.Yaw = -104.3f;
		P.Pitch = -3.2f;
		R.Shot = FHLQuantPose::Quantize(P);
		for (int32 i = 0; i < 3; ++i)
		{
			FHLPose T;
			T.Location = FVector(40.f - 50.f * i, -150.f, 88.f);
			T.Yaw = 70.f;
			(i == 0 ? R.ViewPose : i == 1 ? R.TracedPose : R.CurrentPose) = FHLQuantPose::Quantize(T);
		}
		R.TrackStartMs = -283.f;
		R.TrackStepMs = 25.f;
		for (int32 i = 0; i < 20; ++i)
		{
			FHLPose T;
			T.Location = FVector(60.f - 15.f * i, -150.f, 88.f);
			T.Yaw = 70.f;
			R.Track.Add(FHLQuantPose::Quantize(T));
		}
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLAutopsyClassifyTest, "HitregLab.Autopsy.Classify", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLAutopsyClassifyTest::RunTest(const FString& Parameters)
{
	using namespace HLShotAutopsy;
	auto V = [](auto&& Edit) { FHLVerdictInputs In; Edit(In); return Classify(In); };

	// Validation failures win over everything else, each with its own verdict
	TestEqual(TEXT("fire rate"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("fire_rate"); I.bServerHit = true; })), int32(EHLShotVerdict::RejectedFireRate));
	TestEqual(TEXT("origin"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("origin"); })), int32(EHLShotVerdict::RejectedOrigin));
	TestEqual(TEXT("timestamp"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("bad_timestamp"); })), int32(EHLShotVerdict::RejectedTimestamp));
	TestEqual(TEXT("ammo"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("no_ammo"); })), int32(EHLShotVerdict::RejectedNoAmmo));
	TestEqual(TEXT("dead"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("dead_or_no_weapon"); })), int32(EHLShotVerdict::RejectedDead));
	TestEqual(TEXT("unknown reason"), int32(V([](FHLVerdictInputs& I) { I.RejectReason = TEXT("something_new"); })), int32(EHLShotVerdict::RejectedOther));
	TestEqual(TEXT("projectile"), int32(V([](FHLVerdictInputs& I) { I.bProjectile = true; })), int32(EHLShotVerdict::Projectile));

	// Hits: clamped or not
	TestEqual(TEXT("hit"), int32(V([](FHLVerdictInputs& I) { I.bServerHit = true; I.bViewHit = true; })), int32(EHLShotVerdict::Hit));
	TestEqual(TEXT("hit at the cap"), int32(V([](FHLVerdictInputs& I) { I.bServerHit = true; I.Clamp = EHLRewindClamp::Cap; })), int32(EHLShotVerdict::HitClamped));
	TestEqual(TEXT("hit under the cover rule"), int32(V([](FHLVerdictInputs& I) { I.bServerHit = true; I.Clamp = EHLRewindClamp::CoverRule; })), int32(EHLShotVerdict::HitClamped));

	// Misses the shooter saw as hits: the reason is the clamp (or no rewind at all)
	TestEqual(TEXT("moved, compensation off"), int32(V([](FHLVerdictInputs& I) { I.bViewHit = true; I.bLagCompensation = false; })), int32(EHLShotVerdict::MissMoved));
	TestEqual(TEXT("capped"), int32(V([](FHLVerdictInputs& I) { I.bViewHit = true; I.Clamp = EHLRewindClamp::Cap; })), int32(EHLShotVerdict::MissCapped));
	TestEqual(TEXT("cover rule"), int32(V([](FHLVerdictInputs& I) { I.bViewHit = true; I.Clamp = EHLRewindClamp::CoverRule; })), int32(EHLShotVerdict::MissCoverRule));

	// Misses on the shooter's own screen too
	TestEqual(TEXT("blocked by a wall"), int32(V([](FHLVerdictInputs& I) { I.bViewHitThroughWorld = true; })), int32(EHLShotVerdict::MissBlocked));
	TestEqual(TEXT("plain miss"), int32(V([](FHLVerdictInputs& I) {})), int32(EHLShotVerdict::MissAim));

	// Every verdict has a CSV key, and the summary counts add up
	TArray<FHLShotAutopsy> Records;
	for (int32 i = 0; i < int32(EHLShotVerdict::Count); ++i)
	{
		TestTrue(TEXT("verdict has a key"), FCString::Strlen(VerdictKey(EHLShotVerdict(i))) > 0);
		Records.Add(MakeRecord(i + 1, EHLShotVerdict(i)));
	}
	Records.Add(MakeRecord(99, EHLShotVerdict::Hit));
	const TArray<int32> Counts = CountByVerdict(Records);
	TestEqual(TEXT("two hits counted"), Counts[int32(EHLShotVerdict::Hit)], 2);
	TestEqual(TEXT("one capped miss"), Counts[int32(EHLShotVerdict::MissCapped)], 1);
	const FString Csv = SummaryCsv(Records);
	TestTrue(TEXT("summary has the total"), Csv.Contains(FString::Printf(TEXT("total_shots,%d,"), Records.Num())));
	TestTrue(TEXT("summary has miss_cover_rule"), Csv.Contains(TEXT("miss_cover_rule,1,")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLAutopsyRoundTripTest, "HitregLab.Autopsy.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLAutopsyRoundTripTest::RunTest(const FString& Parameters)
{
	// One record: bytes -> record -> bytes is identical, and the decoded fields match
	const FHLShotAutopsy A = MakeRecord(7, EHLShotVerdict::MissCapped);
	const TArray<uint8> Bytes = A.ToBytes();
	FHLShotAutopsy B;
	TestTrue(TEXT("record decodes"), B.FromBytes(Bytes));
	TestTrue(TEXT("re-encodes to the same bytes"), B.ToBytes() == Bytes);
	TestEqual(TEXT("verdict"), int32(B.Verdict), int32(EHLShotVerdict::MissCapped));
	TestEqual(TEXT("server time"), B.ServerTime, A.ServerTime);
	TestEqual(TEXT("track length"), B.Track.Num(), 20);
	TestTrue(TEXT("flags"), B.HasFlag(FHLShotAutopsy::FlagLagComp) && !B.HasFlag(FHLShotAutopsy::FlagTargetInCover));
	TestTrue(TEXT("ray direction survives quantization (< 0.01 deg)"),
		FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetDirection(), FRotator(-3.2f, -104.3f, 0.f).Vector()), -1.0, 1.0))) < 0.01);
	AddInfo(FString::Printf(TEXT("One record with a 20-sample track: %d bytes"), Bytes.Num()));
	TestTrue(TEXT("compact: under 512 bytes with its track"), Bytes.Num() < 512);

	// Track sampling interpolates between samples and holds at the ends
	FHLPose P;
	TestTrue(TEXT("track samples"), B.SampleTrack(-283.f + 12.5f, P));
	TestEqual(TEXT("halfway between two samples"), float(P.Location.X), 52.5f, 0.1f);
	B.SampleTrack(10000.f, P);
	TestEqual(TEXT("held at the end"), float(P.Location.X), 60.f - 15.f * 19, 0.1f);

	// A record from a newer build is refused, not misread
	TArray<uint8> Future = Bytes;
	Future[0] = 200;
	FHLShotAutopsy C;
	TestFalse(TEXT("newer record version refused"), C.FromBytes(Future));

	// In a replay file (version 2): save -> load keeps every record
	FHLReplayClip Clip;
	Clip.Duration = 1.f;
	FHLReplayTrack Track;
	Track.PlayerId = 257;
	Track.Keys.AddDefaulted(2);
	Track.Keys[1].Time = 1.f;
	Clip.Tracks.Add(Track);
	for (uint32 i = 1; i <= 5; ++i)
	{
		Clip.Autopsies.Add(MakeRecord(i, EHLShotVerdict(i % int32(EHLShotVerdict::Count))));
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/autopsy_roundtrip.hlreplay");
	TestTrue(TEXT("saved"), Clip.SaveToFile(Path));
	FHLReplayClip Loaded;
	TestTrue(TEXT("loaded"), Loaded.LoadFromFile(Path));
	TestEqual(TEXT("all records back"), Loaded.Autopsies.Num(), 5);
	TestEqual(TEXT("same content hash"), Loaded.ContentHash(), Clip.ContentHash());
	TestTrue(TEXT("record 3 identical"), Loaded.Autopsies[2].ToBytes() == Clip.Autopsies[2].ToBytes());

	// A version 1 file (written before autopsies existed) still loads, with no records
	FHLReplayClip Old;
	TestTrue(TEXT("version 1 file loads"), Old.FromBytes(Clip.ToBytes(1)));
	TestEqual(TEXT("version 1 has no autopsies"), Old.Autopsies.Num(), 0);
	TestEqual(TEXT("version 1 keeps its tracks"), Old.Tracks.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLCoverRuleTest, "HitregLab.Net.CoverRule", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLCoverRuleTest::RunTest(const FString& Parameters)
{
	// --- The rule on its own ---
	FHLRewindInput In;
	In.Now = 10.0;
	In.MaxRewindSeconds = 0.2;
	In.CoverBudgetSeconds = 0.1;

	In.ViewTime = 9.85; // 150 ms old
	FHLRewindDecision D = HLRewindRules::Resolve(In);
	TestEqual(TEXT("classic: traced at the view time"), D.TraceTime, 9.85, 1e-9);
	TestEqual(TEXT("classic: no clamp"), int32(D.Clamp), int32(EHLRewindClamp::None));

	In.ViewTime = 9.7; // 300 ms old
	D = HLRewindRules::Resolve(In);
	TestEqual(TEXT("cap: traced 200 ms back"), D.TraceTime, 9.8, 1e-9);
	TestEqual(TEXT("cap clamp"), int32(D.Clamp), int32(EHLRewindClamp::Cap));

	In.ViewTime = 9.85;
	In.bCoverRule = true;
	D = HLRewindRules::Resolve(In);
	TestEqual(TEXT("cover rule, target in the open: unchanged"), D.TraceTime, 9.85, 1e-9);
	TestTrue(TEXT("150 ms view: the cover test is needed"), HLRewindRules::NeedsCoverTest(In));
	In.bTargetInCoverNow = true;
	D = HLRewindRules::Resolve(In);
	TestEqual(TEXT("cover rule, target in cover: traced 100 ms back"), D.TraceTime, 9.9, 1e-9);
	TestEqual(TEXT("cover rule clamp"), int32(D.Clamp), int32(EHLRewindClamp::CoverRule));

	In.ViewTime = 9.94; // a 60 ms (low-ping) view is never touched
	D = HLRewindRules::Resolve(In);
	TestEqual(TEXT("low ping, target in cover: unchanged"), D.TraceTime, 9.94, 1e-9);
	TestEqual(TEXT("low ping: no clamp"), int32(D.Clamp), int32(EHLRewindClamp::None));
	TestFalse(TEXT("60 ms view: no cover test needed"), HLRewindRules::NeedsCoverTest(In));

	In.ViewTime = 10.05; // a view "in the future" (clock error) is traced now
	TestEqual(TEXT("future view traced now"), HLRewindRules::Resolve(In).TraceTime, 10.0, 1e-9);
	In.bLagCompensation = false;
	In.ViewTime = 9.8;
	TestEqual(TEXT("compensation off: traced now"), HLRewindRules::Resolve(In).TraceTime, 10.0, 1e-9);
	TestFalse(TEXT("compensation off: no cover test"), HLRewindRules::NeedsCoverTest(In));

	// --- A shot at a target diving into cover ---
	// The target runs along -X at 600 cm/s, 10 m from the shooter; the shooter aims at its torso as drawn 150 ms ago
	const TArray<FHLHitbox> Boxes = AHLCharacter::MakeDefaultHitboxes();
	auto PoseAt = [](double T)
	{
		FHLPose P;
		P.Location = FVector(-600.0 * (T - 10.0), 1000.0, 0.0);
		P.Yaw = 90.f;
		return P;
	};
	const FVector Eye(0.f, 0.f, 22.f);
	const FVector Aim = (PoseAt(9.85).Location + FVector(0.f, 0.f, 22.f) - Eye).GetSafeNormal();
	FHLHitboxHit Hit;
	FHLRewindInput Shot;
	Shot.Now = 10.0;
	Shot.ViewTime = 9.85;
	Shot.bCoverRule = true;
	Shot.bTargetInCoverNow = true;
	const double Classic = HLRewindRules::Resolve(FHLRewindInput{ 10.0, 9.85, true, 0.2, false, 0.1, true }).TraceTime;
	const double Fair = HLRewindRules::Resolve(Shot).TraceTime;
	TestTrue(TEXT("classic rule: the shot registers where the shooter saw it"), HLHitboxMath::RayHitboxes(Eye, Aim, 5000.f, Boxes, PoseAt(Classic), Hit));
	TestFalse(TEXT("cover rule: the target 30 cm further on is missed"), HLHitboxMath::RayHitboxes(Eye, Aim, 5000.f, Boxes, PoseAt(Fair), Hit));
	return true;
}

#endif

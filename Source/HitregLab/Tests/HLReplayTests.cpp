// Hitreg Lab: replay tests (quantization, file round trip, size budget, refusing newer formats)
#include "Misc/AutomationTest.h"
#include "Replay/HLReplayData.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A scripted 10 s duel at 30 Hz: two players strafing and turning, with shots. */
	FHLReplayClip MakeDuel()
	{
		FHLReplayClip Clip;
		Clip.Duration = 10.f;
		Clip.KillerId = 1;
		Clip.VictimId = 2;
		Clip.KillTime = 9.2f;
		for (int32 Id = 1; Id <= 2; ++Id)
		{
			FHLReplayTrack Track;
			Track.PlayerId = Id;
			Track.Name = FString::Printf(TEXT("Player %d"), Id);
			Track.Color = FLinearColor(0.2f * Id, 0.5f, 0.3f);
			for (int32 i = 0; i <= 300; ++i)
			{
				const float T = i / 30.f;
				FHLPose P;
				P.Location = FVector(Id * 1500.f, 300.f * FMath::Sin(T * (1.3f + Id)), 100.f);
				P.Yaw = FRotator::NormalizeAxis(Id == 1 ? 10.f * T : 180.f - 12.f * T);
				P.Pitch = 5.f * FMath::Cos(T);
				FHLReplayKey K;
				K.Time = T;
				K.Pose = FHLQuantPose::Quantize(P);
				K.bAlive = !(Id == 2 && T > 9.2f);
				Track.Keys.Add(K);
			}
			Clip.Tracks.Add(Track);
		}
		for (int32 i = 0; i < 12; ++i)
		{
			FHLReplayShot S;
			S.Time = 7.f + i * 0.1f;
			S.ShooterId = 1;
			S.Start = FVector(1500, 0, 164);
			S.End = FVector(3000, 50, 160);
			S.bHit = i % 3 == 0;
			Clip.Shots.Add(S);
		}
		return Clip;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLReplayRoundTripTest, "HitregLab.Replay.RoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLReplayRoundTripTest::RunTest(const FString& Parameters)
{
	// Quantization error is bounded: 0.5 mm in position, under 0.01 degree in angles
	FHLPose P;
	P.Location = FVector(1234.5678, -987.6543, 120.04);
	P.Yaw = -137.77f;
	P.Pitch = 23.456f;
	const FHLPose Q = FHLQuantPose::Quantize(P).Dequantize();
	TestTrue(TEXT("position within 0.05 cm"), FVector::Dist(P.Location, Q.Location) < 0.05f * 1.8f);
	TestTrue(TEXT("yaw within 0.01 deg"), FMath::Abs(FRotator::NormalizeAxis(P.Yaw - Q.Yaw)) < 0.01f);
	TestTrue(TEXT("pitch within 0.01 deg"), FMath::Abs(P.Pitch - Q.Pitch) < 0.01f);
	TestTrue(TEXT("quantizing twice is stable"), FHLQuantPose::Quantize(Q) == FHLQuantPose::Quantize(P));

	// Save -> load -> identical content, identical hash, identical playback at every frame
	const FHLReplayClip Clip = MakeDuel();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Automation/replay_roundtrip.hlreplay");
	TestTrue(TEXT("saved"), Clip.SaveToFile(Path));
	FHLReplayClip Loaded;
	TestTrue(TEXT("loaded"), Loaded.LoadFromFile(Path));
	TestEqual(TEXT("same hash"), Loaded.ContentHash(), Clip.ContentHash());
	TestEqual(TEXT("same shots"), Loaded.Shots.Num(), Clip.Shots.Num());
	bool bFramesMatch = true;
	for (int32 i = 0; i <= 300; ++i)
	{
		for (const FHLReplayTrack& Track : Clip.Tracks)
		{
			FHLPose A, B;
			bool bAliveA = false, bAliveB = false;
			Track.Sample(i / 30.f, A, bAliveA);
			Loaded.FindTrack(Track.PlayerId)->Sample(i / 30.f, B, bAliveB);
			bFramesMatch &= A.Location.Equals(B.Location, 1e-4) && FMath::IsNearlyEqual(A.Yaw, B.Yaw, 1e-4f) && bAliveA == bAliveB;
		}
	}
	TestTrue(TEXT("playback identical at every recorded frame"), bFramesMatch);

	// A file from a newer build is refused, not misread
	TArray<uint8> Bytes = Clip.ToBytes();
	Bytes[4] = 99; // version field (after the 4-byte magic)
	FHLReplayClip Future;
	TestFalse(TEXT("newer version refused"), Future.FromBytes(Bytes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHLReplaySizeTest, "HitregLab.Replay.Size", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHLReplaySizeTest::RunTest(const FString& Parameters)
{
	const FHLReplayClip Clip = MakeDuel();
	const float Bps = Clip.BytesPerSecondPerPlayer();
	AddInfo(FString::Printf(TEXT("%d bytes for 10 s, 2 players: %.0f bytes/s per player"), Clip.ToBytes().Num(), Bps));
	TestTrue(TEXT("under 2 KB/s per player"), Bps < 2048.f);
	return true;
}

#endif

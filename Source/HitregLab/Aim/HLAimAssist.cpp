// Hitreg Lab: aim assist maths and the bot-thumb benchmark
#include "Aim/HLAimAssist.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace HLAimAssist
{
	FVector2D ApplyStickCurve(const UHLAimAssistSettings& S, const FVector2D& Raw)
	{
		const float Mag = Raw.Size();
		if (Mag <= S.InnerDeadzone || Mag < UE_KINDA_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		const float Range = FMath::Max(S.OuterDeadzone - S.InnerDeadzone, 0.01f);
		const float Scaled = FMath::Clamp((Mag - S.InnerDeadzone) / Range, 0.f, 1.f);
		return (Raw / Mag) * FMath::Pow(Scaled, S.ResponseExponent);
	}

	FRotator LookAt(const FVector& CamLoc, const FVector& Point)
	{
		return (Point - CamLoc).Rotation();
	}

	float AngleTo(const FVector& CamLoc, const FRotator& CamRot, const FVector& Point)
	{
		const FVector To = (Point - CamLoc).GetSafeNormal();
		const float Dot = FMath::Clamp(FVector::DotProduct(CamRot.Vector(), To), -1.f, 1.f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}

	FVector2D LookRate(const UHLAimAssistSettings& S, const FVector2D& Curved, bool bAds, float EdgeHoldSeconds)
	{
		const float Scale = bAds ? S.AdsRateScale : 1.f;
		FVector2D Rate(Curved.X * S.MaxYawRate * Scale, Curved.Y * S.MaxPitchRate * Scale);
		if (FMath::Abs(Curved.X) > 0.95f && EdgeHoldSeconds > S.AccelDelay && !bAds)
		{
			const float Ramp = FMath::Clamp((EdgeHoldSeconds - S.AccelDelay) / FMath::Max(S.AccelRampTime, 0.01f), 0.f, 1.f);
			Rate.X += FMath::Sign(Curved.X) * S.AccelExtraYawRate * Ramp;
		}
		return Rate;
	}

	FHLAimOutput Compute(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, const FHLAimInput& In, TArrayView<const FHLAimTarget> Targets)
	{
		FHLAimOutput Out;
		if (Mode == EHLAimAssistMode::Off || In.WeaponScale <= 0.f)
		{
			return Out;
		}

		// Pick the target closest to the crosshair, measured in bubble radii (so a near target's big bubble
		// doesn't lose to a far target that happens to be a degree closer)
		float BestScore = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Targets.Num(); ++i)
		{
			const float Dist = FVector::Dist(Targets[i].Point, In.CamLoc);
			if (Dist > S.MaxRangeCm || Dist < 1.f)
			{
				continue;
			}
			const float Angle = AngleTo(In.CamLoc, In.CamRot, Targets[i].Point);
			const float SlowBubble = FMath::Min(FMath::RadiansToDegrees(FMath::Atan(S.SlowdownRadiusCm / Dist)), S.MaxAssistAngleDeg);
			const float PullBubble = FMath::Min(FMath::RadiansToDegrees(FMath::Atan(S.PullRadiusCm / Dist)), S.MaxAssistAngleDeg);
			const float Bubble = FMath::Max(SlowBubble, PullBubble);
			if (Angle >= Bubble)
			{
				continue;
			}
			const float Score = Angle / Bubble;
			if (Score < BestScore)
			{
				BestScore = Score;
				Out.TargetIndex = i;
				Out.TargetAngleDeg = Angle;
				Out.SlowdownBubbleDeg = SlowBubble;
				Out.PullBubbleDeg = PullBubble;
			}
		}
		if (Out.TargetIndex == INDEX_NONE)
		{
			return Out;
		}
		const FHLAimTarget& T = Targets[Out.TargetIndex];

		// Slowdown: the look rate drops smoothly toward the target's centre
		if (S.bSlowdown && Out.TargetAngleDeg < Out.SlowdownBubbleDeg)
		{
			const float MinScale = FMath::Lerp(1.f, In.bAds ? S.SlowdownMinScaleAds : S.SlowdownMinScale, FMath::Clamp(In.WeaponScale, 0.f, 1.f));
			const float Alpha = FMath::SmoothStep(0.f, 1.f, Out.TargetAngleDeg / Out.SlowdownBubbleDeg);
			Out.SlowdownScale = FMath::Lerp(MinScale, 1.f, Alpha);
		}

		// Pull: only while the player is actively looking or moving, never on its own
		const float InputAmount = FMath::Max(In.Look.Size(), In.MoveInput);
		if (Mode == EHLAimAssistMode::Full && S.bPull && Out.TargetAngleDeg < Out.PullBubbleDeg && InputAmount >= S.MinInputForPull)
		{
			// Target's angular velocity as seen from the camera (relative motion over a short step)
			const float Step = 0.05f;
			const FVector RelVel = T.Velocity - In.PlayerVelocity;
			const FRotator Now = LookAt(In.CamLoc, T.Point);
			const FRotator Next = LookAt(In.CamLoc + In.PlayerVelocity * 0.f, T.Point + RelVel * Step);
			const FVector2D AngVel(FRotator::NormalizeAxis(Next.Yaw - Now.Yaw) / Step, FRotator::NormalizeAxis(Next.Pitch - Now.Pitch) / Step);
			const FVector2D Error(FRotator::NormalizeAxis(Now.Yaw - In.CamRot.Yaw), FRotator::NormalizeAxis(Now.Pitch - In.CamRot.Pitch));
			const float Falloff = 1.f - FMath::Clamp(Out.TargetAngleDeg / Out.PullBubbleDeg, 0.f, 1.f);
			FVector2D Pull = AngVel * S.PullTrackingStrength + Error * (S.PullCenteringRate * Falloff);
			const float Cap = S.PullMaxDegPerSec * In.WeaponScale;
			Pull.X = FMath::Clamp(Pull.X, -Cap, Cap);
			Pull.Y = FMath::Clamp(Pull.Y, -Cap, Cap);
			Out.PullRate = Pull * In.WeaponScale;
		}
		return Out;
	}

	FVector ApplyBulletMagnetism(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, const FVector& CamLoc, const FVector& ShotDir, TArrayView<const FHLAimTarget> Targets, float WeaponScale)
	{
		if (Mode != EHLAimAssistMode::Full || !S.bBulletMagnetism || WeaponScale <= 0.f)
		{
			return ShotDir;
		}
		const float Cone = S.BulletMagnetismConeDeg * WeaponScale;
		float BestAngle = Cone;
		FVector Best = ShotDir;
		for (const FHLAimTarget& T : Targets)
		{
			for (const FVector& P : { T.Head, T.Point })
			{
				if (FVector::Dist(P, CamLoc) > S.MaxRangeCm)
				{
					continue;
				}
				const FVector To = (P - CamLoc).GetSafeNormal();
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ShotDir, To), -1.f, 1.f)));
				if (Angle < BestAngle)
				{
					BestAngle = Angle;
					Best = To;
				}
			}
		}
		return Best;
	}
}

namespace HLAimBench
{
	FHLAimBenchResult RunTrial(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, int32 Seed, float TrialSeconds)
	{
		FRandomStream Rng(Seed);
		const float Dt = 1.f / 120.f;
		const int32 Steps = FMath::CeilToInt(TrialSeconds / Dt);
		const float Delay = 0.2f;
		const int32 DelaySteps = FMath::RoundToInt(Delay / Dt);

		// Target 20 m away, strafing sinusoidally at up to ~600 cm/s; head at eye height, torso 44 cm lower
		const float Distance = 2000.f + Rng.FRandRange(-200.f, 200.f);
		const float Amplitude = 250.f;
		const float Period = 2.6f;
		const float Phase = Rng.FRandRange(0.f, 2.f * UE_PI);
		const float HeadRadius = 16.f;
		auto TargetY = [&](float T) { return Amplitude * FMath::Sin(2.f * UE_PI * T / Period + Phase); };
		auto TargetVY = [&](float T) { return Amplitude * (2.f * UE_PI / Period) * FMath::Cos(2.f * UE_PI * T / Period + Phase); };

		// Crosshair starts 6-10 degrees off to a random side, a little high
		const FVector Cam = FVector::ZeroVector;
		FRotator CamRot = HLAimAssist::LookAt(Cam, FVector(Distance, TargetY(0.f), 0.f));
		CamRot.Yaw += (Rng.FRand() < 0.5f ? -1.f : 1.f) * Rng.FRandRange(6.f, 10.f);
		CamRot.Pitch += Rng.FRandRange(0.5f, 2.f);

		TArray<FVector2D> ErrHistory;      // what the thumb sees, delayed
		TArray<FVector2D> AngVelHistory;
		TArray<FVector2D> CamHistory;      // where the camera pointed (yaw, pitch) at each step
		FHLAimBenchResult R;
		float OnTargetTime = 0.f;
		float FirstAcquire = -1.f;
		int32 TrackSteps = 0;
		int32 TrackOn = 0;
		bool bCrossed = false;
		float InitialSign = 0.f;
		float EdgeHold = 0.f;
		FVector2D NoiseState = FVector2D::ZeroVector;

		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float T = Step * Dt;
			const FVector Head(Distance, TargetY(T), 2.f);
			const FVector Torso(Distance, TargetY(T), -42.f);
			const FVector Vel(0.f, TargetVY(T), 0.f);

			const FRotator Want = HLAimAssist::LookAt(Cam, Head);
			const FVector2D Err(FRotator::NormalizeAxis(Want.Yaw - CamRot.Yaw), FRotator::NormalizeAxis(Want.Pitch - CamRot.Pitch));
			const FRotator WantNext = HLAimAssist::LookAt(Cam, Head + Vel * 0.05f);
			const FVector2D AngVel(FRotator::NormalizeAxis(WantNext.Yaw - Want.Yaw) / 0.05f, FRotator::NormalizeAxis(WantNext.Pitch - Want.Pitch) / 0.05f);
			ErrHistory.Add(Err);
			AngVelHistory.Add(AngVel);
			CamHistory.Add(FVector2D(CamRot.Yaw, CamRot.Pitch));

			// --- The thumb: acts on what it saw 200 ms ago, corrected the way people do: it knows how far
			// it has turned since (efference copy) and extrapolates the target over the delay ---
			FVector2D Raw = FVector2D::ZeroVector;
			const bool bThumbActive = Step >= DelaySteps;
			if (bThumbActive)
			{
				const FVector2D SeenVel = AngVelHistory[Step - DelaySteps];
				const FVector2D OwnTurn(FRotator::NormalizeAxis(CamRot.Yaw - CamHistory[Step - DelaySteps].X), CamRot.Pitch - CamHistory[Step - DelaySteps].Y);
				const FVector2D SeenErr = ErrHistory[Step - DelaySteps] - OwnTurn + SeenVel * Delay;
				const FVector2D DesiredRate = SeenErr * 4.f + SeenVel * 0.9f;
				// Invert the response curve roughly, the way a practised player learns their sensitivity
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const float MaxRate = Axis == 0 ? S.MaxYawRate : S.MaxPitchRate;
					const float M = FMath::Clamp(FMath::Abs(DesiredRate[Axis]) / MaxRate, 0.f, 1.f);
					const float Stick = M <= 0.f ? 0.f : S.InnerDeadzone + (S.OuterDeadzone - S.InnerDeadzone) * FMath::Pow(M, 1.f / S.ResponseExponent);
					Raw[Axis] = FMath::Sign(DesiredRate[Axis]) * Stick;
				}
				// Thumb tremor: low-passed noise
				NoiseState = NoiseState * 0.9f + FVector2D(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)) * 0.012f;
				Raw += NoiseState;
				Raw.X = FMath::Clamp(Raw.X, -1.f, 1.f);
				Raw.Y = FMath::Clamp(Raw.Y, -1.f, 1.f);
			}

			// --- The game: curve, assist, rotate ---
			const FVector2D Curved = HLAimAssist::ApplyStickCurve(S, Raw);
			EdgeHold = FMath::Abs(Curved.X) > 0.95f ? EdgeHold + Dt : 0.f;
			FHLAimInput In;
			In.CamLoc = Cam;
			In.CamRot = CamRot;
			In.Look = Curved;
			FHLAimTarget Target;
			Target.Point = Torso;
			Target.Head = Head;
			Target.Velocity = Vel;
			const FHLAimOutput Assist = HLAimAssist::Compute(S, Mode, In, MakeArrayView(&Target, 1));
			const FVector2D Rate = HLAimAssist::LookRate(S, Curved, false, EdgeHold) * Assist.SlowdownScale + Assist.PullRate;
			CamRot.Yaw += Rate.X * Dt;
			CamRot.Pitch += Rate.Y * Dt;
			if (Raw.IsNearlyZero())
			{
				R.IdleRotationDeg += (FMath::Abs(Rate.X) + FMath::Abs(Rate.Y)) * Dt;
			}

			// --- Metrics ---
			const float Angle = HLAimAssist::AngleTo(Cam, CamRot, Head);
			const float HeadAngle = FMath::RadiansToDegrees(FMath::Atan(HeadRadius / Distance));
			const bool bOn = Angle < HeadAngle;
			OnTargetTime = bOn ? OnTargetTime + Dt : 0.f;
			if (FirstAcquire < 0.f && OnTargetTime >= 0.1f)
			{
				FirstAcquire = T;
			}
			if (FirstAcquire >= 0.f)
			{
				++TrackSteps;
				TrackOn += bOn ? 1 : 0;
			}
			const float YawErr = FRotator::NormalizeAxis(HLAimAssist::LookAt(Cam, Head).Yaw - CamRot.Yaw);
			if (InitialSign == 0.f)
			{
				InitialSign = FMath::Sign(YawErr);
			}
			else if (!bCrossed && FMath::Sign(YawErr) != InitialSign)
			{
				bCrossed = true;
			}
			if (bCrossed && FirstAcquire < 0.f)
			{
				R.OvershootDeg = FMath::Max(R.OvershootDeg, -InitialSign * YawErr);
			}
		}
		R.bAcquired = FirstAcquire >= 0.f;
		R.TimeToAcquire = R.bAcquired ? FirstAcquire : TrialSeconds;
		R.TrackingFraction = TrackSteps > 0 ? float(TrackOn) / TrackSteps : 0.f;
		return R;
	}

	static float Median(TArray<float> V)
	{
		if (V.Num() == 0)
		{
			return 0.f;
		}
		V.Sort();
		return V[V.Num() / 2];
	}

	FHLAimBenchResult RunMedian(const UHLAimAssistSettings& S, EHLAimAssistMode Mode, int32 Trials, int32 FirstSeed)
	{
		TArray<float> Tta, Over, Track;
		FHLAimBenchResult Out;
		int32 Acquired = 0;
		for (int32 i = 0; i < Trials; ++i)
		{
			const FHLAimBenchResult R = RunTrial(S, Mode, FirstSeed + i);
			Tta.Add(R.TimeToAcquire);
			Over.Add(R.OvershootDeg);
			Track.Add(R.TrackingFraction);
			Out.IdleRotationDeg = FMath::Max(Out.IdleRotationDeg, R.IdleRotationDeg);
			Acquired += R.bAcquired ? 1 : 0;
		}
		Out.TimeToAcquire = Median(Tta);
		Out.OvershootDeg = Median(Over);
		Out.TrackingFraction = Median(Track);
		Out.bAcquired = Acquired * 2 >= Trials;
		return Out;
	}

	FString RunToCsv(const UHLAimAssistSettings& S, int32 Trials, const FString& Directory)
	{
		FString Csv = TEXT("mode,seed,time_to_acquire_s,overshoot_deg,tracking_fraction,acquired,idle_rotation_deg\n");
		FString Summary;
		const TCHAR* Names[] = { TEXT("off"), TEXT("slowdown"), TEXT("full") };
		for (int32 M = 0; M < 3; ++M)
		{
			const EHLAimAssistMode Mode = EHLAimAssistMode(M);
			for (int32 i = 0; i < Trials; ++i)
			{
				const FHLAimBenchResult R = RunTrial(S, Mode, 1 + i);
				Csv += FString::Printf(TEXT("%s,%d,%.4f,%.3f,%.3f,%d,%.4f\n"), Names[M], 1 + i, R.TimeToAcquire, R.OvershootDeg, R.TrackingFraction, R.bAcquired ? 1 : 0, R.IdleRotationDeg);
			}
			const FHLAimBenchResult Med = RunMedian(S, Mode, Trials);
			Summary += FString::Printf(TEXT("%s: acquire %.0f ms, overshoot %.2f deg, tracking %.0f%%   "), Names[M], Med.TimeToAcquire * 1000.f, Med.OvershootDeg, Med.TrackingFraction * 100.f);
		}
		const FString File = FPaths::Combine(Directory, FString::Printf(TEXT("aim_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
		FFileHelper::SaveStringToFile(Csv, *File);
		return Summary + TEXT("-> ") + File;
	}
}

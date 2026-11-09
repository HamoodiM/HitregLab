// Hitreg Lab: replay clip format
#include "Replay/HLReplayData.h"
#include "Algo/BinarySearch.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

FHLQuantPose FHLQuantPose::Quantize(const FHLPose& Pose)
{
	FHLQuantPose Q;
	Q.X = FMath::RoundToInt(Pose.Location.X * 10.0);
	Q.Y = FMath::RoundToInt(Pose.Location.Y * 10.0);
	Q.Z = FMath::RoundToInt(Pose.Location.Z * 10.0);
	const float Yaw01 = FRotator::ClampAxis(Pose.Yaw) / 360.f;
	Q.Yaw = uint16(FMath::RoundToInt(Yaw01 * 65536.f) & 0xFFFF);
	Q.Pitch = int16(FMath::Clamp(FMath::RoundToInt(FMath::Clamp(Pose.Pitch, -90.f, 90.f) / 90.f * 32767.f), -32767, 32767));
	return Q;
}

FHLPose FHLQuantPose::Dequantize() const
{
	FHLPose P;
	P.Location = FVector(X, Y, Z) / 10.0;
	P.Yaw = FRotator::NormalizeAxis(float(Yaw) / 65536.f * 360.f);
	P.Pitch = float(Pitch) / 32767.f * 90.f;
	return P;
}

FArchive& operator<<(FArchive& Ar, FHLQuantPose& P)
{
	Ar << P.X << P.Y << P.Z << P.Yaw << P.Pitch;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHLReplayKey& K)
{
	uint8 Alive = K.bAlive ? 1 : 0;
	Ar << K.Time << K.Pose << Alive;
	K.bAlive = Alive != 0;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHLReplayTrack& T)
{
	Ar << T.PlayerId << T.Name << T.Color << T.Keys;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FHLReplayShot& S)
{
	uint8 Hit = S.bHit ? 1 : 0;
	Ar << S.Time << S.ShooterId << S.Start << S.End << Hit;
	S.bHit = Hit != 0;
	return Ar;
}

bool FHLReplayTrack::Sample(float Time, FHLPose& OutPose, bool& bOutAlive) const
{
	if (Keys.Num() == 0)
	{
		return false;
	}
	if (Time <= Keys[0].Time)
	{
		OutPose = Keys[0].Pose.Dequantize();
		bOutAlive = Keys[0].bAlive;
		return true;
	}
	if (Time >= Keys.Last().Time)
	{
		OutPose = Keys.Last().Pose.Dequantize();
		bOutAlive = Keys.Last().bAlive;
		return true;
	}
	const int32 Hi = Algo::UpperBoundBy(Keys, Time, &FHLReplayKey::Time);
	const FHLReplayKey& A = Keys[Hi - 1];
	const FHLReplayKey& B = Keys[Hi];
	const float Alpha = (Time - A.Time) / FMath::Max(B.Time - A.Time, 1e-6f);
	OutPose = FHLPose::Lerp(A.Pose.Dequantize(), B.Pose.Dequantize(), Alpha);
	bOutAlive = A.bAlive;
	return true;
}

const FHLReplayTrack* FHLReplayClip::FindTrack(int32 PlayerId) const
{
	return Tracks.FindByPredicate([PlayerId](const FHLReplayTrack& T) { return T.PlayerId == PlayerId; });
}

bool FHLReplayClip::Serialize(FArchive& Ar, int32 WriteVersion)
{
	uint32 FileMagic = Magic;
	int32 FileVersion = FMath::Clamp(WriteVersion, 1, Version);
	Ar << FileMagic << FileVersion;
	if (Ar.IsLoading() && (FileMagic != Magic || FileVersion > Version || FileVersion < 1))
	{
		return false; // not a replay, or written by a newer build: refuse rather than misread
	}
	Ar << Duration << KillerId << VictimId << KillTime << Tracks << Shots;
	if (FileVersion >= 2)
	{
		Ar << Autopsies;
	}
	else if (Ar.IsLoading())
	{
		Autopsies.Reset();
	}
	return !Ar.IsError();
}

TArray<uint8> FHLReplayClip::ToBytes(int32 WriteVersion) const
{
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	const_cast<FHLReplayClip*>(this)->Serialize(Writer, WriteVersion);
	return Bytes;
}

bool FHLReplayClip::FromBytes(const TArray<uint8>& Bytes)
{
	FMemoryReader Reader(Bytes);
	return Serialize(Reader);
}

bool FHLReplayClip::SaveToFile(const FString& Path) const
{
	return FFileHelper::SaveArrayToFile(ToBytes(), *Path);
}

bool FHLReplayClip::LoadFromFile(const FString& Path)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Path) && FromBytes(Bytes);
}

uint32 FHLReplayClip::ContentHash() const
{
	const TArray<uint8> Bytes = ToBytes();
	return FCrc::MemCrc32(Bytes.GetData(), Bytes.Num());
}

float FHLReplayClip::BytesPerSecondPerPlayer() const
{
	if (Duration <= 0.f || Tracks.Num() == 0)
	{
		return 0.f;
	}
	return float(ToBytes().Num()) / Duration / float(Tracks.Num());
}

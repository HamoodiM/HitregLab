// Hitreg Lab: clock offset estimate
#include "Net/HLClockSync.h"

void FHLClockSync::AddSample(double ClientSend, double ServerTime, double ClientRecv)
{
	const double Rtt = FMath::Max(0.0, ClientRecv - ClientSend);
	LastRtt = Rtt;
	// Assume the reply took half the round trip: when the client received it, the server clock read ServerTime + Rtt/2
	FSample S;
	S.Rtt = Rtt;
	S.Offset = (ServerTime + Rtt * 0.5) - ClientRecv;
	if (Samples.Num() >= MaxSamples)
	{
		Samples.RemoveAt(0);
	}
	Samples.Add(S);
}

double FHLClockSync::GetOffset() const
{
	const FSample* Best = nullptr;
	for (const FSample& S : Samples)
	{
		if (!Best || S.Rtt < Best->Rtt)
		{
			Best = &S;
		}
	}
	return Best ? Best->Offset : 0.0;
}

double FHLClockSync::GetBestRtt() const
{
	const FSample* Best = nullptr;
	for (const FSample& S : Samples)
	{
		if (!Best || S.Rtt < Best->Rtt)
		{
			Best = &S;
		}
	}
	return Best ? Best->Rtt : 0.0;
}

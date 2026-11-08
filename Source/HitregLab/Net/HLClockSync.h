// Hitreg Lab: NTP-style clock offset estimate. Pure: feed it ping samples, read the offset.
#pragma once

#include "CoreMinimal.h"

class HITREGLAB_API FHLClockSync
{
public:
	/** One round trip: client sent at ClientSend, server stamped ServerTime, client received at ClientRecv (seconds). */
	void AddSample(double ClientSend, double ServerTime, double ClientRecv);

	bool HasEstimate() const { return Samples.Num() > 0; }

	/**
	 * ServerTime ~= ClientTime + Offset. Taken from the lowest-RTT recent sample: the less time a ping spent
	 * in flight, the less the "reply took half the round trip" guess can be wrong.
	 */
	double GetOffset() const;

	/** RTT of the sample the offset came from (seconds). */
	double GetBestRtt() const;

	/** Most recent RTT (seconds). */
	double GetLastRtt() const { return LastRtt; }

	int32 NumSamples() const { return Samples.Num(); }

	static constexpr int32 MaxSamples = 8;

private:
	struct FSample
	{
		double Rtt = 0.0;
		double Offset = 0.0;
	};
	TArray<FSample> Samples;
	double LastRtt = 0.0;
};

// Offline test for the techno analyzer, no Unreal needed.
// Build & run (Linux/macOS/MinGW):
//   g++ -O2 -std=c++17 -I Source/TechnoViz/Private/Audio Scripts/analyzer_test.cpp Source/TechnoViz/Private/Audio/TechnoAnalyzer.cpp -o analyzer_test && ./analyzer_test
// Feeds the demo techno loop (16 bars groove, 4 bars breakdown, drop) in 10 ms packets like WASAPI and checks
// tempo lock, beat-grid timing against the true kicks, and drop detection.
#include "TechnoAnalyzer.h"
#include "TechnoDemoSynth.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int RunOne(float Bpm)
{
	TechnoDSP::DemoSynth Synth(48000.f, Bpm);
	TechnoDSP::Analyzer A;
	A.Configure(48000.f);
	A.bUseAudioClockForSnapshot = true;
	A.SetLookaheadMs(0.f);

	const double Beat = 60.0 / Bpm;
	const double DropTime = 20 * 4 * Beat; // first drop at bar 20
	std::vector<float> Buf(480);
	double ErrSum = 0; int ErrN = 0, Drops = 0; double FirstDrop = -1; float FinalBpm = 0;
	for (int i = 0; i < 48000 * 90 / 480; ++i)
	{
		Synth.Render(Buf.data(), 480);
		A.Process(Buf.data(), 480);
		const auto F = A.GetSnapshot();
		const double t = (i + 1) * 0.01;
		if (F.DropCount != Drops) { Drops = F.DropCount; if (FirstDrop < 0) FirstDrop = t; }
		FinalBpm = F.Bpm;
		if (t > 6 && F.bGridLocked)
		{
			double e = F.BeatPhase - std::fmod(t / Beat, 1.0);
			if (e > 0.5) e -= 1;
			if (e < -0.5) e += 1;
			ErrSum += std::fabs(e) * Beat * 1000; ++ErrN;
		}
	}
	const double GridErr = ErrSum / std::max(ErrN, 1);
	const bool bOk = std::fabs(FinalBpm - Bpm) < 0.5f && GridErr < 8.0 && Drops >= 2 && std::fabs(FirstDrop - DropTime) < 0.05;
	printf("%s bpm %.0f -> tracked %.2f | grid error %.1f ms | drops %d, first at %.2fs (expected %.2fs)\n",
		bOk ? "PASS" : "FAIL", Bpm, FinalBpm, GridErr, Drops, FirstDrop, DropTime);
	return bOk ? 0 : 1;
}

int main()
{
	int Fails = 0;
	for (float Bpm : { 122.f, 128.f, 132.f, 138.f, 145.f, 150.f }) Fails += RunOne(Bpm);
	return Fails;
}

// Tiny techno loop generator used for "Demo" mode (see the scene without playing music)
// and for the offline analyzer test. Arrangement: 16 bars groove, 4 bars breakdown, drop, repeat.
#pragma once

#include <cmath>
#include <cstdint>

namespace TechnoDSP
{
	class DemoSynth
	{
	public:
		explicit DemoSynth(float InFs = 48000.f, float InBpm = 132.f) : Fs(InFs), Bpm(InBpm) {}

		void Render(float* Out, int N)
		{
			const double Beat = 60.0 / Bpm;
			for (int i = 0; i < N; ++i)
			{
				const double t = Sample / Fs;
				const double Bars = t / (Beat * 4.0);
				const int Bar = (int)std::fmod(Bars, 20.0);
				const bool bBreak = Bar >= 16;
				const double bt = std::fmod(t, Beat);
				const int BeatIdx = (int)(t / Beat);
				float s = 0.f;

				if (BeatIdx != LastBeat) { LastBeat = BeatIdx; KickPhase = 0.0; }
				if (!bBreak)
				{
					const double f = 48.0 + 130.0 * std::exp(-bt * 38.0);
					KickPhase += 2.0 * 3.14159265358979 * f / Fs;
					s += 0.85f * (float)(std::sin(KickPhase) * std::exp(-bt * 8.0));
				}
				// Rolling bass on the off-16ths (ducked by the kick).
				const double Six = Beat / 4.0;
				const double sx = std::fmod(t, Six);
				const int SixIdx = (int)(bt / Six);
				if (SixIdx != 0 && !bBreak)
					s += 0.28f * (float)(std::sin(2.0 * 3.14159265358979 * 55.0 * 2.0 * t) * std::exp(-sx * 22.0));
				// Open hat on the off-beat, closed hats on 16ths.
				const float n = Noise();
				Hp = 0.92f * (Hp + n - PrevN);
				PrevN = n;
				const double hb = std::fmod(t + Beat * 0.5, Beat);
				s += 0.16f * Hp * (float)std::exp(-hb * 18.0);
				s += 0.05f * Hp * (float)std::exp(-sx * 120.0);
				// Clap on 2 and 4.
				if (BeatIdx % 2 == 1 && !bBreak) s += 0.18f * n * (float)std::exp(-bt * 30.0);
				// Pad / riser through the breakdown.
				const float Riser = bBreak ? (float)std::fmod(Bars, 20.0) - 16.f : 0.f;
				s += (0.04f + 0.03f * Riser) * (float)std::sin(2.0 * 3.14159265358979 * (220.0 + 60.0 * Riser) * t);
				Out[i] = 0.5f * s;
				++Sample;
			}
		}

	private:
		float Noise()
		{
			Seed = Seed * 1664525u + 1013904223u;
			return ((Seed >> 9) * (1.f / 4194304.f)) * 2.f - 1.f;
		}

		float Fs, Bpm;
		int64_t Sample = 0;
		int LastBeat = -1;
		double KickPhase = 0.0;
		float Hp = 0.f, PrevN = 0.f;
		uint32_t Seed = 12345u;
	};
}

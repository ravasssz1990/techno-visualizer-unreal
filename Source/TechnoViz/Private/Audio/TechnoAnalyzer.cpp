#include "TechnoAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace TechnoDSP
{
	static constexpr float Pi = 3.14159265358979f;

	static inline float Clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

	// ---------------------------------------------------------------- Biquad
	void Biquad::SetLowpass(float SR, float F, float Q)
	{
		const float w0 = 2.f * Pi * F / SR, c = std::cos(w0), al = std::sin(w0) / (2.f * Q);
		const float a0 = 1.f + al;
		b0 = (1.f - c) * 0.5f / a0; b1 = (1.f - c) / a0; b2 = b0;
		a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
		z1 = z2 = 0.f;
	}

	void Biquad::SetHighpass(float SR, float F, float Q)
	{
		const float w0 = 2.f * Pi * F / SR, c = std::cos(w0), al = std::sin(w0) / (2.f * Q);
		const float a0 = 1.f + al;
		b0 = (1.f + c) * 0.5f / a0; b1 = -(1.f + c) / a0; b2 = b0;
		a1 = -2.f * c / a0; a2 = (1.f - al) / a0;
		z1 = z2 = 0.f;
	}

	// ---------------------------------------------------------------- Follower
	static inline float Coef(float Rate, float Tau)
	{
		return Tau <= 0.f ? 1.f : 1.f - std::exp(-1.f / (Rate * Tau));
	}

	void Follower::Set(float Rate, float AttackSec, float ReleaseSec)
	{
		Att = Coef(Rate, AttackSec);
		Rel = Coef(Rate, ReleaseSec);
	}

	// ---------------------------------------------------------------- Onsets
	void OnsetDetector::Configure(float Rate, float FastRel, float SlowRel)
	{
		Fast.Set(Rate, 0.f, FastRel);
		Slow.Set(Rate, SlowRel, SlowRel);
		MeanCoef = Coef(Rate, 2.0f);
	}

	bool OnsetDetector::Tick(float Level, double Time, double Refractory)
	{
		const float f = Fast.Tick(Level);
		const float s = Slow.Tick(Level);
		Odf = std::max(0.f, f - s);

		const float Std = std::sqrt(std::max(Var, 0.f));
		const float Thr = Mean + Sensitivity * Std + AbsFloor;
		bool bOnset = false;
		if (bArmed && Odf > Thr && (Time - LastOnset) > Refractory)
		{
			bOnset = true;
			LastOnset = Time;
			bArmed = false;
		}
		else if (!bArmed && Odf < Mean + 0.5f * Std + AbsFloor * 0.5f)
		{
			bArmed = true;
		}

		const float d = Odf - Mean;
		Mean += MeanCoef * d;
		Var = (1.f - MeanCoef) * (Var + MeanCoef * d * d);
		return bOnset;
	}

	// ---------------------------------------------------------------- FFT
	static void Fft(std::vector<std::complex<float>>& a)
	{
		const int n = (int)a.size();
		for (int i = 1, j = 0; i < n; ++i)
		{
			int bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) std::swap(a[i], a[j]);
		}
		for (int len = 2; len <= n; len <<= 1)
		{
			const float ang = -2.f * Pi / len;
			const std::complex<float> wl(std::cos(ang), std::sin(ang));
			for (int i = 0; i < n; i += len)
			{
				std::complex<float> w(1.f, 0.f);
				for (int k = 0; k < len / 2; ++k)
				{
					const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
					a[i + k] = u + v;
					a[i + k + len / 2] = u - v;
					w *= wl;
				}
			}
		}
	}

	// ---------------------------------------------------------------- Analyzer
	Analyzer::Analyzer()
	{
		Configure(48000.f);
	}

	void Analyzer::SetSensitivity(float S)
	{
		SensitivityScale = std::max(0.25f, std::min(S, 4.f));
		// Lower threshold multiplier = more sensitive.
		KickOnset.Sensitivity = 1.4f / SensitivityScale;
		HatOnset.Sensitivity = 1.6f / SensitivityScale;
	}

	void Analyzer::Configure(float SampleRate)
	{
		Fs = SampleRate;
		ControlRate = Fs / Hop;

		// Kick: 35-110 Hz. Bass: 120-350 Hz. Mid: 350-4k. Hats: 6k+.
		KickHP.SetHighpass(Fs, 35.f, 0.7071f);
		KickLP1.SetLowpass(Fs, 110.f, 0.5412f);
		KickLP2.SetLowpass(Fs, 110.f, 1.3066f);
		BassHP1.SetHighpass(Fs, 120.f, 0.5412f);
		BassHP2.SetHighpass(Fs, 120.f, 1.3066f);
		BassLP1.SetLowpass(Fs, 350.f, 0.5412f);
		BassLP2.SetLowpass(Fs, 350.f, 1.3066f);
		MidHP1.SetHighpass(Fs, 350.f, 0.5412f);
		MidHP2.SetHighpass(Fs, 350.f, 1.3066f);
		MidLP1.SetLowpass(Fs, 4000.f, 0.5412f);
		MidLP2.SetLowpass(Fs, 4000.f, 1.3066f);
		HatHP1.SetHighpass(Fs, 6000.f, 0.5412f);
		HatHP2.SetHighpass(Fs, 6000.f, 1.3066f);

		// Auto-gain peak memory: half-life ~8 s.
		PeakDecay = std::pow(0.5f, 1.f / (ControlRate * 8.f));
		Peak.fill(0.f);
		KickRefDecay = std::pow(0.5f, 1.f / (ControlRate * 40.f));
		KickRef = 0.f;

		// Envelopes: near-instant attack, pumpy releases.
		Env[0].Set(ControlRate, 0.002f, 0.11f); // kick
		Env[1].Set(ControlRate, 0.004f, 0.14f); // bass
		Env[2].Set(ControlRate, 0.004f, 0.16f); // mid
		Env[3].Set(ControlRate, 0.001f, 0.06f); // hats
		EnergyEnv.Set(ControlRate, 0.05f, 0.45f);
		IntensityEnv.Set(ControlRate, 1.0f, 2.5f);

		KickOnset.Configure(ControlRate, 0.03f, 0.09f);
		KickOnset.AbsFloor = 0.06f;
		HatOnset.Configure(ControlRate, 0.012f, 0.04f);
		HatOnset.AbsFloor = 0.05f;
		SetSensitivity(SensitivityScale);

		Ring.assign(FftSize, 0.f);
		RingPos = 0;
		SinceFft = 0;
		Window.resize(FftSize);
		for (int i = 0; i < FftSize; ++i)
			Window[i] = 0.5f - 0.5f * std::cos(2.f * Pi * i / (FftSize - 1));
		const float FLo = 30.f, FHi = std::min(16000.f, Fs * 0.45f);
		int Prev = 0;
		for (int b = 0; b <= NumSpectrumBands; ++b)
		{
			const float F = FLo * std::pow(FHi / FLo, (float)b / NumSpectrumBands);
			int Bin = (int)std::lround(F * FftSize / Fs);
			if (b > 0) Bin = std::max(Bin, Prev + 1);
			Bin = std::min(Bin, FftSize / 2);
			SpecEdges[b] = Bin;
			Prev = Bin;
		}
		SpecPeak.fill(0.f);
		SpecEnv.fill(0.f);

		OdfRate = ControlRate / OdfDecim;
		OdfHistory.assign((size_t)(OdfRate * 6.f), 0.f);
		OdfWrite = OdfFilled = 0;
		OdfAccum = 0;
		OdfSub = 0;

		HopSq.fill(0.0);
		HopFill = 0;
	}

	void Analyzer::Process(const float* Mono, int N)
	{
		for (int i = 0; i < N; ++i)
		{
			const float x = Mono[i];

			const float k = KickLP2.Tick(KickLP1.Tick(KickHP.Tick(x)));
			const float b = BassLP2.Tick(BassLP1.Tick(BassHP2.Tick(BassHP1.Tick(x))));
			const float m = MidLP2.Tick(MidLP1.Tick(MidHP2.Tick(MidHP1.Tick(x))));
			const float h = HatHP2.Tick(HatHP1.Tick(x));
			HopSq[0] += k * k; HopSq[1] += b * b; HopSq[2] += m * m; HopSq[3] += h * h; HopSq[4] += x * x;

			Ring[RingPos] = x;
			RingPos = (RingPos + 1) % FftSize;
			if (++SinceFft >= FftHop) { SinceFft = 0; ProcessSpectrum(); }

			if (++HopFill >= Hop)
			{
				AudioTime += (double)Hop / Fs;
				ProcessHop();
				HopSq.fill(0.0);
				HopFill = 0;
			}
		}

		std::lock_guard<std::mutex> Lock(Mutex);
		Published.Kick = Env[0].Value;
		Published.Bass = Env[1].Value;
		Published.Mid = Env[2].Value;
		Published.Hats = Env[3].Value;
		Published.Energy = EnergyEnv.Value;
		Published.Intensity = IntensityEnv.Value;
		Published.Bpm = Bpm;
		Published.BeatConfidence = Confidence;
		Published.KickCount = KickCount;
		Published.HatCount = HatCount;
		Published.BeatCount = BeatCount;
		Published.DropCount = DropCount;
		Published.Spectrum = SpecEnv;
		Published.LatencyMs = DeviceLatencyMs + 1000.f * Hop / Fs;
		PubAudioTime = AudioTime;
		PubLastKick = LastKick;
		PubLastHat = LastHat;
		PubLastDrop = LastDrop;
		PubLastSignal = LastSignal;
		PubLastBeat = NextBeat;
		PubBeatPeriod = BeatPeriod;
		PubWall = std::chrono::steady_clock::now();
	}

	void Analyzer::ProcessHop()
	{
		const double T = AudioTime;
		float Rms[5];
		for (int i = 0; i < 5; ++i) Rms[i] = (float)std::sqrt(HopSq[i] / Hop);

		const bool bSilent = Rms[4] < 2e-4f; // ~ -74 dBFS
		if (!bSilent) LastSignal = T;

		float Norm[5];
		for (int i = 0; i < 5; ++i)
		{
			Peak[i] = std::max(Rms[i], Peak[i] * PeakDecay);
			Norm[i] = bSilent ? 0.f : Clamp01(Rms[i] / std::max(Peak[i], 1e-4f));
		}

		// Pumpy shaping: expand so quiet parts drop away and hits slam to 1.
		for (int i = 0; i < 4; ++i)
		{
			const float Shaped = Clamp01(std::pow(Norm[i], 1.6f) * 1.15f);
			Env[i].Tick(Shaped);
		}
		EnergyEnv.Tick(Norm[4]);

		// Beat grid advances on its own, so beats keep landing on time between kicks.
		while (T >= NextBeat + BeatPeriod)
		{
			NextBeat += BeatPeriod;
			++BeatCount;
		}

		const double KickRefractory = std::max(0.2, BeatPeriod * 0.55);
		KickRef = std::max(Rms[0], KickRef * KickRefDecay);
		const bool bKickOnset = KickOnset.Tick(Norm[0], T, KickRefractory);
		// A real kick reaches a good share of the loudest kicks heard recently. The onset can fire
		// a few ms before the body of the kick arrives, so give it a short window to prove itself.
		if (bKickOnset) PendingKick = T;
		if (PendingKick > 0.0)
		{
			if (Rms[0] > 0.4f * KickRef) { OnKick(PendingKick); PendingKick = -1.0; }
			else if (T - PendingKick > 0.025) PendingKick = -1.0;
		}
		if (HatOnset.Tick(Norm[3], T, 0.05)) OnHat(T);

		// Onset strength history for tempo.
		OdfAccum = std::max(OdfAccum, KickOnset.Odf);
		if (++OdfSub >= OdfDecim)
		{
			OdfHistory[OdfWrite] = OdfAccum;
			OdfWrite = (OdfWrite + 1) % (int)OdfHistory.size();
			OdfFilled = std::min(OdfFilled + 1, (int)OdfHistory.size());
			OdfAccum = 0;
			OdfSub = 0;
		}
		if (T - LastTempoUpdate > 0.5)
		{
			LastTempoUpdate = T;
			UpdateTempo();
		}

		// Intensity: is the kick running, are hats busy, how loud is it.
		const bool bKickRunning = (T - LastKick) < BeatPeriod * 2.2;
		const float Target = bSilent ? 0.f : Clamp01(0.5f * (bKickRunning ? 1.f : 0.f) + 0.25f * Env[3].Value * 1.5f + 0.25f * EnergyEnv.Value);
		IntensityEnv.Tick(Target);
	}

	void Analyzer::OnKick(double T)
	{
		const double Gap = T - LastKick;
		PrevKickBeforeThis = LastKick;
		LastKick = T;
		++KickCount;

		// Drop: kick returns after >= 2 bars without one, while music kept playing.
		if (Gap > BeatPeriod * 8.0 && (T - LastSignal) < 0.5 && PrevKickBeforeThis > 0.0)
		{
			LastDrop = T;
			++DropCount;
			// Treat the drop as a downbeat.
			BeatCount = ((BeatCount + 3) / 4) * 4;
			NextBeat = T - OnsetCompensation;
			return;
		}

		// Phase-locked loop: pull the beat grid toward the kick. Detection lags the true kick onset
		// (band filter group delay + envelope rise), so the grid aligns to the compensated time.
		const double Onset = T - OnsetCompensation;
		double E = Onset - NextBeat;
		if (E > BeatPeriod * 0.5)
		{
			// Kick is early for the next grid beat: fire that beat now.
			NextBeat += BeatPeriod;
			++BeatCount;
			E = Onset - NextBeat;
		}
		if (std::abs(E) < BeatPeriod * 0.25)
		{
			const double Gain = Confidence > 0.5f ? 0.35 : 0.7;
			NextBeat += Gain * E;
			ConsecutiveKicks = 0;
		}
		else if (++ConsecutiveKicks >= 3 || Confidence < 0.3f)
		{
			NextBeat = Onset; // re-anchor
			ConsecutiveKicks = 0;
		}
	}

	void Analyzer::OnHat(double T)
	{
		LastHat = T;
		++HatCount;
	}

	void Analyzer::UpdateTempo()
	{
		const int N = OdfFilled;
		if (N < (int)(OdfRate * 3.f)) return;

		const int Size = (int)OdfHistory.size();
		std::vector<float> X(N);
		double Mean = 0;
		for (int i = 0; i < N; ++i)
		{
			const int Idx = (OdfWrite - N + i + Size) % Size;
			X[i] = OdfHistory[Idx];
			Mean += X[i];
		}
		Mean /= N;
		double Pow = 0;
		for (float& v : X) { v -= (float)Mean; Pow += v * v; }
		if (Pow < 1e-6) { Confidence *= 0.9f; return; }

		auto Ac = [&](float Lag) -> double
		{
			const int L0 = (int)Lag;
			const float Fr = Lag - L0;
			double S = 0;
			for (int i = L0 + 1; i < N; ++i)
			{
				const float Prev = X[i - L0] * (1.f - Fr) + X[i - L0 - 1] * Fr;
				S += X[i] * Prev;
			}
			return S / (N - L0);
		};

		float BestBpm = Bpm;
		double Best = -1e30;
		for (float Cand = 115.f; Cand <= 160.f; Cand += 0.25f)
		{
			const float Lag = 60.f / Cand * OdfRate;
			double Score = Ac(Lag) + 0.5 * Ac(Lag * 2.f);
			if (Lag * 4.f < N - 2) Score += 0.25 * Ac(Lag * 4.f);
			// Mild prior toward the techno sweet spot.
			Score *= 1.0 - 0.0015 * std::abs(Cand - 132.f);
			if (Score > Best) { Best = Score; BestBpm = Cand; }
		}

		const float Conf = Clamp01((float)(Best / (Pow / N) / 1.2));
		Confidence += (Conf - Confidence) * 0.4f;
		if (Conf < 0.15f) return;

		if (std::abs(BestBpm - Bpm) < 3.f)
		{
			Bpm += (BestBpm - Bpm) * 0.35f;
			PendingCount = 0;
		}
		else if (std::abs(BestBpm - PendingBpm) < 2.f)
		{
			if (++PendingCount >= 3) { Bpm = BestBpm; PendingCount = 0; }
		}
		else
		{
			PendingBpm = BestBpm;
			PendingCount = 1;
		}
		BeatPeriod = 60.0 / Bpm;
	}

	void Analyzer::ProcessSpectrum()
	{
		std::vector<std::complex<float>> A(FftSize);
		for (int i = 0; i < FftSize; ++i)
			A[i] = { Ring[(RingPos + i) % FftSize] * Window[i], 0.f };
		Fft(A);

		const float SpecRate = Fs / FftHop;
		const float RelC = Coef(SpecRate, 0.14f);
		const float PeakD = std::pow(0.5f, 1.f / (SpecRate * 8.f));
		for (int b = 0; b < NumSpectrumBands; ++b)
		{
			const int Lo = SpecEdges[b], Hi = std::max(SpecEdges[b + 1], Lo + 1);
			double P = 0;
			for (int k = Lo; k < Hi; ++k) P += std::norm(A[k]);
			const float Mag = (float)std::sqrt(P / (Hi - Lo));
			SpecPeak[b] = std::max(Mag, SpecPeak[b] * PeakD);
			const float V = SpecPeak[b] > 1e-3f ? Clamp01(std::pow(Mag / SpecPeak[b], 1.4f)) : 0.f;
			SpecEnv[b] = V > SpecEnv[b] ? V : SpecEnv[b] + (V - SpecEnv[b]) * RelC;
		}
	}

	double Analyzer::SecondsSinceSignal() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return PubAudioTime - PubLastSignal;
	}

	Features Analyzer::GetSnapshot() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Features F = Published;

		double Dt = 0;
		if (!bUseAudioClockForSnapshot)
		{
			Dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - PubWall).count();
			Dt = std::min(std::max(Dt, 0.0), 0.12);
		}
		const double Now = PubAudioTime + Dt;

		// Smooth release between audio packets.
		if (Dt > 0)
		{
			F.Kick *= (float)std::exp(-Dt / 0.11);
			F.Bass *= (float)std::exp(-Dt / 0.14);
			F.Mid *= (float)std::exp(-Dt / 0.16);
			F.Hats *= (float)std::exp(-Dt / 0.06);
		}

		const double P = PubBeatPeriod;
		F.KickPulse = (float)std::exp(-(Now - PubLastKick) / (0.33 * P));
		F.HatPulse = (float)std::exp(-(Now - PubLastHat) / 0.05);
		F.DropPulse = (float)std::exp(-(Now - PubLastDrop) / 3.0);

		// Locked grid: predict the beat instead of waiting to hear it.
		const bool bKickRunning = (Now - PubLastKick) < 2.2 * P;
		F.bGridLocked = F.BeatConfidence > 0.6f && bKickRunning;
		const double GridNow = F.bGridLocked ? Now + LookaheadMs * 0.001 : Now;

		double Beats = (GridNow - PubLastBeat) / P;
		if (Beats < 0) Beats = 0;
		const int Extra = (int)Beats;
		F.BeatPhase = (float)(Beats - Extra);
		F.BeatCount += Extra;
		F.BarPhase = ((float)(((F.BeatCount % 4) + 4) % 4) + F.BeatPhase) / 4.f;
		if (F.bGridLocked)
		{
			F.KickPulse = std::max(F.KickPulse, (float)std::exp(-F.BeatPhase / 0.33));
		}

		const bool bSignal = (Now - PubLastSignal) < 1.0;
		const float GapBeats = (float)((Now - PubLastKick) / P);
		F.Breakdown = bSignal ? Clamp01((GapBeats - 4.f) / 8.f) : 0.f;
		return F;
	}
}

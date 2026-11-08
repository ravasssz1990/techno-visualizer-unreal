// Techno analysis core. Pure C++17, no Unreal types, so it can be unit-tested on its own
// (see Scripts/analyzer_test.cpp). Designed for low latency: band envelopes and onsets are
// computed in the time domain every 64 samples (~1.3 ms at 48 kHz), not from big FFT frames.
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace TechnoDSP
{
	static constexpr int NumSpectrumBands = 32;

	struct Features
	{
		float Kick = 0, Bass = 0, Mid = 0, Hats = 0;
		float Energy = 0, Intensity = 0;
		float KickPulse = 0, HatPulse = 0, DropPulse = 0, Breakdown = 0;
		float Bpm = 130, BeatPhase = 0, BarPhase = 0, BeatConfidence = 0;
		int32_t KickCount = 0, HatCount = 0, BeatCount = 0, DropCount = 0;
		float LatencyMs = 0;
		/** True when the beat grid is locked to a running kick: BeatCount ticks are then reliable kick predictions. */
		bool bGridLocked = false;
		std::array<float, NumSpectrumBands> Spectrum{};
	};

	struct Biquad
	{
		float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
		float z1 = 0, z2 = 0;
		void SetLowpass(float Fs, float F, float Q);
		void SetHighpass(float Fs, float F, float Q);
		inline float Tick(float x)
		{
			const float y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	/** Fast-attack / slower-release follower on an already rectified control signal. */
	struct Follower
	{
		float Value = 0, Att = 0, Rel = 0;
		void Set(float ControlRate, float AttackSec, float ReleaseSec);
		inline float Tick(float x)
		{
			const float c = x > Value ? Att : Rel;
			Value += (x - Value) * c;
			return Value;
		}
	};

	/** Onset detector: fast/slow envelope difference + adaptive threshold + refractory. */
	struct OnsetDetector
	{
		Follower Fast, Slow;
		float Mean = 0, Var = 0, MeanCoef = 0;
		float Sensitivity = 1.5f;
		float AbsFloor = 0.02f;
		double LastOnset = -10.0;
		bool bArmed = true;
		float Odf = 0;
		void Configure(float ControlRate, float FastRel, float SlowRel);
		/** Returns true on a new onset. Level must be auto-gained 0..1. */
		bool Tick(float Level, double Time, double Refractory);
	};

	class Analyzer
	{
	public:
		Analyzer();
		void Configure(float SampleRate);
		/** Feed mono samples (capture thread). */
		void Process(const float* Mono, int NumFrames);
		/** Extra latency reported by the device (buffer size etc.). */
		void SetDeviceLatencyMs(float Ms) { DeviceLatencyMs = Ms; }
		/** Overall sensitivity, 1 = default. Higher = more kicks/hats detected. */
		void SetSensitivity(float S);
		/**
		 * Beat-grid lookahead in ms. Once the tempo is locked, beat phase and the predicted kick pulse are
		 * computed this far ahead, cancelling display/render latency so the hit lands on screen with the sound.
		 */
		void SetLookaheadMs(float Ms) { LookaheadMs = Ms; }
		/** Thread-safe snapshot, extrapolated to "now" so phases/pulses stay smooth between packets. */
		Features GetSnapshot() const;
		/** Time since any non-silent audio was seen, seconds. */
		double SecondsSinceSignal() const;

		/** Test hook: when set, snapshot uses audio time instead of wall time. */
		bool bUseAudioClockForSnapshot = false;

	private:
		void ProcessHop();
		void ProcessSpectrum();
		void UpdateTempo();
		void OnKick(double T);
		void OnHat(double T);

		float Fs = 48000.f;
		float ControlRate = 750.f;
		static constexpr int Hop = 64;

		// Band filters (two cascaded biquads per edge = 24 dB/oct).
		Biquad KickHP, KickLP1, KickLP2;
		Biquad BassHP1, BassHP2, BassLP1, BassLP2;
		Biquad MidHP1, MidHP2, MidLP1, MidLP2;
		Biquad HatHP1, HatHP2;

		std::array<double, 5> HopSq{}; // kick, bass, mid, hats, broadband
		int HopFill = 0;

		std::array<float, 5> Peak{};
		float PeakDecay = 0.9999f;
		/** Long memory of how loud real kicks are (half-life ~40 s), gates false kicks in breakdowns. */
		float KickRef = 0.f, KickRefDecay = 0.99999f;
		std::array<Follower, 4> Env;
		Follower EnergyEnv, IntensityEnv;
		OnsetDetector KickOnset, HatOnset;
		float SensitivityScale = 1.f;

		// Spectrum
		static constexpr int FftSize = 1024;
		static constexpr int FftHop = 256;
		std::vector<float> Ring;
		int RingPos = 0;
		int SinceFft = 0;
		std::vector<float> Window;
		std::array<float, NumSpectrumBands> SpecPeak{};
		std::array<float, NumSpectrumBands> SpecEnv{};
		std::array<int, NumSpectrumBands + 1> SpecEdges{};

		// Tempo
		std::vector<float> OdfHistory; // onset strength at OdfRate
		int OdfWrite = 0;
		int OdfFilled = 0;
		float OdfAccum = 0;
		int OdfSub = 0;
		static constexpr int OdfDecim = 4; // control hops per ODF sample
		float OdfRate = 187.5f;
		double LastTempoUpdate = 0;
		float Bpm = 130.f;
		float PendingBpm = 0.f;
		int PendingCount = 0;
		float Confidence = 0.f;
		double BeatPeriod = 60.0 / 130.0;
		double NextBeat = 0;
		int32_t BeatCount = 0;

		// Events
		double AudioTime = 0; // seconds of audio processed
		double LastKick = -10, LastHat = -10, LastDrop = -100;
		double LastSignal = -10;
		double PrevKickBeforeThis = -10;
		double PendingKick = -1.0;
		int32_t KickCount = 0, HatCount = 0, DropCount = 0;
		int ConsecutiveKicks = 0;

		float DeviceLatencyMs = 0;
		float LookaheadMs = 20.f;
		static constexpr double OnsetCompensation = 0.02;

		// Published state
		mutable std::mutex Mutex;
		Features Published;
		double PubAudioTime = 0;
		double PubLastKick = -10, PubLastHat = -10, PubLastDrop = -100, PubLastBeat = 0, PubLastSignal = -10;
		double PubBeatPeriod = 0.46;
		std::chrono::steady_clock::time_point PubWall;
	};
}

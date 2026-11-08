#pragma once

#include "CoreMinimal.h"
#include "TechnoAudioTypes.generated.h"

/** Where the visualizer listens. */
UENUM(BlueprintType)
enum class ETechnoAudioSource : uint8
{
	/** What the PC is playing (WASAPI loopback of the default output). */
	Loopback,
	/** An external input device: mic, line-in, DJ mixer, audio interface. */
	Input,
	/** Both at once; the louder analysis wins per feature. */
	Both,
	/** Built-in techno loop (silent), to see the show without music. */
	Demo
};

/** Number of log-spaced spectrum bands published every frame. */
static constexpr int32 TECHNO_SPECTRUM_BANDS = 32;

/**
 * One snapshot of the music, published from the capture thread.
 * All "level" values are auto-gained to roughly 0..1 so the visuals hit hard at any volume.
 */
USTRUCT(BlueprintType)
struct FTechnoAudioFrame
{
	GENERATED_BODY()

	/** Kick drum envelope (40-120 Hz), fast attack. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Kick = 0.f;
	/** Bassline envelope (120-350 Hz). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Bass = 0.f;
	/** Mids: synths, stabs, claps (350 Hz - 4 kHz). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Mid = 0.f;
	/** Hi-hats, rides, shakers (6 kHz+). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Hats = 0.f;

	/** Overall loudness, slow. 0 = silence. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Energy = 0.f;
	/** Intensity of the track section, 0 = breakdown, 1 = peak-time banger. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Intensity = 0.f;

	/** 1 right on a detected kick, decaying to 0 over ~a beat. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float KickPulse = 0.f;
	/** 1 on a detected hat/clap transient, decaying fast. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float HatPulse = 0.f;
	/** 1 when the kick returns after a breakdown, decaying over ~2 bars. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float DropPulse = 0.f;
	/** 0..1 while a breakdown/build is going on (no kick for a while). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Breakdown = 0.f;

	/** Tracked tempo. Techno range is clamped to 115-160. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float Bpm = 130.f;
	/** 0..1 phase inside the current beat (0 = on the kick). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float BeatPhase = 0.f;
	/** 0..1 phase inside the current 4-beat bar. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float BarPhase = 0.f;
	/** Confidence of the tempo lock, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float BeatConfidence = 0.f;

	/** Counts every detected kick. Compare with a previous value to detect new kicks. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") int32 KickCount = 0;
	/** Counts every detected hat transient. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") int32 HatCount = 0;
	/** Counts beats on the tracked grid (also ticks during breakdowns). */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") int32 BeatCount = 0;
	/** Counts drops. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") int32 DropCount = 0;

	/** Log-spaced spectrum, 30 Hz - 16 kHz, auto-gained 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") TArray<float> Spectrum;

	/** True when the beat grid is locked to a running kick; BeatCount then predicts kicks with zero latency. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") bool bGridLocked = false;

	/** Seconds of audio latency from the device (buffer) plus analysis. */
	UPROPERTY(BlueprintReadOnly, Category = "Techno") float LatencyMs = 0.f;
};

/** A capture device as seen by Windows. */
USTRUCT(BlueprintType)
struct FTechnoAudioDevice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Techno") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "Techno") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Techno") bool bIsLoopback = false;
	UPROPERTY(BlueprintReadOnly, Category = "Techno") bool bIsDefault = false;
};

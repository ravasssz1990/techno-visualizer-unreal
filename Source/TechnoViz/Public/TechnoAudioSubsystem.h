#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "TechnoAudioTypes.h"
#include "TechnoAudioSubsystem.generated.h"

class FTechnoWasapiCapture;
namespace TechnoDSP { class Analyzer; class DemoSynth; struct Features; }

/**
 * Owns the audio capture threads and the techno analyzers.
 * Anything in the game can call GetFrame() any time; it is cheap and always current.
 */
UCLASS()
class TECHNOVIZ_API UTechnoAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Latest music snapshot, extrapolated to this exact moment. */
	UFUNCTION(BlueprintPure, Category = "Techno")
	FTechnoAudioFrame GetFrame() const;

	UFUNCTION(BlueprintCallable, Category = "Techno")
	void SetSource(ETechnoAudioSource NewSource);

	UFUNCTION(BlueprintPure, Category = "Techno")
	ETechnoAudioSource GetSource() const { return Source; }

	/** Step through input devices (mic / line-in / interface). */
	UFUNCTION(BlueprintCallable, Category = "Techno")
	void CycleInputDevice(int32 Direction);

	/** Step through output devices to listen to (loopback). */
	UFUNCTION(BlueprintCallable, Category = "Techno")
	void CycleLoopbackDevice(int32 Direction);

	/** 1 = default. Higher catches quieter kicks/hats. */
	UFUNCTION(BlueprintCallable, Category = "Techno")
	void SetSensitivity(float NewSensitivity);

	UFUNCTION(BlueprintPure, Category = "Techno")
	float GetSensitivity() const { return Sensitivity; }

	/** Beat-grid lookahead to cancel render/display latency, ms. */
	UFUNCTION(BlueprintCallable, Category = "Techno")
	void SetLookaheadMs(float Ms);

	UFUNCTION(BlueprintPure, Category = "Techno")
	float GetLookaheadMs() const { return LookaheadMs; }

	UFUNCTION(BlueprintPure, Category = "Techno")
	TArray<FTechnoAudioDevice> GetDevices() const { return Devices; }

	/** One line per active stream, for the on-screen overlay. */
	UFUNCTION(BlueprintPure, Category = "Techno")
	FString GetStatusText() const;

private:
	void RestartCapture();
	void StopAll();
	bool Watchdog(float Dt);
	void LoadSettings();
	void SaveSettings() const;
	static void ToFrame(const TechnoDSP::Features& F, FTechnoAudioFrame& Out);

	ETechnoAudioSource Source = ETechnoAudioSource::Loopback;
	FString InputDeviceId;
	FString LoopbackDeviceId;
	float Sensitivity = 1.f;
	float LookaheadMs = 20.f;
	TArray<FTechnoAudioDevice> Devices;

	TSharedPtr<TechnoDSP::Analyzer> LoopbackAnalyzer;
	TSharedPtr<TechnoDSP::Analyzer> InputAnalyzer;
	TSharedPtr<FTechnoWasapiCapture> LoopbackCapture;
	TSharedPtr<FTechnoWasapiCapture> InputCapture;
	TSharedPtr<TechnoDSP::DemoSynth> Demo;
	double DemoClock = 0;
	bool TickDemo(float Dt);
	FTSTicker::FDelegateHandle DemoHandle;

	FTSTicker::FDelegateHandle WatchdogHandle;
	double LastRestart = 0;
};

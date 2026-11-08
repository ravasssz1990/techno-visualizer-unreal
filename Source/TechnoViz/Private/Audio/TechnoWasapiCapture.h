// Event-driven WASAPI capture of one endpoint (loopback of an output, or an input device).
// Runs its own thread at MMCSS "Pro Audio" priority and feeds a TechnoDSP::Analyzer directly,
// so analysis happens the moment a packet arrives (no game-thread hop, no extra buffering).
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "TechnoAudioTypes.h"
#include "TechnoAnalyzer.h"

#include <atomic>

class FTechnoWasapiCapture : public FRunnable
{
public:
	/** DeviceId empty = system default. bLoopback = capture what a render device plays. */
	FTechnoWasapiCapture(const FString& InDeviceId, bool bInLoopback, TSharedRef<TechnoDSP::Analyzer> InAnalyzer);
	virtual ~FTechnoWasapiCapture() override;

	bool Start();
	void StopCapture();

	bool IsRunning() const { return bRunning.load(); }
	FString GetLastError() const;
	FString GetDeviceName() const { return DeviceName; }
	float GetPeak() const { return Peak.load(); }

	/** Lists active endpoints. Render endpoints are returned with bIsLoopback = true. */
	static TArray<FTechnoAudioDevice> EnumerateDevices();

	// FRunnable
	virtual uint32 Run() override;
	virtual void Stop() override { bStopRequested = true; }

private:
	FString DeviceId;
	bool bLoopback;
	TSharedRef<TechnoDSP::Analyzer> Analyzer;
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bStopRequested{ false };
	std::atomic<bool> bRunning{ false };
	std::atomic<float> Peak{ 0.f };
	mutable FCriticalSection ErrorLock;
	FString LastError;
	FString DeviceName;

	void SetError(const FString& E);
};

#include "TechnoWasapiCapture.h"

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include <algorithm>
#include <vector>

#if PLATFORM_WINDOWS
namespace
{
	template <typename T>
	void SafeRelease(T*& P)
	{
		if (P) { P->Release(); P = nullptr; }
	}

	// PKEY_Device_FriendlyName, defined locally so no INITGUID / extra lib is needed.
	const PROPERTYKEY TechnoPKEY_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

	FString GetFriendlyName(IMMDevice* Device)
	{
		FString Result;
		IPropertyStore* Store = nullptr;
		if (SUCCEEDED(Device->OpenPropertyStore(STGM_READ, &Store)))
		{
			PROPVARIANT Var;
			PropVariantInit(&Var);
			if (SUCCEEDED(Store->GetValue(TechnoPKEY_FriendlyName, &Var)) && Var.vt == VT_LPWSTR)
			{
				Result = Var.pwszVal;
			}
			PropVariantClear(&Var);
			Store->Release();
		}
		return Result;
	}

	FString GetId(IMMDevice* Device)
	{
		FString Result;
		LPWSTR Id = nullptr;
		if (SUCCEEDED(Device->GetId(&Id)) && Id)
		{
			Result = Id;
			CoTaskMemFree(Id);
		}
		return Result;
	}

	FString GetDefaultId(IMMDeviceEnumerator* Enum, EDataFlow Flow)
	{
		IMMDevice* D = nullptr;
		FString Id;
		if (SUCCEEDED(Enum->GetDefaultAudioEndpoint(Flow, eConsole, &D)))
		{
			Id = GetId(D);
			D->Release();
		}
		return Id;
	}

	struct FComScope
	{
		bool bOwns;
		FComScope() { bOwns = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
		~FComScope() { if (bOwns) CoUninitialize(); }
	};
}
#endif

FTechnoWasapiCapture::FTechnoWasapiCapture(const FString& InDeviceId, bool bInLoopback, TSharedRef<TechnoDSP::Analyzer> InAnalyzer)
	: DeviceId(InDeviceId), bLoopback(bInLoopback), Analyzer(InAnalyzer)
{
}

FTechnoWasapiCapture::~FTechnoWasapiCapture()
{
	StopCapture();
}

bool FTechnoWasapiCapture::Start()
{
	if (Thread) return true;
	bStopRequested = false;
	Thread = FRunnableThread::Create(this, bLoopback ? TEXT("TechnoLoopbackCapture") : TEXT("TechnoInputCapture"), 0, TPri_TimeCritical);
	return Thread != nullptr;
}

void FTechnoWasapiCapture::StopCapture()
{
	if (Thread)
	{
		bStopRequested = true;
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

FString FTechnoWasapiCapture::GetLastError() const
{
	FScopeLock Lock(&ErrorLock);
	return LastError;
}

void FTechnoWasapiCapture::SetError(const FString& E)
{
	FScopeLock Lock(&ErrorLock);
	LastError = E;
	UE_LOG(LogTemp, Warning, TEXT("TechnoViz audio: %s"), *E);
}

TArray<FTechnoAudioDevice> FTechnoWasapiCapture::EnumerateDevices()
{
	TArray<FTechnoAudioDevice> Out;
#if PLATFORM_WINDOWS
	FComScope Com;
	IMMDeviceEnumerator* Enum = nullptr;
	if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&Enum)))
	{
		return Out;
	}
	const FString DefRender = GetDefaultId(Enum, eRender);
	const FString DefCapture = GetDefaultId(Enum, eCapture);

	for (EDataFlow Flow : { eRender, eCapture })
	{
		IMMDeviceCollection* Coll = nullptr;
		if (FAILED(Enum->EnumAudioEndpoints(Flow, DEVICE_STATE_ACTIVE, &Coll))) continue;
		UINT Count = 0;
		Coll->GetCount(&Count);
		for (UINT i = 0; i < Count; ++i)
		{
			IMMDevice* D = nullptr;
			if (FAILED(Coll->Item(i, &D))) continue;
			FTechnoAudioDevice Info;
			Info.Id = GetId(D);
			Info.Name = GetFriendlyName(D);
			Info.bIsLoopback = (Flow == eRender);
			Info.bIsDefault = Info.Id == (Flow == eRender ? DefRender : DefCapture);
			Out.Add(Info);
			D->Release();
		}
		Coll->Release();
	}
	Enum->Release();
#endif
	return Out;
}

uint32 FTechnoWasapiCapture::Run()
{
#if PLATFORM_WINDOWS
	FComScope Com;
	bRunning = true;

	IMMDeviceEnumerator* Enum = nullptr;
	IMMDevice* Device = nullptr;
	IAudioClient* Client = nullptr;
	IAudioCaptureClient* Capture = nullptr;
	WAVEFORMATEX* Fmt = nullptr;
	HANDLE Event = nullptr;
	HANDLE MmTask = nullptr;

	auto Cleanup = [&]()
	{
		if (Client) Client->Stop();
		if (MmTask) AvRevertMmThreadCharacteristics(MmTask);
		if (Event) CloseHandle(Event);
		if (Fmt) CoTaskMemFree(Fmt);
		SafeRelease(Capture);
		SafeRelease(Client);
		SafeRelease(Device);
		SafeRelease(Enum);
		bRunning = false;
	};

	HRESULT Hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&Enum);
	if (FAILED(Hr)) { SetError(TEXT("Could not create device enumerator")); Cleanup(); return 1; }

	if (DeviceId.IsEmpty())
		Hr = Enum->GetDefaultAudioEndpoint(bLoopback ? eRender : eCapture, eConsole, &Device);
	else
		Hr = Enum->GetDevice(*DeviceId, &Device);
	if (FAILED(Hr)) { SetError(TEXT("Audio device not found")); Cleanup(); return 1; }
	DeviceName = GetFriendlyName(Device);

	Hr = Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&Client);
	if (FAILED(Hr)) { SetError(TEXT("Could not open audio device")); Cleanup(); return 1; }

	Client->GetMixFormat(&Fmt);
	const int32 Channels = Fmt->nChannels;
	const int32 Bits = Fmt->wBitsPerSample;
	bool bFloat = Fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
	if (Fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		const WAVEFORMATEXTENSIBLE* Ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Fmt);
		bFloat = Ext->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
	}

	DWORD Flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST;
	if (bLoopback) Flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

	REFERENCE_TIME DefaultPeriod = 0, MinPeriod = 0;
	Client->GetDevicePeriod(&DefaultPeriod, &MinPeriod);
	// Smallest shared-mode buffer the engine accepts: one device period (usually 10 ms).
	Hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, Flags, DefaultPeriod, 0, Fmt, nullptr);
	if (FAILED(Hr)) { SetError(FString::Printf(TEXT("Audio initialize failed (0x%08x)"), (uint32)Hr)); Cleanup(); return 1; }

	Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	Client->SetEventHandle(Event);
	Hr = Client->GetService(__uuidof(IAudioCaptureClient), (void**)&Capture);
	if (FAILED(Hr)) { SetError(TEXT("No capture service")); Cleanup(); return 1; }

	REFERENCE_TIME StreamLatency = 0;
	Client->GetStreamLatency(&StreamLatency);
	const float LatencyMs = (float)(StreamLatency + DefaultPeriod) / 10000.f;

	Analyzer->Configure((float)Fmt->nSamplesPerSec);
	Analyzer->SetDeviceLatencyMs(LatencyMs);

	DWORD TaskIndex = 0;
	MmTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &TaskIndex);

	Hr = Client->Start();
	if (FAILED(Hr)) { SetError(TEXT("Audio start failed")); Cleanup(); return 1; }
	{
		FScopeLock Lock(&ErrorLock);
		LastError.Empty();
	}

	std::vector<float> Mono;
	const double SampleRate = Fmt->nSamplesPerSec;
	double LastData = FPlatformTime::Seconds();
	float PeakHold = 0.f;

	while (!bStopRequested)
	{
		// Loopback can skip event signalling on some drivers; the timeout doubles as a poll.
		WaitForSingleObject(Event, 5);

		UINT32 PacketFrames = 0;
		Hr = Capture->GetNextPacketSize(&PacketFrames);
		if (Hr == AUDCLNT_E_DEVICE_INVALIDATED) { SetError(TEXT("Audio device was removed or changed")); break; }

		bool bGotData = false;
		while (SUCCEEDED(Hr) && PacketFrames > 0)
		{
			BYTE* Data = nullptr;
			UINT32 Frames = 0;
			DWORD BufFlags = 0;
			Hr = Capture->GetBuffer(&Data, &Frames, &BufFlags, nullptr, nullptr);
			if (FAILED(Hr)) break;

			Mono.resize(Frames);
			if ((BufFlags & AUDCLNT_BUFFERFLAGS_SILENT) || !Data)
			{
				std::fill(Mono.begin(), Mono.end(), 0.f);
			}
			else
			{
				const float Inv = 1.f / Channels;
				for (UINT32 f = 0; f < Frames; ++f)
				{
					float Sum = 0.f;
					for (int32 c = 0; c < Channels; ++c)
					{
						const int32 Idx = f * Channels + c;
						float S;
						if (bFloat) S = reinterpret_cast<const float*>(Data)[Idx];
						else if (Bits == 16) S = reinterpret_cast<const int16*>(Data)[Idx] / 32768.f;
						else if (Bits == 32) S = reinterpret_cast<const int32*>(Data)[Idx] / 2147483648.f;
						else
						{
							const uint8* P = Data + Idx * 3;
							const int32 V = (int32)((uint32)P[0] << 8 | (uint32)P[1] << 16 | (uint32)P[2] << 24) >> 8;
							S = V / 8388608.f;
						}
						Sum += S;
					}
					Mono[f] = Sum * Inv;
					PeakHold = FMath::Max(PeakHold * 0.99995f, FMath::Abs(Mono[f]));
				}
			}
			Capture->ReleaseBuffer(Frames);
			Analyzer->Process(Mono.data(), (int)Frames);
			bGotData = true;
			Hr = Capture->GetNextPacketSize(&PacketFrames);
		}

		const double Now = FPlatformTime::Seconds();
		if (bGotData)
		{
			LastData = Now;
		}
		else if (Now - LastData > 0.02)
		{
			// Loopback delivers nothing while Windows plays nothing: keep the analysis clock moving.
			const int32 Frames = (int32)((Now - LastData) * SampleRate);
			Mono.assign(Frames, 0.f);
			Analyzer->Process(Mono.data(), Frames);
			LastData = Now;
			PeakHold *= 0.9f;
		}
		Peak = PeakHold;
	}

	Cleanup();
	return 0;
#else
	SetError(TEXT("Audio capture is only implemented for Windows"));
	return 1;
#endif
}

#include "TechnoAudioSubsystem.h"

#include "Audio/TechnoAnalyzer.h"
#include "Audio/TechnoWasapiCapture.h"
#include "Audio/TechnoDemoSynth.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static const TCHAR* SettingsSection = TEXT("TechnoViz.Audio");

void UTechnoAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSettings();
	Devices = FTechnoWasapiCapture::EnumerateDevices();
	RestartCapture();
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTechnoAudioSubsystem::Watchdog), 1.0f);
}

void UTechnoAudioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(WatchdogHandle);
	StopAll();
	Super::Deinitialize();
}

void UTechnoAudioSubsystem::StopAll()
{
	LoopbackCapture.Reset();
	InputCapture.Reset();
	if (DemoHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DemoHandle);
		DemoHandle.Reset();
	}
	Demo.Reset();
}

bool UTechnoAudioSubsystem::TickDemo(float)
{
	// Generate exactly as much demo audio as real time has passed.
	if (!Demo || !LoopbackAnalyzer) return true;
	const double Now = FPlatformTime::Seconds();
	const int32 Frames = FMath::Clamp((int32)((Now - DemoClock) * 48000.0), 0, 48000);
	if (Frames <= 0) return true;
	DemoClock += Frames / 48000.0;
	TArray<float> Buf;
	Buf.SetNumUninitialized(Frames);
	Demo->Render(Buf.GetData(), Frames);
	LoopbackAnalyzer->Process(Buf.GetData(), Frames);
	return true;
}

void UTechnoAudioSubsystem::RestartCapture()
{
	StopAll();
	LastRestart = FPlatformTime::Seconds();

	if (Source == ETechnoAudioSource::Demo)
	{
		// The demo loop is fed through the loopback analyzer slot.
		LoopbackAnalyzer = MakeShared<TechnoDSP::Analyzer>();
		LoopbackAnalyzer->Configure(48000.f);
		LoopbackAnalyzer->SetSensitivity(Sensitivity);
		LoopbackAnalyzer->SetLookaheadMs(LookaheadMs);
		InputAnalyzer.Reset();
		Demo = MakeShared<TechnoDSP::DemoSynth>(48000.f, 132.f);
		DemoClock = FPlatformTime::Seconds();
		DemoHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UTechnoAudioSubsystem::TickDemo), 0.f);
		return;
	}

	if (Source == ETechnoAudioSource::Loopback || Source == ETechnoAudioSource::Both)
	{
		LoopbackAnalyzer = MakeShared<TechnoDSP::Analyzer>();
		LoopbackAnalyzer->SetSensitivity(Sensitivity);
		LoopbackAnalyzer->SetLookaheadMs(LookaheadMs);
		LoopbackCapture = MakeShared<FTechnoWasapiCapture>(LoopbackDeviceId, true, LoopbackAnalyzer.ToSharedRef());
		LoopbackCapture->Start();
	}
	else
	{
		LoopbackAnalyzer.Reset();
	}

	if (Source == ETechnoAudioSource::Input || Source == ETechnoAudioSource::Both)
	{
		InputAnalyzer = MakeShared<TechnoDSP::Analyzer>();
		InputAnalyzer->SetSensitivity(Sensitivity);
		InputAnalyzer->SetLookaheadMs(LookaheadMs);
		InputCapture = MakeShared<FTechnoWasapiCapture>(InputDeviceId, false, InputAnalyzer.ToSharedRef());
		InputCapture->Start();
	}
	else
	{
		InputAnalyzer.Reset();
	}
}

bool UTechnoAudioSubsystem::Watchdog(float)
{
	// Restart streams that died (device unplugged, default output switched, driver reset).
	const double Now = FPlatformTime::Seconds();
	if (Now - LastRestart < 2.0) return true;
	const bool bDead = (LoopbackCapture && !LoopbackCapture->IsRunning()) || (InputCapture && !InputCapture->IsRunning());
	if (bDead)
	{
		Devices = FTechnoWasapiCapture::EnumerateDevices();
		RestartCapture();
	}
	return true;
}

void UTechnoAudioSubsystem::ToFrame(const TechnoDSP::Features& F, FTechnoAudioFrame& Out)
{
	Out.Kick = F.Kick; Out.Bass = F.Bass; Out.Mid = F.Mid; Out.Hats = F.Hats;
	Out.Energy = F.Energy; Out.Intensity = F.Intensity;
	Out.KickPulse = F.KickPulse; Out.HatPulse = F.HatPulse; Out.DropPulse = F.DropPulse; Out.Breakdown = F.Breakdown;
	Out.Bpm = F.Bpm; Out.BeatPhase = F.BeatPhase; Out.BarPhase = F.BarPhase; Out.BeatConfidence = F.BeatConfidence;
	Out.KickCount = F.KickCount; Out.HatCount = F.HatCount; Out.BeatCount = F.BeatCount; Out.DropCount = F.DropCount;
	Out.LatencyMs = F.LatencyMs;
	Out.bGridLocked = F.bGridLocked;
	Out.Spectrum.SetNumUninitialized(TechnoDSP::NumSpectrumBands);
	for (int32 i = 0; i < TechnoDSP::NumSpectrumBands; ++i) Out.Spectrum[i] = F.Spectrum[i];
}

FTechnoAudioFrame UTechnoAudioSubsystem::GetFrame() const
{
	FTechnoAudioFrame Out;
	const bool bL = LoopbackAnalyzer.IsValid();
	const bool bI = InputAnalyzer.IsValid();
	if (!bL && !bI)
	{
		Out.Spectrum.Init(0.f, TECHNO_SPECTRUM_BANDS);
		return Out;
	}
	if (bL != bI)
	{
		ToFrame((bL ? LoopbackAnalyzer : InputAnalyzer)->GetSnapshot(), Out);
		return Out;
	}

	// Both: the stream that is actually playing music drives timing; levels take the max.
	const TechnoDSP::Features A = LoopbackAnalyzer->GetSnapshot();
	const TechnoDSP::Features B = InputAnalyzer->GetSnapshot();
	const TechnoDSP::Features& Lead = (A.Energy * (0.5f + A.BeatConfidence) >= B.Energy * (0.5f + B.BeatConfidence)) ? A : B;
	ToFrame(Lead, Out);
	Out.Kick = FMath::Max(A.Kick, B.Kick);
	Out.Bass = FMath::Max(A.Bass, B.Bass);
	Out.Mid = FMath::Max(A.Mid, B.Mid);
	Out.Hats = FMath::Max(A.Hats, B.Hats);
	Out.Energy = FMath::Max(A.Energy, B.Energy);
	Out.Intensity = FMath::Max(A.Intensity, B.Intensity);
	Out.HatPulse = FMath::Max(A.HatPulse, B.HatPulse);
	for (int32 i = 0; i < TechnoDSP::NumSpectrumBands; ++i) Out.Spectrum[i] = FMath::Max(A.Spectrum[i], B.Spectrum[i]);
	return Out;
}

void UTechnoAudioSubsystem::SetSource(ETechnoAudioSource NewSource)
{
	Source = NewSource;
	SaveSettings();
	RestartCapture();
}

static void CycleIn(const TArray<FTechnoAudioDevice>& Devices, bool bLoopback, FString& InOutId, int32 Dir)
{
	TArray<const FTechnoAudioDevice*> List;
	for (const FTechnoAudioDevice& D : Devices) if (D.bIsLoopback == bLoopback) List.Add(&D);
	if (List.Num() == 0) return;
	// Index -1 = "system default".
	int32 Cur = -1;
	for (int32 i = 0; i < List.Num(); ++i) if (List[i]->Id == InOutId) Cur = i;
	int32 Next = Cur + (Dir >= 0 ? 1 : -1);
	if (Next >= List.Num()) Next = -1;
	if (Next < -1) Next = List.Num() - 1;
	InOutId = Next < 0 ? FString() : List[Next]->Id;
}

void UTechnoAudioSubsystem::CycleInputDevice(int32 Direction)
{
	Devices = FTechnoWasapiCapture::EnumerateDevices();
	CycleIn(Devices, false, InputDeviceId, Direction);
	SaveSettings();
	RestartCapture();
}

void UTechnoAudioSubsystem::CycleLoopbackDevice(int32 Direction)
{
	Devices = FTechnoWasapiCapture::EnumerateDevices();
	CycleIn(Devices, true, LoopbackDeviceId, Direction);
	SaveSettings();
	RestartCapture();
}

void UTechnoAudioSubsystem::SetSensitivity(float NewSensitivity)
{
	Sensitivity = FMath::Clamp(NewSensitivity, 0.25f, 4.f);
	if (LoopbackAnalyzer) LoopbackAnalyzer->SetSensitivity(Sensitivity);
	if (InputAnalyzer) InputAnalyzer->SetSensitivity(Sensitivity);
	SaveSettings();
}

void UTechnoAudioSubsystem::SetLookaheadMs(float Ms)
{
	LookaheadMs = FMath::Clamp(Ms, 0.f, 120.f);
	if (LoopbackAnalyzer) LoopbackAnalyzer->SetLookaheadMs(LookaheadMs);
	if (InputAnalyzer) InputAnalyzer->SetLookaheadMs(LookaheadMs);
	SaveSettings();
}

FString UTechnoAudioSubsystem::GetStatusText() const
{
	if (Source == ETechnoAudioSource::Demo) return TEXT("Demo loop (built-in, silent)\n");
	auto Line = [](const TCHAR* Label, const TSharedPtr<FTechnoWasapiCapture>& C) -> FString
	{
		if (!C) return FString();
		const FString Err = C->GetLastError();
		const FString Name = C->GetDeviceName().IsEmpty() ? TEXT("(opening...)") : C->GetDeviceName();
		return FString::Printf(TEXT("%s: %s  peak %.2f %s\n"), Label, *Name, C->GetPeak(), Err.IsEmpty() ? TEXT("") : *FString(TEXT("ERROR ") + Err));
	};
	return Line(TEXT("PC output"), LoopbackCapture) + Line(TEXT("Input"), InputCapture);
}

void UTechnoAudioSubsystem::LoadSettings()
{
	if (!GConfig) return;
	int32 Src = (int32)Source;
	GConfig->GetInt(SettingsSection, TEXT("Source"), Src, GGameUserSettingsIni);
	Source = (ETechnoAudioSource)FMath::Clamp(Src, 0, 3);
	GConfig->GetString(SettingsSection, TEXT("InputDevice"), InputDeviceId, GGameUserSettingsIni);
	GConfig->GetString(SettingsSection, TEXT("LoopbackDevice"), LoopbackDeviceId, GGameUserSettingsIni);
	GConfig->GetFloat(SettingsSection, TEXT("Sensitivity"), Sensitivity, GGameUserSettingsIni);
	GConfig->GetFloat(SettingsSection, TEXT("LookaheadMs"), LookaheadMs, GGameUserSettingsIni);
	// -TechnoDemo on the command line forces the built-in loop (handy for screenshots and tests).
	if (FParse::Param(FCommandLine::Get(), TEXT("TechnoDemo"))) Source = ETechnoAudioSource::Demo;
}

void UTechnoAudioSubsystem::SaveSettings() const
{
	if (!GConfig) return;
	GConfig->SetInt(SettingsSection, TEXT("Source"), (int32)Source, GGameUserSettingsIni);
	GConfig->SetString(SettingsSection, TEXT("InputDevice"), *InputDeviceId, GGameUserSettingsIni);
	GConfig->SetString(SettingsSection, TEXT("LoopbackDevice"), *LoopbackDeviceId, GGameUserSettingsIni);
	GConfig->SetFloat(SettingsSection, TEXT("Sensitivity"), Sensitivity, GGameUserSettingsIni);
	GConfig->SetFloat(SettingsSection, TEXT("LookaheadMs"), LookaheadMs, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

#include "TechnoGameMode.h"

#include "TechnoAudioSubsystem.h"
#include "TechnoStageDirector.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/CameraActor.h"

ATechnoGameMode::ATechnoGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ATechnoPlayerController::StaticClass();
	HUDClass = ATechnoHUD::StaticClass();
}

void ATechnoGameMode::StartPlay()
{
	// An empty level still gets the full show.
	bool bHasDirector = false;
	for (TActorIterator<ATechnoStageDirector> It(GetWorld()); It; ++It) { bHasDirector = true; break; }
	if (!bHasDirector)
	{
		GetWorld()->SpawnActor<ATechnoStageDirector>(ATechnoStageDirector::StaticClass(), FTransform::Identity);
	}
	Super::StartPlay();
}

// ---------------------------------------------------------------- controller

ATechnoPlayerController::ATechnoPlayerController()
{
	bShowMouseCursor = false;
	// The stage director owns the camera.
	bAutoManageActiveCameraTarget = false;
}

void ATechnoPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetInputMode(FInputModeGameOnly());
	// Lowest-latency presentation: no vsync, no frame cap.
	if (GEngine && GEngine->GetGameUserSettings())
	{
		UGameUserSettings* S = GEngine->GetGameUserSettings();
		S->SetVSyncEnabled(false);
		S->SetFrameRateLimit(0.f);
		S->ApplySettings(false);
	}
	Say(TEXT("H = help / overlay"));
}

ATechnoStageDirector* ATechnoPlayerController::FindDirector()
{
	if (!Director.IsValid())
	{
		for (TActorIterator<ATechnoStageDirector> It(GetWorld()); It; ++It) { Director = *It; break; }
	}
	return Director.Get();
}

void ATechnoPlayerController::Say(const FString& Msg)
{
	Toast = Msg;
	ToastTime = 2.5f;
}

void ATechnoPlayerController::PlayerTick(float Dt)
{
	Super::PlayerTick(Dt);
	ToastTime = FMath::Max(0.f, ToastTime - Dt);

	UTechnoAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTechnoAudioSubsystem>() : nullptr;
	ATechnoStageDirector* D = FindDirector();
	if (D && D->GetCamera() && GetViewTarget() != D->GetCamera())
	{
		SetViewTarget(D->GetCamera());
	}
	auto Pressed = [this](const FKey& K) { return WasInputKeyJustPressed(K); };

	if (Pressed(EKeys::H) || Pressed(EKeys::F1)) bShowOverlay = !bShowOverlay;
	if (Pressed(EKeys::Escape)) UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
	if (Pressed(EKeys::F11) && GEngine && GEngine->GetGameUserSettings())
	{
		UGameUserSettings* S = GEngine->GetGameUserSettings();
		const bool bFull = S->GetFullscreenMode() != EWindowMode::Windowed;
		S->SetFullscreenMode(bFull ? EWindowMode::Windowed : EWindowMode::WindowedFullscreen);
		if (!bFull) S->SetScreenResolution(S->GetDesktopResolution());
		S->ApplySettings(false);
	}

	if (Audio)
	{
		if (Pressed(EKeys::One)) { Audio->SetSource(ETechnoAudioSource::Loopback); Say(TEXT("Listening to: PC output")); }
		if (Pressed(EKeys::Two)) { Audio->SetSource(ETechnoAudioSource::Input); Say(TEXT("Listening to: input device")); }
		if (Pressed(EKeys::Three)) { Audio->SetSource(ETechnoAudioSource::Both); Say(TEXT("Listening to: PC output + input")); }
		if (Pressed(EKeys::Four)) { Audio->SetSource(ETechnoAudioSource::Demo); Say(TEXT("Demo loop")); }
		if (Pressed(EKeys::RightBracket)) { Audio->CycleInputDevice(1); Say(TEXT("Next input device")); }
		if (Pressed(EKeys::LeftBracket)) { Audio->CycleInputDevice(-1); Say(TEXT("Previous input device")); }
		if (Pressed(EKeys::Apostrophe)) { Audio->CycleLoopbackDevice(1); Say(TEXT("Next output device")); }
		if (Pressed(EKeys::Semicolon)) { Audio->CycleLoopbackDevice(-1); Say(TEXT("Previous output device")); }
		if (Pressed(EKeys::Equals) || Pressed(EKeys::Add)) { Audio->SetSensitivity(Audio->GetSensitivity() * 1.15f); Say(FString::Printf(TEXT("Sensitivity %.2f"), Audio->GetSensitivity())); }
		if (Pressed(EKeys::Hyphen) || Pressed(EKeys::Subtract)) { Audio->SetSensitivity(Audio->GetSensitivity() / 1.15f); Say(FString::Printf(TEXT("Sensitivity %.2f"), Audio->GetSensitivity())); }
		if (Pressed(EKeys::Period)) { Audio->SetLookaheadMs(Audio->GetLookaheadMs() + 5.f); Say(FString::Printf(TEXT("Beat lookahead %.0f ms"), Audio->GetLookaheadMs())); }
		if (Pressed(EKeys::Comma)) { Audio->SetLookaheadMs(Audio->GetLookaheadMs() - 5.f); Say(FString::Printf(TEXT("Beat lookahead %.0f ms"), Audio->GetLookaheadMs())); }
	}
	if (D)
	{
		if (Pressed(EKeys::C)) { D->bAutoCamera = false; D->NextCameraShot(); Say(TEXT("Next shot (auto camera off, A = auto)")); }
		if (Pressed(EKeys::A)) { D->bAutoCamera = !D->bAutoCamera; Say(D->bAutoCamera ? TEXT("Auto camera on") : TEXT("Auto camera off")); }
		if (Pressed(EKeys::P)) { D->NextPalette(); Say(TEXT("Next palette")); }
		if (Pressed(EKeys::Up)) { D->Punch = FMath::Min(3.f, D->Punch + 0.1f); Say(FString::Printf(TEXT("Punch %.1f"), D->Punch)); }
		if (Pressed(EKeys::Down)) { D->Punch = FMath::Max(0.f, D->Punch - 0.1f); Say(FString::Printf(TEXT("Punch %.1f"), D->Punch)); }
		if (Pressed(EKeys::PageUp)) { D->ExposureBias += 0.25f; Say(FString::Printf(TEXT("Exposure %+.2f"), D->ExposureBias)); }
		if (Pressed(EKeys::PageDown)) { D->ExposureBias -= 0.25f; Say(FString::Printf(TEXT("Exposure %+.2f"), D->ExposureBias)); }
	}
}

// ---------------------------------------------------------------- HUD

void ATechnoHUD::DrawHUD()
{
	Super::DrawHUD();
	ATechnoPlayerController* PC = Cast<ATechnoPlayerController>(GetOwningPlayerController());
	if (!PC || !Canvas) return;
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	const float S = FMath::Max(1.f, Canvas->ClipY / 1080.f);

	if (PC->ToastTime > 0.f)
	{
		const float A = FMath::Min(1.f, PC->ToastTime);
		DrawText(PC->Toast, FLinearColor(1.f, 1.f, 1.f, A), Canvas->ClipX * 0.5f - 120.f * S, Canvas->ClipY - 80.f * S, Font, 1.4f * S);
	}
	if (!PC->bShowOverlay) return;

	UTechnoAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTechnoAudioSubsystem>() : nullptr;
	if (!Audio) return;
	const FTechnoAudioFrame F = Audio->GetFrame();

	float X = 24.f * S, Y = 20.f * S;
	const float Line = 18.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 10.f * S, Y - 8.f * S, 470.f * S, 330.f * S);
	DrawText(TEXT("TECHNO VIZ"), FLinearColor(1.f, 0.1f, 0.1f), X, Y, Font, 1.3f * S); Y += Line * 1.4f;

	TArray<FString> Lines;
	Audio->GetStatusText().ParseIntoArrayLines(Lines);
	for (const FString& L : Lines) { DrawText(L, FLinearColor::White, X, Y, Font, S); Y += Line; }

	DrawText(FString::Printf(TEXT("BPM %.1f  %s  conf %.2f   latency %.0f ms  lookahead %.0f ms   sens %.2f"),
		F.Bpm, F.bGridLocked ? TEXT("LOCKED") : TEXT("tracking"), F.BeatConfidence, F.LatencyMs, Audio->GetLookaheadMs(), Audio->GetSensitivity()),
		FLinearColor(0.7f, 1.f, 0.7f), X, Y, Font, S);
	Y += Line * 1.3f;

	// Beat lights: 4 boxes, the current beat lit.
	const int32 BeatInBar = ((F.BeatCount % 4) + 4) % 4;
	for (int32 b = 0; b < 4; ++b)
	{
		const float On = b == BeatInBar ? (1.f - F.BeatPhase) : 0.08f;
		DrawRect(FLinearColor(On, On * 0.15f, On * 0.1f, 1.f), X + b * 28.f * S, Y, 22.f * S, 12.f * S);
	}
	if (F.DropPulse > 0.05f) DrawText(TEXT("DROP"), FLinearColor(1.f, 1.f, 0.f, F.DropPulse), X + 130.f * S, Y - 3.f * S, Font, 1.2f * S);
	if (F.Breakdown > 0.3f) DrawText(TEXT("breakdown"), FLinearColor(0.5f, 0.6f, 1.f), X + 200.f * S, Y - 3.f * S, Font, S);
	Y += Line * 1.2f;

	auto Meter = [&](const TCHAR* Name, float V, const FLinearColor& C)
	{
		DrawText(Name, FLinearColor::White, X, Y, Font, S);
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 1.f), X + 80.f * S, Y + 3.f * S, 300.f * S, 10.f * S);
		DrawRect(C, X + 80.f * S, Y + 3.f * S, 300.f * S * FMath::Clamp(V, 0.f, 1.f), 10.f * S);
		Y += Line;
	};
	Meter(TEXT("Kick"), FMath::Max(F.Kick, F.KickPulse), FLinearColor(1.f, 0.1f, 0.05f));
	Meter(TEXT("Bass"), F.Bass, FLinearColor(1.f, 0.4f, 0.f));
	Meter(TEXT("Mid"), F.Mid, FLinearColor(0.9f, 0.9f, 0.1f));
	Meter(TEXT("Hats"), FMath::Max(F.Hats, F.HatPulse), FLinearColor(0.2f, 0.8f, 1.f));
	Meter(TEXT("Energy"), F.Energy, FLinearColor(0.8f, 0.8f, 0.8f));
	Meter(TEXT("Intensity"), F.Intensity, FLinearColor(1.f, 0.f, 0.6f));

	// Spectrum.
	const float W = 380.f * S / FMath::Max(1, F.Spectrum.Num());
	const float SpecY = Y + 50.f * S;
	for (int32 i = 0; i < F.Spectrum.Num(); ++i)
	{
		const float H = 48.f * S * F.Spectrum[i];
		DrawRect(FLinearColor(0.2f, 0.7f, 1.f), X + i * W, SpecY - H, W * 0.8f, H);
	}
	Y = SpecY + 6.f * S;

	for (TActorIterator<ATechnoStageDirector> It(GetWorld()); It; ++It)
	{
		DrawText(It->GetSceneStatus(), FLinearColor(0.8f, 0.8f, 1.f), X, Y, Font, 0.85f * S);
		Y += Line;
		break;
	}

	const TCHAR* Help =
		TEXT("1 PC output  2 Input  3 Both  4 Demo   [ ] input device   ; ' output device   - = sensitivity   , . lookahead\n")
		TEXT("C next shot  A auto camera  P palette  Up/Down punch  PgUp/PgDn exposure   F11 fullscreen  H overlay  Esc quit");
	TArray<FString> HelpLines;
	FString(Help).ParseIntoArrayLines(HelpLines);
	float HY = Canvas->ClipY - 46.f * S;
	for (const FString& L : HelpLines) { DrawText(L, FLinearColor(1.f, 1.f, 1.f, 0.75f), X, HY, Font, 0.9f * S); HY += Line; }
}

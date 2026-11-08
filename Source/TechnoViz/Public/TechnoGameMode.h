#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "TechnoGameMode.generated.h"

class ATechnoStageDirector;
class UTechnoAudioSubsystem;

/** No pawn, our camera, our controls, our overlay. Spawns a stage director if the level has none. */
UCLASS()
class TECHNOVIZ_API ATechnoGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ATechnoGameMode();
	virtual void StartPlay() override;
};

/** Keyboard controls (works with or without Enhanced Input, no input assets needed). */
UCLASS()
class TECHNOVIZ_API ATechnoPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ATechnoPlayerController();
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	bool bShowOverlay = true;
	FString Toast;
	float ToastTime = 0.f;

private:
	ATechnoStageDirector* FindDirector();
	void Say(const FString& Msg);
	TWeakObjectPtr<ATechnoStageDirector> Director;
};

/** Minimal overlay: source, BPM, meters, spectrum and the key help. Toggle with H. */
UCLASS()
class TECHNOVIZ_API ATechnoHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};

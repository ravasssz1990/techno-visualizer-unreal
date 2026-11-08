#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TechnoAudioTypes.h"
#include "TechnoStageDirector.generated.h"

class UNiagaraSystem;
class UNiagaraComponent;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class USpotLightComponent;
class UPointLightComponent;
class UPostProcessComponent;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class ACameraActor;

/** When a Niagara slot fires. */
UENUM(BlueprintType)
enum class ETechnoFxTrigger : uint8
{
	/** Always running; receives parameters every frame and its sim speed pumps with the kick. */
	Continuous,
	/** A fresh instance spawns on every kick. */
	Kick,
	/** A fresh instance spawns on every hat/clap transient (only when the track is intense). */
	Hat,
	/** A fresh instance spawns on every beat of the tracked grid (keeps going in breakdowns). */
	Beat,
	/** A fresh instance spawns when the kick returns after a breakdown. */
	Drop,
	/** A fresh instance spawns on the first beat of every bar. */
	Bar
};

/**
 * One Niagara effect placed in the scene.
 *
 * Every system receives these User parameters (create the ones you need in the Niagara asset):
 *   float  Kick, Bass, Mid, Hats, Energy, Intensity, KickPulse, HatPulse, DropPulse, Breakdown,
 *          BeatPhase, BarPhase, Bpm, Strength
 *   int    KickCount, BeatCount
 *   color  ColorA, ColorB      (current palette)
 *   float array  Spectrum      (32 bands, Niagara "Array Float" data interface)
 * "Strength" is 0..1 for how hard this particular trigger hit (kick level, drop = 1, ...).
 */
USTRUCT(BlueprintType)
struct FTechnoFxSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	TSoftObjectPtr<UNiagaraSystem> System;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	ETechnoFxTrigger Trigger = ETechnoFxTrigger::Continuous;

	/** Spawn points relative to the director. Continuous: one component per point. Triggered: cycles through them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	TArray<FTransform> Points;

	/** Triggered slots: how many points fire at once (0 = all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	int32 PointsPerTrigger = 0;

	/** Continuous slots: sim speed = 1 + KickPulse * Pump. Makes smoke surge with the kick even without bindings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	float TimeDilationPump = 0.f;

	/** Only trigger when Intensity is at least this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno")
	float MinIntensity = 0.f;

	// Runtime
	UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Components;
	UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Loaded;
	int32 NextPoint = 0;
};

/**
 * Builds the whole "Reactor" stage procedurally and makes every part of it dance:
 * reactor rings, spectrum pillars, LED walls, truss with moving heads, lasers, strobes,
 * volumetric fog, physics debris, Niagara FX slots, post-process punch and the camera.
 * Drop one in an empty level and press Play.
 */
UCLASS()
class TECHNOVIZ_API ATechnoStageDirector : public AActor
{
	GENERATED_BODY()

public:
	ATechnoStageDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "Techno")
	const FTechnoAudioFrame& GetLastFrame() const { return Frame; }

	UFUNCTION(BlueprintCallable, Category = "Techno")
	void NextCameraShot();

	UFUNCTION(BlueprintCallable, Category = "Techno")
	void NextPalette();

	UFUNCTION(BlueprintPure, Category = "Techno")
	FString GetSceneStatus() const;

	ACameraActor* GetCamera() const { return Camera; }

	/** Overall multiplier on every reaction. 1 = default "pumpy", 2 = insane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Reaction", meta = (ClampMin = "0", ClampMax = "3"))
	float Punch = 1.f;

	/** Cut camera shots automatically on phrase boundaries and drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Camera")
	bool bAutoCamera = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Camera")
	int32 BarsPerShot = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Look")
	float ExposureBias = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Look")
	float EmissiveScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Look")
	float SpotIntensity = 400000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Techno|Look")
	float FogDensity = 0.035f;

	UPROPERTY(EditAnywhere, Category = "Techno|FX")
	TArray<FTechnoFxSlot> FxSlots;

	UPROPERTY(EditAnywhere, Category = "Techno|Assets")
	TSoftObjectPtr<UMaterialInterface> EmissiveMaterial;
	UPROPERTY(EditAnywhere, Category = "Techno|Assets")
	TSoftObjectPtr<UMaterialInterface> LaserMaterial;
	UPROPERTY(EditAnywhere, Category = "Techno|Assets")
	TSoftObjectPtr<UMaterialInterface> FloorMaterial;
	UPROPERTY(EditAnywhere, Category = "Techno|Assets")
	TSoftObjectPtr<UMaterialInterface> MetalMaterial;

private:
	struct FPalette { FLinearColor A, B; };

	void BuildStage();
	void BuildReactor();
	void BuildPillars();
	void BuildLedWalls();
	void BuildTruss();
	void BuildLasers();
	void BuildDebris();
	void BuildAtmosphere();
	void BuildCamera();
	void LoadFx();

	void UpdatePalette(float Dt);
	void UpdateReactor(float Dt);
	void UpdatePillars(float Dt);
	void UpdateLedWalls(float Dt);
	void UpdateLights(float Dt);
	void UpdateLasers(float Dt);
	void UpdateDebris(float Dt);
	void UpdateAtmosphere(float Dt);
	void UpdateCamera(float Dt);
	void UpdateFx(float Dt);
	void FireFx(FTechnoFxSlot& Slot, float Strength);
	void PushParams(UNiagaraComponent* C, float Strength) const;

	UMaterialInterface* LoadMat(const TSoftObjectPtr<UMaterialInterface>& Soft) const;
	UMaterialInstanceDynamic* MakeMID(UMaterialInterface* Parent, const FLinearColor& Color, float Intensity);
	UInstancedStaticMeshComponent* MakeISM(UStaticMesh* Mesh, UMaterialInterface* Mat, bool bCollision = false);
	static void SetMIDColor(UMaterialInstanceDynamic* M, const FLinearColor& C, float Intensity);

	UPROPERTY(Transient) TObjectPtr<USceneComponent> Root;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> SphereMesh;

	// Reactor
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> ReactorRings;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> ReactorMIDs;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ReactorCore;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CoreMID;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> CoreLight;
	TArray<float> RingAngles;

	// Pillars
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Pillars;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PillarMID;
	TArray<float> PillarHeights;

	// LED walls
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> LedTiles;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LedMID;
	TArray<FVector2D> LedUV; // per tile: x along wall (0..1 around all walls), y height 0..1
	int32 LedPattern = 0;

	// Truss + lights
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Truss;
	UPROPERTY(Transient) TArray<TObjectPtr<USpotLightComponent>> MovingHeads;
	UPROPERTY(Transient) TArray<TObjectPtr<USpotLightComponent>> Strobes;
	TArray<FVector> HeadBase;
	int32 LightPattern = 0;

	// Lasers
	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Lasers;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> LaserMID;
	int32 LaserPattern = 0;

	// Physics
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Debris;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DebrisMIDs;

	// Atmosphere
	UPROPERTY(Transient) TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> Post;

	// Camera
	UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
	int32 Shot = 0;
	float ShotTime = 0.f;
	float CamAngle = 0.f;
	FVector CamShake = FVector::ZeroVector;
	FVector CamShakeVel = FVector::ZeroVector;

	// Music state
	FTechnoAudioFrame Frame;
	int32 LastKickCount = 0, LastHatCount = 0, LastBeatCount = 0, LastDropCount = 0;
	bool bKick = false, bHat = false, bBeat = false, bBar = false, bDrop = false;
	int32 BarCounter = 0;
	float Time = 0.f;
	float SmoothedIntensity = 0.f;

	TArray<FPalette> Palettes;
	int32 PaletteIndex = 0;
	FLinearColor ColorA = FLinearColor::Red, ColorB = FLinearColor::White;
	float PaletteBlend = 1.f;
	FPalette FromPalette;

	FRandomStream Rng;
};

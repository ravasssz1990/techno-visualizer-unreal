#include "TechnoStageDirector.h"

#include "TechnoAudioSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace
{
	// Stage layout in cm. Everything is centred on the director.
	constexpr int32 NumRings = 5;
	constexpr int32 SegmentsPerRing = 48;
	constexpr int32 NumPillars = 64;
	constexpr float PillarRadius = 1150.f;
	constexpr float TrussHalf = 1500.f;
	constexpr float TrussZ = 1900.f;
	constexpr int32 HeadsPerSide = 6;
	constexpr int32 NumStrobes = 8;
	constexpr int32 LasersPerCorner = 5;
	constexpr int32 NumDebris = 56;
	constexpr float WallHalf = 3400.f;
	constexpr float CoreZ = 520.f;

	const float RingZ[NumRings] = { 140.f, 330.f, 520.f, 710.f, 900.f };
	const float RingR[NumRings] = { 560.f, 480.f, 420.f, 480.f, 560.f };

	FORCEINLINE float Saturate(float X) { return FMath::Clamp(X, 0.f, 1.f); }

	/** Sharp attack shape for "hits": 1 at 0, falling quickly. */
	FORCEINLINE float HitShape(float Phase, float Sharpness)
	{
		return FMath::Exp(-Phase * Sharpness);
	}

	FTransform Xf(const FVector& L, const FRotator& R = FRotator::ZeroRotator, const FVector& S = FVector::OneVector)
	{
		return FTransform(R, L, S);
	}

	/** Transform for a unit cylinder/cube (100 cm along Z) stretched from A to B. */
	FTransform Beam(const FVector& A, const FVector& B, float Thickness)
	{
		const FVector D = B - A;
		const float Len = D.Size();
		const FRotator R = FRotationMatrix::MakeFromZ(D.GetSafeNormal()).Rotator();
		return FTransform(R, (A + B) * 0.5f, FVector(Thickness / 100.f, Thickness / 100.f, Len / 100.f));
	}
}

ATechnoStageDirector::ATechnoStageDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	auto MatPath = [](const TCHAR* Name)
	{
		return TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(FString::Printf(TEXT("/Game/TechnoViz/Materials/%s.%s"), Name, Name)));
	};
	EmissiveMaterial = MatPath(TEXT("M_TV_Emissive"));
	LaserMaterial = MatPath(TEXT("M_TV_Laser"));
	FloorMaterial = MatPath(TEXT("M_TV_Floor"));
	MetalMaterial = MatPath(TEXT("M_TV_Metal"));

	auto Fx = [this](const TCHAR* Name, ETechnoFxTrigger Trigger, TArray<FTransform> Points, int32 PerTrigger = 0, float Pump = 0.f, float MinIntensity = 0.f)
	{
		FTechnoFxSlot S;
		S.System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(FString::Printf(TEXT("/Game/TechnoViz/FX/%s.%s"), Name, Name)));
		S.Trigger = Trigger;
		S.Points = MoveTemp(Points);
		S.PointsPerTrigger = PerTrigger;
		S.TimeDilationPump = Pump;
		S.MinIntensity = MinIntensity;
		FxSlots.Add(S);
	};

	TArray<FTransform> SmokePts, RingTop, Corners, TrussPts;
	for (int32 i = 0; i < 6; ++i)
	{
		const float A = i * PI * 2.f / 6.f;
		SmokePts.Add(Xf(FVector(FMath::Cos(A) * 850.f, FMath::Sin(A) * 850.f, 0.f), FRotator(0.f, FMath::RadiansToDegrees(A), 0.f)));
	}
	for (int32 i = 0; i < 8; ++i)
	{
		const float A = i * PI * 2.f / 8.f;
		RingTop.Add(Xf(FVector(FMath::Cos(A) * RingR[4], FMath::Sin(A) * RingR[4], RingZ[4] + 40.f), FRotator(0.f, FMath::RadiansToDegrees(A), 0.f)));
	}
	for (int32 i = 0; i < 4; ++i)
	{
		const float X = (i & 1) ? TrussHalf : -TrussHalf, Y = (i & 2) ? TrussHalf : -TrussHalf;
		Corners.Add(Xf(FVector(X, Y, 0.f)));
		TrussPts.Add(Xf(FVector(X * 0.5f, Y, TrussZ - 60.f)));
		TrussPts.Add(Xf(FVector(X, Y * 0.5f, TrussZ - 60.f)));
	}

	Fx(TEXT("NS_GroundSmoke"), ETechnoFxTrigger::Continuous, SmokePts, 0, 2.5f);
	Fx(TEXT("NS_BeamDust"), ETechnoFxTrigger::Continuous, { Xf(FVector(0.f, 0.f, 900.f)) }, 0, 0.6f);
	Fx(TEXT("NS_ReactorEmbers"), ETechnoFxTrigger::Continuous, { Xf(FVector(0.f, 0.f, CoreZ)) }, 0, 1.2f);
	Fx(TEXT("NS_KickSparks"), ETechnoFxTrigger::Kick, RingTop, 3);
	Fx(TEXT("NS_FloorShockwave"), ETechnoFxTrigger::Kick, { Xf(FVector(0.f, 0.f, 5.f)) });
	Fx(TEXT("NS_HatGlitter"), ETechnoFxTrigger::Hat, TrussPts, 2, 0.f, 0.6f);
	Fx(TEXT("NS_CO2Jet"), ETechnoFxTrigger::Bar, Corners, 0, 0.f, 0.8f);
	Fx(TEXT("NS_DropBlast"), ETechnoFxTrigger::Drop, { Xf(FVector(0.f, 0.f, CoreZ)) });
	Fx(TEXT("NS_CO2Jet"), ETechnoFxTrigger::Drop, Corners);

	Palettes = {
		{ FLinearColor(1.f, 0.02f, 0.01f), FLinearColor(1.f, 0.9f, 0.85f) },   // blood red / cold white
		{ FLinearColor(0.35f, 0.f, 1.f), FLinearColor(0.f, 0.85f, 1.f) },      // UV / cyan
		{ FLinearColor(0.45f, 1.f, 0.f), FLinearColor(1.f, 1.f, 1.f) },        // acid green / white
		{ FLinearColor(1.f, 0.28f, 0.f), FLinearColor(1.f, 0.f, 0.08f) },      // amber / red
		{ FLinearColor(1.f, 0.f, 0.55f), FLinearColor(0.1f, 0.2f, 1.f) },      // magenta / deep blue
	};
	ColorA = Palettes[0].A;
	ColorB = Palettes[0].B;
	FromPalette = Palettes[0];
	Rng.Initialize(1337);
}

// ---------------------------------------------------------------- helpers

UMaterialInterface* ATechnoStageDirector::LoadMat(const TSoftObjectPtr<UMaterialInterface>& Soft) const
{
	if (UMaterialInterface* M = Soft.LoadSynchronous()) return M;
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInstanceDynamic* ATechnoStageDirector::MakeMID(UMaterialInterface* Parent, const FLinearColor& Color, float Intensity)
{
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this);
	SetMIDColor(M, Color, Intensity);
	return M;
}

void ATechnoStageDirector::SetMIDColor(UMaterialInstanceDynamic* M, const FLinearColor& C, float Intensity)
{
	if (!M) return;
	M->SetVectorParameterValue(TEXT("Color"), C);
	M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
}

UInstancedStaticMeshComponent* ATechnoStageDirector::MakeISM(UStaticMesh* Mesh, UMaterialInterface* Mat, bool bCollision)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetupAttachment(Root);
	C->SetStaticMesh(Mesh);
	if (Mat) C->SetMaterial(0, Mat);
	C->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	C->RegisterComponent();
	C->SetNumCustomDataFloats(1);
	return C;
}

// ---------------------------------------------------------------- build

void ATechnoStageDirector::BeginPlay()
{
	Super::BeginPlay();
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	BuildStage();
	LoadFx();
	BuildCamera();
}

void ATechnoStageDirector::BuildStage()
{
	// Floor: a huge thin cube so physics debris has something solid to land on.
	UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(this);
	Floor->SetupAttachment(Root);
	Floor->SetStaticMesh(CubeMesh);
	Floor->SetMaterial(0, LoadMat(FloorMaterial));
	Floor->SetRelativeLocation(FVector(0.f, 0.f, -50.f));
	Floor->SetRelativeScale3D(FVector(140.f, 140.f, 1.f));
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->RegisterComponent();

	BuildReactor();
	BuildPillars();
	BuildLedWalls();
	BuildTruss();
	BuildLasers();
	BuildDebris();
	BuildAtmosphere();
}

void ATechnoStageDirector::BuildReactor()
{
	UMaterialInterface* Emissive = LoadMat(EmissiveMaterial);
	for (int32 r = 0; r < NumRings; ++r)
	{
		UMaterialInstanceDynamic* MID = MakeMID(Emissive, (r % 2) ? ColorB : ColorA, 20.f);
		UInstancedStaticMeshComponent* Ring = MakeISM(CubeMesh, MID);
		Ring->SetRelativeLocation(FVector(0.f, 0.f, RingZ[r]));
		const float Circ = 2.f * PI * RingR[r];
		const float SegLen = Circ / SegmentsPerRing * 0.78f;
		for (int32 s = 0; s < SegmentsPerRing; ++s)
		{
			const float A = s * 2.f * PI / SegmentsPerRing;
			const FVector P(FMath::Cos(A) * RingR[r], FMath::Sin(A) * RingR[r], 0.f);
			const FRotator Rot(0.f, FMath::RadiansToDegrees(A) + 90.f, 0.f);
			Ring->AddInstance(Xf(P, Rot, FVector(SegLen / 100.f, 0.22f, 0.32f)));
		}
		ReactorRings.Add(Ring);
		ReactorMIDs.Add(MID);
		RingAngles.Add(0.f);
	}

	CoreMID = MakeMID(Emissive, ColorA, 40.f);
	ReactorCore = NewObject<UStaticMeshComponent>(this);
	ReactorCore->SetupAttachment(Root);
	ReactorCore->SetStaticMesh(SphereMesh);
	ReactorCore->SetMaterial(0, CoreMID);
	ReactorCore->SetRelativeLocation(FVector(0.f, 0.f, CoreZ));
	ReactorCore->SetRelativeScale3D(FVector(2.4f));
	ReactorCore->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ReactorCore->SetCastShadow(false);
	ReactorCore->RegisterComponent();

	CoreLight = NewObject<UPointLightComponent>(this);
	CoreLight->SetupAttachment(Root);
	CoreLight->SetRelativeLocation(FVector(0.f, 0.f, CoreZ));
	CoreLight->IntensityUnits = ELightUnits::Candelas;
	CoreLight->SetIntensity(20000.f);
	CoreLight->SetAttenuationRadius(5000.f);
	CoreLight->SetSourceRadius(130.f);
	CoreLight->SetCastShadows(true);
	CoreLight->SetVolumetricScatteringIntensity(3.f);
	CoreLight->RegisterComponent();
}

void ATechnoStageDirector::BuildPillars()
{
	PillarMID = MakeMID(LoadMat(EmissiveMaterial), ColorB, 25.f);
	Pillars = MakeISM(CubeMesh, PillarMID);
	for (int32 i = 0; i < NumPillars; ++i)
	{
		const float A = i * 2.f * PI / NumPillars;
		Pillars->AddInstance(Xf(FVector(FMath::Cos(A) * PillarRadius, FMath::Sin(A) * PillarRadius, 50.f), FRotator(0.f, FMath::RadiansToDegrees(A), 0.f), FVector(0.3f, 0.3f, 1.f)));
		PillarHeights.Add(100.f);
	}
}

void ATechnoStageDirector::BuildLedWalls()
{
	// LED "pixels": 4 walls of 56 x 18 tiles. Brightness per tile goes through per-instance custom data.
	LedMID = MakeMID(LoadMat(EmissiveMaterial), ColorA, 30.f);
	LedTiles = MakeISM(CubeMesh, LedMID);
	constexpr int32 Cols = 56, Rows = 18;
	constexpr float Tile = 110.f;
	for (int32 w = 0; w < 4; ++w)
	{
		const float Yaw = w * 90.f;
		const FRotator WR(0.f, Yaw, 0.f);
		const FVector Normal = WR.RotateVector(FVector(-1.f, 0.f, 0.f));
		const FVector Along = WR.RotateVector(FVector(0.f, 1.f, 0.f));
		const FVector Center = -Normal * WallHalf;
		for (int32 c = 0; c < Cols; ++c)
		{
			for (int32 r = 0; r < Rows; ++r)
			{
				const float X = (c - (Cols - 1) * 0.5f) * Tile;
				const FVector P = Center + Along * X + FVector(0.f, 0.f, 150.f + r * Tile);
				LedTiles->AddInstance(Xf(P, WR, FVector(0.1f, Tile * 0.88f / 100.f, Tile * 0.88f / 100.f)));
				LedUV.Add(FVector2D((w + (float)c / (Cols - 1)) / 4.f, (float)r / (Rows - 1)));
			}
		}
	}
}

void ATechnoStageDirector::BuildTruss()
{
	UMaterialInterface* Metal = LoadMat(MetalMaterial);
	Truss = MakeISM(CubeMesh, Metal);
	Truss->SetCastShadow(true);

	auto AddBeam = [this](const FVector& A, const FVector& B, float T) { Truss->AddInstance(Beam(A, B, T)); };

	// Square box truss: 4 chords per side, zig-zag braces.
	const float H = 45.f;
	FVector C[4] = { { -TrussHalf, -TrussHalf, 0 }, { TrussHalf, -TrussHalf, 0 }, { TrussHalf, TrussHalf, 0 }, { -TrussHalf, TrussHalf, 0 } };
	for (int32 s = 0; s < 4; ++s)
	{
		const FVector A = C[s], B = C[(s + 1) % 4];
		const FVector In = (-(A + B) * 0.5f).GetSafeNormal() * H;
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector Off = FVector(0.f, 0.f, TrussZ + ((k & 1) ? H : 0.f)) + ((k & 2) ? In : FVector::ZeroVector);
			AddBeam(A + Off, B + Off, 6.f);
		}
		const int32 Braces = 30;
		for (int32 i = 0; i < Braces; ++i)
		{
			const FVector P0 = FMath::Lerp(A, B, (float)i / Braces), P1 = FMath::Lerp(A, B, (float)(i + 1) / Braces);
			const FVector Lo(0.f, 0.f, TrussZ), Hi(0.f, 0.f, TrussZ + H);
			AddBeam(P0 + Lo, P1 + Hi, 3.f);
			AddBeam(P0 + Lo + In, P1 + Hi + In, 3.f);
			AddBeam(P0 + Lo, P0 + Lo + In, 3.f);
		}
	}
	// Corner towers.
	for (int32 s = 0; s < 4; ++s)
	{
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector O((k & 1) ? 30.f : -30.f, (k & 2) ? 30.f : -30.f, 0.f);
			AddBeam(C[s] + O, C[s] + O + FVector(0.f, 0.f, TrussZ + H), 7.f);
		}
		for (int32 z = 0; z < 20; ++z)
		{
			const float Z0 = z * (TrussZ / 20.f), Z1 = (z + 1) * (TrussZ / 20.f);
			AddBeam(C[s] + FVector(-30.f, -30.f, Z0), C[s] + FVector(30.f, -30.f, Z1), 3.f);
			AddBeam(C[s] + FVector(30.f, 30.f, Z0), C[s] + FVector(-30.f, 30.f, Z1), 3.f);
		}
	}
	// Reactor cage struts.
	for (int32 i = 0; i < 12; ++i)
	{
		const float A = i * 2.f * PI / 12.f;
		const FVector P(FMath::Cos(A) * 640.f, FMath::Sin(A) * 640.f, 0.f);
		AddBeam(P, P + FVector(0.f, 0.f, 1050.f), 14.f);
		const float A2 = (i + 1) * 2.f * PI / 12.f;
		const FVector P2(FMath::Cos(A2) * 640.f, FMath::Sin(A2) * 640.f, 0.f);
		AddBeam(P + FVector(0.f, 0.f, 1050.f), P2 + FVector(0.f, 0.f, 1050.f), 10.f);
		AddBeam(P + FVector(0.f, 0.f, 20.f), P2 + FVector(0.f, 0.f, 20.f), 10.f);
	}

	// Moving heads hanging under the truss.
	for (int32 s = 0; s < 4; ++s)
	{
		const FVector A = C[s], B = C[(s + 1) % 4];
		for (int32 i = 0; i < HeadsPerSide; ++i)
		{
			const FVector P = FMath::Lerp(A, B, (i + 0.5f) / HeadsPerSide) * 0.97f + FVector(0.f, 0.f, TrussZ - 70.f);
			HeadBase.Add(P);
			Truss->AddInstance(Xf(P + FVector(0.f, 0.f, 35.f), FRotator::ZeroRotator, FVector(0.35f, 0.35f, 0.5f)));

			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(Root);
			L->SetRelativeLocation(P);
			L->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
			L->IntensityUnits = ELightUnits::Candelas;
			L->SetIntensity(SpotIntensity);
			L->SetAttenuationRadius(6000.f);
			L->SetInnerConeAngle(1.5f);
			L->SetOuterConeAngle(4.5f);
			L->SetCastShadows(false);
			L->SetVolumetricScatteringIntensity(6.f);
			L->SetSourceRadius(8.f);
			L->RegisterComponent();
			MovingHeads.Add(L);
		}
	}

	// Strobes: wide, blinding, pointing at the floor.
	for (int32 i = 0; i < NumStrobes; ++i)
	{
		const float A = i * 2.f * PI / NumStrobes + PI / NumStrobes;
		const FVector P(FMath::Cos(A) * TrussHalf * 0.9f, FMath::Sin(A) * TrussHalf * 0.9f, TrussZ - 40.f);
		USpotLightComponent* L = NewObject<USpotLightComponent>(this);
		L->SetupAttachment(Root);
		L->SetRelativeLocation(P);
		L->SetRelativeRotation((FVector(0.f, 0.f, 0.f) - P).Rotation());
		L->IntensityUnits = ELightUnits::Candelas;
		L->SetIntensity(0.f);
		L->SetAttenuationRadius(5000.f);
		L->SetInnerConeAngle(30.f);
		L->SetOuterConeAngle(60.f);
		L->SetCastShadows(false);
		L->SetVolumetricScatteringIntensity(2.f);
		L->SetLightColor(FLinearColor(1.f, 0.97f, 0.95f));
		L->RegisterComponent();
		Strobes.Add(L);
	}
}

void ATechnoStageDirector::BuildLasers()
{
	LaserMID = MakeMID(LoadMat(LaserMaterial), ColorB, 60.f);
	Lasers = MakeISM(CylinderMesh, LaserMID);
	Lasers->SetCastShadow(false);
	for (int32 i = 0; i < 4 * LasersPerCorner; ++i)
	{
		Lasers->AddInstance(Xf(FVector::ZeroVector, FRotator::ZeroRotator, FVector(0.02f, 0.02f, 1.f)));
	}
}

void ATechnoStageDirector::BuildDebris()
{
	UMaterialInterface* Emissive = LoadMat(EmissiveMaterial);
	for (int32 i = 0; i < 4; ++i) DebrisMIDs.Add(MakeMID(Emissive, ColorA, 2.f));

	for (int32 i = 0; i < NumDebris; ++i)
	{
		const float A = Rng.FRandRange(0.f, 2.f * PI);
		const float R = Rng.FRandRange(720.f, 1050.f);
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(CubeMesh);
		C->SetMaterial(0, DebrisMIDs[i % 4]);
		C->SetWorldLocation(GetActorLocation() + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 60.f + Rng.FRandRange(0.f, 200.f)));
		C->SetWorldRotation(FRotator(Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f), 0.f));
		C->SetWorldScale3D(FVector(Rng.FRandRange(0.25f, 0.7f)));
		C->SetCollisionProfileName(TEXT("PhysicsActor"));
		C->SetMobility(EComponentMobility::Movable);
		C->RegisterComponent();
		C->SetSimulatePhysics(true);
		C->SetLinearDamping(0.15f);
		C->SetAngularDamping(0.3f);
		Debris.Add(C);
	}
}

void ATechnoStageDirector::BuildAtmosphere()
{
	Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(FogDensity);
	Fog->SetFogHeightFalloff(0.12f);
	Fog->SetVolumetricFog(true);
	Fog->SetVolumetricFogScatteringDistribution(0.75f);
	Fog->SetVolumetricFogExtinctionScale(1.2f);
	Fog->SetVolumetricFogAlbedo(FColor(240, 240, 255));
	Fog->RegisterComponent();

	Post = NewObject<UPostProcessComponent>(this);
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
	FPostProcessSettings& S = Post->Settings;
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Manual;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;
	S.bOverride_AutoExposureBias = true;
	S.bOverride_BloomIntensity = true;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = -1.f;
	S.bOverride_SceneFringeIntensity = true;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.65f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.12f;
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.6f;
	S.bOverride_ColorSaturation = true;
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.15f, 1.15f, 1.15f, 1.f);
	Post->RegisterComponent();
}

void ATechnoStageDirector::BuildCamera()
{
	FActorSpawnParameters P;
	P.Owner = this;
	Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(FVector(-2200.f, 0.f, 500.f)), P);
	if (Camera)
	{
		Camera->GetCameraComponent()->SetFieldOfView(80.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->SetViewTarget(Camera);
		}
	}
}

void ATechnoStageDirector::LoadFx()
{
	for (FTechnoFxSlot& Slot : FxSlots)
	{
		Slot.Loaded = Slot.System.LoadSynchronous();
		if (!Slot.Loaded)
		{
			UE_LOG(LogTemp, Log, TEXT("TechnoViz: FX %s not found yet, skipping"), *Slot.System.ToString());
			continue;
		}
		if (Slot.Trigger != ETechnoFxTrigger::Continuous) continue;
		for (const FTransform& T : Slot.Points)
		{
			UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAttached(Slot.Loaded, Root, NAME_None, T.GetLocation(), T.Rotator(), T.GetScale3D(), EAttachLocation::KeepRelativeOffset, false, ENCPoolMethod::None, true, true);
			if (C) Slot.Components.Add(C);
		}
	}
}

// ---------------------------------------------------------------- tick

void ATechnoStageDirector::Tick(float Dt)
{
	Super::Tick(Dt);
	Dt = FMath::Min(Dt, 0.05f);
	Time += Dt;

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UTechnoAudioSubsystem* Audio = GI->GetSubsystem<UTechnoAudioSubsystem>())
		{
			Frame = Audio->GetFrame();
		}
	}

	const bool bKickHeard = Frame.KickCount != LastKickCount;
	bHat = Frame.HatCount != LastHatCount;
	bBeat = Frame.BeatCount != LastBeatCount;
	// Once the grid is locked, kicks are predicted (zero latency, lookahead included) instead of heard.
	bKick = Frame.bGridLocked ? bBeat : bKickHeard;
	bDrop = Frame.DropCount != LastDropCount;
	bBar = bBeat && (Frame.BeatCount % 4 == 0);
	LastKickCount = Frame.KickCount;
	LastHatCount = Frame.HatCount;
	LastBeatCount = Frame.BeatCount;
	LastDropCount = Frame.DropCount;
	if (bBar) ++BarCounter;

	SmoothedIntensity = FMath::FInterpTo(SmoothedIntensity, Frame.Intensity, Dt, 2.f);

	if (bDrop || (bBar && BarCounter % 16 == 0)) NextPalette();
	if (bBar && BarCounter % 4 == 0)
	{
		LightPattern = (LightPattern + 1) % 4;
		LaserPattern = (LaserPattern + 1) % 3;
		LedPattern = (LedPattern + 1) % 4;
	}
	if (bAutoCamera && (bDrop || (bBar && BarsPerShot > 0 && BarCounter % BarsPerShot == 0))) NextCameraShot();

	UpdatePalette(Dt);
	UpdateReactor(Dt);
	UpdatePillars(Dt);
	UpdateLedWalls(Dt);
	UpdateLights(Dt);
	UpdateLasers(Dt);
	UpdateDebris(Dt);
	UpdateAtmosphere(Dt);
	UpdateFx(Dt);
	UpdateCamera(Dt);
}

void ATechnoStageDirector::NextPalette()
{
	FromPalette = { ColorA, ColorB };
	PaletteIndex = (PaletteIndex + 1) % Palettes.Num();
	PaletteBlend = 0.f;
}

void ATechnoStageDirector::NextCameraShot()
{
	Shot = (Shot + 1) % 5;
	ShotTime = 0.f;
	CamAngle += Rng.FRandRange(60.f, 160.f);
}

void ATechnoStageDirector::UpdatePalette(float Dt)
{
	const float BeatSec = 60.f / FMath::Max(Frame.Bpm, 60.f);
	PaletteBlend = FMath::Min(1.f, PaletteBlend + Dt / BeatSec);
	const FPalette& To = Palettes[PaletteIndex];
	ColorA = FMath::Lerp(FromPalette.A, To.A, PaletteBlend);
	ColorB = FMath::Lerp(FromPalette.B, To.B, PaletteBlend);
}

void ATechnoStageDirector::UpdateReactor(float Dt)
{
	const float K = Frame.KickPulse * Punch;
	const float Drop = Frame.DropPulse * Punch;
	const float Live = 0.25f + 0.75f * SmoothedIntensity;

	for (int32 r = 0; r < ReactorRings.Num(); ++r)
	{
		// Rings counter-rotate; each kick kicks their spin forward.
		const float Dir = (r % 2) ? -1.f : 1.f;
		const float Speed = (8.f + 50.f * K + 140.f * Drop) * Live * (1.f + 0.3f * r);
		RingAngles[r] += Dir * Speed * Dt;
		UInstancedStaticMeshComponent* Ring = ReactorRings[r];
		const float Lift = (r - 2) * 45.f * K;
		Ring->SetRelativeLocationAndRotation(FVector(0.f, 0.f, RingZ[r] + Lift), FRotator(FMath::Sin(Time * 0.7f + r) * 6.f * Frame.Bass, RingAngles[r], 0.f));
		const float Scale = 1.f + 0.12f * K + 0.25f * Drop;
		Ring->SetRelativeScale3D(FVector(Scale, Scale, 1.f));

		// Chase: a bright head runs around the ring on the beat grid.
		for (int32 s = 0; s < SegmentsPerRing; ++s)
		{
			const float U = (float)s / SegmentsPerRing;
			const float Chase = FMath::Frac(U * 4.f - Frame.BeatPhase - r * 0.25f);
			float B = 0.15f + 0.85f * FMath::Pow(1.f - Chase, 6.f);
			B = FMath::Max(B, K);
			Ring->SetCustomDataValue(s, 0, B, false);
		}
		Ring->MarkRenderStateDirty();
		SetMIDColor(ReactorMIDs[r], (r % 2) ? ColorB : ColorA, EmissiveScale * (6.f + 40.f * Frame.Kick * Punch + 30.f * Drop) * Live);
	}

	const float CoreScale = 2.1f + 0.7f * K + 1.2f * Drop + 0.25f * Frame.Bass;
	ReactorCore->SetRelativeScale3D(FVector(CoreScale));
	SetMIDColor(CoreMID, FMath::Lerp(ColorA, FLinearColor::White, 0.35f * K), EmissiveScale * (10.f + 120.f * K + 200.f * Drop) * (0.4f + 0.6f * Frame.Energy));
	CoreLight->SetLightColor(ColorA);
	CoreLight->SetIntensity((4000.f + 60000.f * K + 150000.f * Drop) * (0.3f + 0.7f * Frame.Energy));
}

void ATechnoStageDirector::UpdatePillars(float Dt)
{
	const int32 Bands = Frame.Spectrum.Num();
	TArray<FTransform> Xfs;
	Xfs.SetNum(NumPillars);
	for (int32 i = 0; i < NumPillars; ++i)
	{
		const int32 Band = Bands > 0 ? FMath::Clamp(i < NumPillars / 2 ? i : NumPillars - 1 - i, 0, Bands - 1) : 0;
		const float V = Bands > 0 ? Frame.Spectrum[Band] : 0.f;
		const float Target = 40.f + V * 900.f * (0.6f + 0.4f * Punch) + Frame.KickPulse * 120.f * Punch;
		// Fast up, slower down: pumps rather than wobbles.
		const float Rate = Target > PillarHeights[i] ? 40.f : 7.f;
		PillarHeights[i] = FMath::FInterpTo(PillarHeights[i], Target, Dt, Rate);
		const float A = i * 2.f * PI / NumPillars;
		const float H = PillarHeights[i];
		Xfs[i] = Xf(FVector(FMath::Cos(A) * PillarRadius, FMath::Sin(A) * PillarRadius, H * 0.5f), FRotator(0.f, FMath::RadiansToDegrees(A), 0.f), FVector(0.3f, 0.3f, H / 100.f));
		Pillars->SetCustomDataValue(i, 0, 0.2f + 0.8f * V, false);
	}
	Pillars->BatchUpdateInstancesTransforms(0, Xfs, false, true, true);
	SetMIDColor(PillarMID, FMath::Lerp(ColorB, ColorA, Frame.KickPulse), EmissiveScale * (8.f + 30.f * Frame.Energy));
}

void ATechnoStageDirector::UpdateLedWalls(float Dt)
{
	const int32 Bands = Frame.Spectrum.Num();
	const float K = Frame.KickPulse * Punch;
	const float Beat = Frame.BeatPhase;
	for (int32 i = 0; i < LedUV.Num(); ++i)
	{
		const float U = LedUV[i].X, V = LedUV[i].Y;
		float B = 0.f;
		switch (LedPattern)
		{
		case 0: // spectrum bars mirrored on every wall
		{
			const float Local = FMath::Frac(U * 4.f);
			const int32 Band = Bands > 0 ? FMath::Clamp((int32)(FMath::Abs(Local * 2.f - 1.f) * (Bands - 1)), 0, Bands - 1) : 0;
			const float Lvl = Bands > 0 ? Frame.Spectrum[Band] : 0.f;
			B = V < Lvl ? 0.6f + 0.4f * V : 0.03f;
			break;
		}
		case 1: // kick rings expanding from wall centres
		{
			const float D = FVector2D(FMath::Frac(U * 4.f) - 0.5f, (V - 0.5f) * 0.35f).Size();
			B = FMath::Pow(Saturate(1.f - FMath::Abs(D - Beat * 0.7f) * 8.f), 2.f) * (0.4f + 0.6f * K);
			break;
		}
		case 2: // horizontal scanners on the 16ths
		{
			const float Sixteenth = FMath::Frac(Beat * 4.f);
			const float Row = FMath::Frac(V - Beat);
			B = HitShape(FMath::Abs(Row - 0.5f) * 8.f, 1.f) * 0.8f + (Sixteenth < 0.2f ? 0.2f * Frame.Hats : 0.f);
			break;
		}
		default: // checker flashing on beats, inverting every beat
		{
			const int32 Cx = (int32)(U * 224.f) / 4, Cy = (int32)(V * 18.f) / 3;
			const bool bOn = ((Cx + Cy + Frame.BeatCount) & 1) == 0;
			B = bOn ? HitShape(Beat, 4.f) : 0.02f;
			break;
		}
		}
		B = Saturate(B * (1.f - 0.6f * Frame.Breakdown) + 0.8f * Frame.DropPulse * HitShape(Beat, 3.f));
		LedTiles->SetCustomDataValue(i, 0, B, false);
	}
	LedTiles->MarkRenderStateDirty();
	SetMIDColor(LedMID, FMath::Lerp(ColorA, ColorB, 0.5f + 0.5f * FMath::Sin(Time * 0.3f)), EmissiveScale * (10.f + 25.f * K));
}

void ATechnoStageDirector::UpdateLights(float Dt)
{
	const float K = Frame.KickPulse * Punch;
	const float Calm = Frame.Breakdown;
	const float Speed = 0.6f + 1.4f * SmoothedIntensity;

	for (int32 i = 0; i < MovingHeads.Num(); ++i)
	{
		USpotLightComponent* L = MovingHeads[i];
		const FVector P = HeadBase[i];
		const float Fi = (float)i;
		FVector Target;
		switch (LightPattern)
		{
		case 0: // sweeping circles on the floor
		{
			const float A = Time * Speed + Fi * 0.5f;
			Target = FVector(FMath::Cos(A) * 900.f, FMath::Sin(A) * 900.f, 0.f);
			break;
		}
		case 1: // fan: converge on the core on the kick, open up between kicks
		{
			const float Open = 1.f - HitShape(Frame.BeatPhase, 5.f);
			Target = FMath::Lerp(FVector(0.f, 0.f, CoreZ), FVector(P.X * -1.3f, P.Y * -1.3f, 0.f), 0.25f + 0.75f * Open);
			break;
		}
		case 2: // crossfire up into the fog
		{
			const float A = Fi * 1.7f + FMath::Sin(Time * Speed * 0.7f) * 1.2f;
			Target = FVector(FMath::Cos(A) * 2500.f, FMath::Sin(A) * 2500.f, 2600.f + 400.f * FMath::Sin(Time + Fi));
			break;
		}
		default: // ballyhoo: new random spot every beat
		{
			FRandomStream R(Frame.BeatCount * 97 + i * 13);
			const FVector Next(R.FRandRange(-2400.f, 2400.f), R.FRandRange(-2400.f, 2400.f), R.FRandRange(0.f, 1500.f));
			FRandomStream R0((Frame.BeatCount - 1) * 97 + i * 13);
			const FVector Prev(R0.FRandRange(-2400.f, 2400.f), R0.FRandRange(-2400.f, 2400.f), R0.FRandRange(0.f, 1500.f));
			Target = FMath::Lerp(Prev, Next, Saturate(Frame.BeatPhase * 3.f));
			break;
		}
		}
		const FVector Dir = (Target - P).GetSafeNormal();
		L->SetRelativeRotation(Dir.Rotation());

		float Level = 0.25f + 0.75f * K;
		if (SmoothedIntensity > 0.7f && (i % 2) == (Frame.HatCount % 2)) Level += 0.6f * Frame.HatPulse;
		Level *= (1.f - 0.75f * Calm);
		Level += 1.5f * Frame.DropPulse;
		L->SetIntensity(SpotIntensity * Level);
		L->SetLightColor((i % 2) ? ColorB : ColorA);
		L->SetOuterConeAngle(4.f + 6.f * Calm + 3.f * Frame.DropPulse);
	}

	// Strobe: 16th-note bursts at the peak, single flashes on hats when it's hot, white-out on drops.
	const float Sixteenth = FMath::Frac(Frame.BeatPhase * 4.f);
	float Strobe = 0.f;
	if (Frame.DropPulse > 0.5f) Strobe = Sixteenth < 0.18f ? 1.f : 0.f;
	else if (SmoothedIntensity > 0.8f) Strobe = Frame.HatPulse > 0.6f ? 0.7f : 0.f;
	for (int32 i = 0; i < Strobes.Num(); ++i)
	{
		Strobes[i]->SetIntensity(Strobe * Punch * 1500000.f);
	}
}

void ATechnoStageDirector::UpdateLasers(float Dt)
{
	const float Show = Saturate((SmoothedIntensity - 0.45f) * 3.f) * (1.f - Frame.Breakdown) + Frame.DropPulse;
	const FVector Corner[4] = { { -TrussHalf, -TrussHalf, TrussZ }, { TrussHalf, -TrussHalf, TrussZ }, { TrussHalf, TrussHalf, TrussZ }, { -TrussHalf, TrussHalf, TrussZ } };
	TArray<FTransform> Xfs;
	Xfs.SetNum(4 * LasersPerCorner);
	for (int32 c = 0; c < 4; ++c)
	{
		const FVector O = Corner[c];
		const float BaseYaw = FMath::Atan2(-O.Y, -O.X);
		for (int32 j = 0; j < LasersPerCorner; ++j)
		{
			const int32 Idx = c * LasersPerCorner + j;
			const float Fj = (j - (LasersPerCorner - 1) * 0.5f);
			FVector Dir;
			switch (LaserPattern)
			{
			case 0: // horizontal fan sweeping across the room
			{
				const float Yaw = BaseYaw + Fj * 0.18f + FMath::Sin(Time * 1.3f + c) * 0.5f;
				Dir = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), -0.25f - 0.1f * FMath::Sin(Time * 2.f + j));
				break;
			}
			case 1: // tunnel: all beams meet at a point circling the core
			{
				const float A = Time * 0.8f;
				const FVector Meet(FMath::Cos(A) * 400.f, FMath::Sin(A) * 400.f, 600.f + 300.f * FMath::Sin(Time * 1.7f));
				Dir = (Meet + FVector(0.f, 0.f, Fj * 60.f) - O).GetSafeNormal();
				break;
			}
			default: // vertical sheets flipping on every beat
			{
				const float Flip = (Frame.BeatCount % 2) ? 1.f : -1.f;
				const float Yaw = BaseYaw + Flip * 0.35f;
				Dir = FVector(FMath::Cos(Yaw), FMath::Sin(Yaw), -0.9f + Fj * 0.2f);
				break;
			}
			}
			Dir.Normalize();
			Xfs[Idx] = Beam(O, O + Dir * 7000.f, 2.2f);
			// Lasers fire on the off-beat and on hats: the classic techno push-pull with the kick.
			const float Off = HitShape(FMath::Abs(Frame.BeatPhase - 0.5f), 10.f);
			const float B = Saturate(Show * (0.35f + 0.65f * FMath::Max(Off, Frame.HatPulse)));
			Lasers->SetCustomDataValue(Idx, 0, B, false);
		}
	}
	Lasers->BatchUpdateInstancesTransforms(0, Xfs, false, true, true);
	SetMIDColor(LaserMID, ColorB, EmissiveScale * 80.f);
}

void ATechnoStageDirector::UpdateDebris(float Dt)
{
	const FVector Center = GetActorLocation();
	if (bKick && Debris.Num() > 0)
	{
		// Each kick launches a handful of blocks off the floor.
		const int32 N = FMath::Clamp(FMath::RoundToInt((4.f + 6.f * Frame.Kick) * Punch), 1, Debris.Num());
		for (int32 n = 0; n < N; ++n)
		{
			UStaticMeshComponent* C = Debris[Rng.RandRange(0, Debris.Num() - 1)];
			const FVector Out = (C->GetComponentLocation() - Center).GetSafeNormal2D();
			C->AddImpulse(FVector(0.f, 0.f, 450.f + 450.f * Frame.Kick) * Punch + Out * Rng.FRandRange(-120.f, 120.f), NAME_None, true);
			C->AddAngularImpulseInDegrees(Rng.VRand() * 400.f * Punch, NAME_None, true);
		}
	}
	if (bDrop)
	{
		for (UStaticMeshComponent* C : Debris)
		{
			const FVector Out = (C->GetComponentLocation() - Center).GetSafeNormal2D();
			C->AddImpulse((Out * 700.f + FVector(0.f, 0.f, 1300.f)) * Punch, NAME_None, true);
			C->AddAngularImpulseInDegrees(Rng.VRand() * 900.f, NAME_None, true);
		}
	}

	for (UStaticMeshComponent* C : Debris)
	{
		const FVector P = C->GetComponentLocation() - Center;
		const float R = P.Size2D();
		if (P.Z < -300.f || R > 3000.f)
		{
			const float A = Rng.FRandRange(0.f, 2.f * PI);
			C->SetWorldLocation(Center + FVector(FMath::Cos(A) * 900.f, FMath::Sin(A) * 900.f, 600.f), false, nullptr, ETeleportType::TeleportPhysics);
			C->SetPhysicsLinearVelocity(FVector::ZeroVector);
		}
		else if (R > 1250.f || R < 680.f)
		{
			// Gently herd them back into the ring between the reactor and the pillars.
			const FVector Toward = -P.GetSafeNormal2D() * (R > 1250.f ? 1.f : -1.f);
			C->AddForce(Toward * 600.f, NAME_None, true);
		}
	}
	for (int32 i = 0; i < DebrisMIDs.Num(); ++i)
	{
		SetMIDColor(DebrisMIDs[i], (i % 2) ? ColorA : ColorB, EmissiveScale * (0.5f + 12.f * Frame.KickPulse * ((i + Frame.KickCount) % 4 == 0 ? 1.f : 0.15f)));
	}
}

void ATechnoStageDirector::UpdateAtmosphere(float Dt)
{
	Fog->SetFogDensity(FogDensity * (1.f + 0.6f * Frame.Bass + 0.8f * Frame.Breakdown));
	FPostProcessSettings& S = Post->Settings;
	const float K = Frame.KickPulse * Punch;
	S.AutoExposureBias = ExposureBias + 0.5f * K + 1.2f * Frame.DropPulse * HitShape(Frame.BeatPhase, 3.f) - 0.6f * Frame.Breakdown;
	S.BloomIntensity = 0.8f + 1.6f * K + 2.f * Frame.DropPulse;
	S.SceneFringeIntensity = 0.3f + 2.5f * K + 4.f * Frame.DropPulse;
	S.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.1f + 0.2f * SmoothedIntensity);
}

void ATechnoStageDirector::PushParams(UNiagaraComponent* C, float Strength) const
{
	if (!C) return;
	C->SetVariableFloat(TEXT("Kick"), Frame.Kick * Punch);
	C->SetVariableFloat(TEXT("Bass"), Frame.Bass * Punch);
	C->SetVariableFloat(TEXT("Mid"), Frame.Mid);
	C->SetVariableFloat(TEXT("Hats"), Frame.Hats);
	C->SetVariableFloat(TEXT("Energy"), Frame.Energy);
	C->SetVariableFloat(TEXT("Intensity"), SmoothedIntensity);
	C->SetVariableFloat(TEXT("KickPulse"), Frame.KickPulse * Punch);
	C->SetVariableFloat(TEXT("HatPulse"), Frame.HatPulse);
	C->SetVariableFloat(TEXT("DropPulse"), Frame.DropPulse);
	C->SetVariableFloat(TEXT("Breakdown"), Frame.Breakdown);
	C->SetVariableFloat(TEXT("BeatPhase"), Frame.BeatPhase);
	C->SetVariableFloat(TEXT("BarPhase"), Frame.BarPhase);
	C->SetVariableFloat(TEXT("Bpm"), Frame.Bpm);
	C->SetVariableFloat(TEXT("Strength"), Strength);
	C->SetVariableInt(TEXT("KickCount"), Frame.KickCount);
	C->SetVariableInt(TEXT("BeatCount"), Frame.BeatCount);
	C->SetVariableLinearColor(TEXT("ColorA"), ColorA);
	C->SetVariableLinearColor(TEXT("ColorB"), ColorB);
	UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(C, TEXT("Spectrum"), Frame.Spectrum);
}

void ATechnoStageDirector::FireFx(FTechnoFxSlot& Slot, float Strength)
{
	if (!Slot.Loaded || Slot.Points.Num() == 0) return;
	const int32 Count = Slot.PointsPerTrigger <= 0 ? Slot.Points.Num() : FMath::Min(Slot.PointsPerTrigger, Slot.Points.Num());
	for (int32 n = 0; n < Count; ++n)
	{
		const FTransform& T = Slot.Points[Slot.NextPoint % Slot.Points.Num()];
		Slot.NextPoint += (Slot.PointsPerTrigger <= 0) ? 1 : 1 + Rng.RandRange(0, 2);
		const FTransform W = T * GetActorTransform();
		UNiagaraComponent* C = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Slot.Loaded, W.GetLocation(), W.Rotator(), W.GetScale3D(), true, false, ENCPoolMethod::AutoRelease, true);
		if (C)
		{
			PushParams(C, Strength);
			C->Activate(true);
		}
	}
}

void ATechnoStageDirector::UpdateFx(float Dt)
{
	for (FTechnoFxSlot& Slot : FxSlots)
	{
		if (!Slot.Loaded) continue;
		if (Slot.Trigger == ETechnoFxTrigger::Continuous)
		{
			for (UNiagaraComponent* C : Slot.Components)
			{
				PushParams(C, 1.f);
				C->SetCustomTimeDilation(1.f + Frame.KickPulse * Slot.TimeDilationPump * Punch);
			}
			continue;
		}
		if (SmoothedIntensity < Slot.MinIntensity && Slot.Trigger != ETechnoFxTrigger::Drop) continue;
		bool bFire = false;
		float Strength = 1.f;
		switch (Slot.Trigger)
		{
		case ETechnoFxTrigger::Kick: bFire = bKick; Strength = Frame.Kick; break;
		case ETechnoFxTrigger::Hat: bFire = bHat; Strength = Frame.Hats; break;
		case ETechnoFxTrigger::Beat: bFire = bBeat; Strength = 0.5f + 0.5f * Frame.Energy; break;
		case ETechnoFxTrigger::Bar: bFire = bBar; Strength = SmoothedIntensity; break;
		case ETechnoFxTrigger::Drop: bFire = bDrop; Strength = 1.f; break;
		default: break;
		}
		if (bFire) FireFx(Slot, Strength * Punch);
	}
}

void ATechnoStageDirector::UpdateCamera(float Dt)
{
	if (!Camera) return;
	ShotTime += Dt;
	const float K = Frame.KickPulse * Punch;
	const float Speed = 0.4f + 1.2f * SmoothedIntensity + 2.f * Frame.DropPulse;

	FVector Pos, Look;
	float Fov = 80.f;
	switch (Shot)
	{
	case 0: // wide orbit
		CamAngle += 6.f * Speed * Dt;
		Pos = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle)) * 2300.f, FMath::Sin(FMath::DegreesToRadians(CamAngle)) * 2300.f, 520.f + 120.f * FMath::Sin(Time * 0.2f));
		Look = FVector(0.f, 0.f, 480.f);
		break;
	case 1: // low and close, looking up at the reactor
		CamAngle += 11.f * Speed * Dt;
		Pos = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle)) * 1450.f, FMath::Sin(FMath::DegreesToRadians(CamAngle)) * 1450.f, 90.f);
		Look = FVector(0.f, 0.f, 800.f);
		Fov = 92.f;
		break;
	case 2: // overhead, spinning
		CamAngle += 14.f * Speed * Dt;
		Pos = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle)) * 450.f, FMath::Sin(FMath::DegreesToRadians(CamAngle)) * 450.f, 2700.f);
		Look = FVector(0.f, 0.f, 0.f);
		Fov = 85.f;
		break;
	case 3: // fast fly-around through the pillars
		CamAngle += 22.f * Speed * Dt;
		Pos = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle)) * (980.f + 150.f * FMath::Sin(Time)), FMath::Sin(FMath::DegreesToRadians(CamAngle)) * (980.f + 150.f * FMath::Sin(Time)), 260.f + 120.f * FMath::Sin(Time * 1.3f));
		Look = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle + 70.f)) * 300.f, FMath::Sin(FMath::DegreesToRadians(CamAngle + 70.f)) * 300.f, 500.f);
		Fov = 100.f;
		break;
	default: // from the back of the room, whole show in frame
		CamAngle += 3.f * Speed * Dt;
		Pos = FVector(FMath::Cos(FMath::DegreesToRadians(CamAngle)) * 3100.f, FMath::Sin(FMath::DegreesToRadians(CamAngle)) * 3100.f, 1200.f);
		Look = FVector(0.f, 0.f, 350.f);
		Fov = 75.f;
		break;
	}

	// Kick shake: a damped spring hit on every kick.
	if (bKick) CamShakeVel += Rng.VRand() * (180.f + 260.f * Frame.Kick) * Punch;
	if (bDrop) CamShakeVel += Rng.VRand() * 1400.f * Punch;
	const float Stiff = 260.f, Damp = 18.f;
	CamShakeVel += (-CamShake * Stiff - CamShakeVel * Damp) * Dt;
	CamShake += CamShakeVel * Dt;

	const FVector WorldPos = GetActorTransform().TransformPosition(Pos) + CamShake;
	const FVector WorldLook = GetActorTransform().TransformPosition(Look);
	FRotator Rot = (WorldLook - WorldPos).Rotation();
	Rot.Roll = FMath::Sin(Frame.BarPhase * 2.f * PI) * 2.5f * SmoothedIntensity + CamShake.X * 0.02f;
	Camera->SetActorLocationAndRotation(WorldPos, Rot);
	Camera->GetCameraComponent()->SetFieldOfView(Fov - 7.f * K - 12.f * Frame.DropPulse * HitShape(Frame.BeatPhase, 2.f));
}

FString ATechnoStageDirector::GetSceneStatus() const
{
	int32 Loaded = 0;
	for (const FTechnoFxSlot& S : FxSlots) if (S.Loaded) ++Loaded;
	static const TCHAR* Shots[] = { TEXT("Wide orbit"), TEXT("Low"), TEXT("Overhead"), TEXT("Fly-through"), TEXT("Back of room") };
	return FString::Printf(TEXT("Shot: %s   Palette %d   Lights %d  Lasers %d  LED %d   Niagara FX loaded: %d/%d   Punch %.1f"),
		Shots[Shot % 5], PaletteIndex, LightPattern, LaserPattern, LedPattern, Loaded, FxSlots.Num(), Punch);
}

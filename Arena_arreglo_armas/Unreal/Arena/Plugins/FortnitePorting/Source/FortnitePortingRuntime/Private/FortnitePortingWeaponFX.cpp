#include "FortnitePortingWeaponFX.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingWeapon.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace
{
	UStaticMesh* BasicMesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	UMaterialInterface* FxMaterial(const TCHAR* Name)
	{
		return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Arena/FX/%s.%s"), Name, Name));
	}

	USoundBase* WeaponSound(const TCHAR* Name)
	{
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Arena/Sounds/Weapons/%s.%s"), Name, Name));
	}

	/** The fire cues of Fortnite are layers played together (close, medium, distant...): one random sound of each layer at the layer's own volume */
	struct FFireLayer
	{
		const TCHAR* Type;
		float Volume;
		TArray<const TCHAR*> Waves;
	};

	const TArray<FFireLayer>& FireLayers()
	{
		static const TArray<FFireLayer> Layers =
		{
			{ TEXT("Shotgun"), 1.00f, { TEXT("Weapon_Shotgun_MidRange_Fire01"), TEXT("Weapon_Shotgun_MidRange_Fire02"), TEXT("Weapon_Shotgun_MidRange_Fire03") } },
			{ TEXT("Shotgun"), 1.92f, { TEXT("Weapon_Shotgun_MidRange_MediumNew_Fire01"), TEXT("Weapon_Shotgun_MidRange_MediumNew_Fire02") } },
			{ TEXT("Shotgun"), 2.00f, { TEXT("Weapon_Shotgun_MidRange_DistantNew_Fire01"), TEXT("Weapon_Shotgun_MidRange_DistantNew_Fire02") } },
			{ TEXT("Rifle"), 1.05f, { TEXT("AssaultHeavy_Fire_Close_01"), TEXT("AssaultHeavy_Fire_Close_02"), TEXT("AssaultHeavy_Fire_Close_03"), TEXT("AssaultHeavy_Fire_Close_04") } },
			{ TEXT("Rifle"), 1.95f, { TEXT("AssaultHeavy_Fire_MediumNew_01"), TEXT("AssaultHeavy_Fire_MediumNew_03"), TEXT("AssaultHeavy_Fire_MediumNew_02") } },
			{ TEXT("Rifle"), 0.75f, { TEXT("AssaultHeavy_Fire_DistantNew_01"), TEXT("AssaultHeavy_Fire_DistantNew_02") } },
			{ TEXT("Sniper"), 0.93f, { TEXT("MilitarySniper_Fire_Close_01"), TEXT("MilitarySniper_Fire_Close_02"), TEXT("MilitarySniper_Fire_Close_03") } },
			{ TEXT("Sniper"), 1.02f, { TEXT("MilitarySniper_Fire_CloseLayer_01"), TEXT("MilitarySniper_Fire_CloseLayer_02"), TEXT("MilitarySniper_Fire_CloseLayer_03") } },
			{ TEXT("Sniper"), 0.93f, { TEXT("MilitarySniper_Casing_01"), TEXT("MilitarySniper_Casing_02"), TEXT("MilitarySniper_Casing_03") } },
			{ TEXT("SMG"), 0.75f, { TEXT("Scrap_SMG_Fire_Close_02"), TEXT("Scrap_SMG_Fire_Close_03"), TEXT("Scrap_SMG_Fire_Close_04"), TEXT("Scrap_SMG_Fire_Close_05") } },
			{ TEXT("SMG"), 2.00f, { TEXT("Scrap_SMG_Fire_Medium_01"), TEXT("Scrap_SMG_Fire_Medium_02"), TEXT("Scrap_SMG_Fire_Medium_03"), TEXT("Scrap_SMG_Fire_Medium_04") } },
			{ TEXT("SMG"), 1.25f, { TEXT("Scrap_SMG_Fire_Distant_01"), TEXT("Scrap_SMG_Fire_Distant_02"), TEXT("Scrap_SMG_Fire_Distant_03"), TEXT("Scrap_SMG_Fire_Distant_04") } },
			{ TEXT("Pistol"), 1.00f, { TEXT("Pistol_Muster_Athena_Fire_Close_01"), TEXT("Pistol_Muster_Athena_Fire_Close_02"), TEXT("Pistol_Muster_Athena_Fire_Close_03"), TEXT("Pistol_Muster_Athena_Fire_Close_06") } },
			{ TEXT("Pistol"), 1.00f, { TEXT("Pistol_Muster_Athena_Fire_Med_01"), TEXT("Pistol_Muster_Athena_Fire_Med_02"), TEXT("Pistol_Muster_Athena_Fire_Med_03"), TEXT("Pistol_Muster_Athena_Fire_Med_04"), TEXT("Pistol_Muster_Athena_Fire_Med_05"), TEXT("Pistol_Muster_Athena_Fire_Med_06") } },
			{ TEXT("Pistol"), 1.00f, { TEXT("Pistol_Muster_Athena_Fire_Dist_01"), TEXT("Pistol_Muster_Athena_Fire_Dist_02"), TEXT("Pistol_Muster_Athena_Fire_Dist_03"), TEXT("Pistol_Muster_Athena_Fire_Dist_04"), TEXT("Pistol_Muster_Athena_Fire_Dist_05"), TEXT("Pistol_Muster_Athena_Fire_Dist_06") } },
		};
		return Layers;
	}

	FVector CameraLocation(const UWorld* World)
	{
		if (const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
		{
			if (PC->PlayerCameraManager)
			{
				return PC->PlayerCameraManager->GetCameraLocation();
			}
		}
		return FVector::ZeroVector;
	}

	float DistanceVolumeMultiplier(const UWorld* World, const FVector& Location)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC == nullptr || PC->PlayerCameraManager == nullptr)
		{
			return 1.0f;
		}

		constexpr float FullVolumeDistance = 800.0f;
		constexpr float SilentDistance = 7000.0f;
		const float Distance = FVector::Distance(CameraLocation(World), Location);
		const float RemainingVolume = 1.0f - FMath::Clamp(
			(Distance - FullVolumeDistance) / (SilentDistance - FullVolumeDistance), 0.0f, 1.0f);
		return FMath::Square(RemainingVolume);
	}

	/** Plays the layers of a weapon type; false when none of its waves was imported (the single cue is used then) */
	bool PlayFireLayers(UWorld* World, FName Type, const FVector& Location)
	{
		bool bPlayed = false;
		for (const FFireLayer& Layer : FireLayers())
		{
			if (Type != Layer.Type || Layer.Waves.IsEmpty())
			{
				continue;
			}
			const TCHAR* Wave = Layer.Waves[FMath::RandRange(0, Layer.Waves.Num() - 1)];
			if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Arena/Sounds/Weapons/Fire/%s.%s"), Wave, Wave)))
			{
				UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, Layer.Volume * 0.45f * DistanceVolumeMultiplier(World, Location));
				bPlayed = true;
			}
		}
		return bPlayed;
	}

	/** A glowing card that lives for a moment (a flash or a spark) */
	UStaticMeshComponent* MakeCard(AActor* Owner, USceneComponent* Parent, UStaticMesh* Plane, UMaterialInterface* Material, const FTransform& World, const FVector& Scale)
	{
		UStaticMeshComponent* Card = NewObject<UStaticMeshComponent>(Owner);
		Card->SetStaticMesh(Plane);
		Card->SetMobility(EComponentMobility::Movable);
		Card->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Card->SetCastShadow(false);
		Card->SetupAttachment(Parent);
		if (Material)
		{
			Card->SetMaterial(0, Material);
		}
		Card->RegisterComponent();
		Card->SetWorldTransform(FTransform(World.GetRotation(), World.GetLocation(), Scale));
		return Card;
	}
}

// ------------------------------------------------------------------------------------------------ the tracer

AFortnitePortingTracer::AFortnitePortingTracer()
{
	PrimaryActorTick.bCanEverTick = true;
	SetReplicates(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Streak = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Streak"));
	Streak->SetupAttachment(Root);
	Streak->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Streak->SetCastShadow(false);
}

void AFortnitePortingTracer::Launch(const FVector& InStart, const FVector& InEnd, float Speed, float Length)
{
	Start = InStart;
	const FVector Delta = InEnd - InStart;
	TotalDistance = Delta.Size();
	Direction = Delta.GetSafeNormal();
	FlySpeed = Speed;
	StreakLength = FMath::Min(Length, FMath::Max(TotalDistance, 1.0f));
	if (UStaticMesh* Cube = BasicMesh(TEXT("Cube")))
	{
		Streak->SetStaticMesh(Cube);
	}
	if (UMaterialInterface* Material = FxMaterial(TEXT("M_Tracer")))
	{
		Streak->SetMaterial(0, Material);
	}
	// a thin bar along the flight line, its tail at the position
	Streak->SetRelativeScale3D(FVector(StreakLength / 100.0f, 0.012f, 0.012f));
	SetActorLocationAndRotation(Start, Direction.Rotation());
	Streak->SetRelativeLocation(FVector(StreakLength * 0.5f, 0.0f, 0.0f));
}

void AFortnitePortingTracer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Travelled += FlySpeed * DeltaSeconds;
	if (Travelled >= TotalDistance)
	{
		Destroy();
		return;
	}
	SetActorLocation(Start + Direction * Travelled);
}

// ------------------------------------------------------------------------------------------------ one shot

namespace FortnitePortingWeaponFX
{
	USoundBase* DefaultFireSound(FName WeaponType)
	{
		const TCHAR* Name = TEXT("Weapon_Shotgun_MidRange_Fire_Athena_Cue");
		if (WeaponType == TEXT("Rifle"))
		{
			Name = TEXT("AssaultHeavy_Fire_Cue");
		}
		else if (WeaponType == TEXT("Sniper"))
		{
			Name = TEXT("MilitarySniper_Fire_Cue_1P_Athena");
		}
		else if (WeaponType == TEXT("SMG"))
		{
			Name = TEXT("Scrap_SMG_Fire_Cue");
		}
		else if (WeaponType == TEXT("Pistol"))
		{
			Name = TEXT("Pistol_Muster_Athena_Fire_Cue");
		}
		return WeaponSound(Name);
	}

	void PlayShot(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data, const FVector& End, bool bHit)
	{
		UWorld* World = Weapon ? Weapon->GetWorld() : nullptr;
		if (!World || !Data || Data->Range < 1000.0f)
		{
			return;     // melee and the like have no muzzle
		}
		const FVector Muzzle = Weapon->GetMuzzleLocation();
		const FVector Forward = Weapon->GetMuzzleDirection();

		// sound at the barrel
		if (Data->FireSound)
		{
			UGameplayStatics::PlaySoundAtLocation(World, Data->FireSound.Get(), Muzzle, DistanceVolumeMultiplier(World, Muzzle));
		}
		else if (!PlayFireLayers(World, Data->WeaponType, Muzzle))
		{
			if (USoundBase* Single = DefaultFireSound(Data->WeaponType))
			{
				UGameplayStatics::PlaySoundAtLocation(World, Single, Muzzle, DistanceVolumeMultiplier(World, Muzzle));
			}
		}

		// flash: two cards that contain the barrel axis and a star that always faces the camera, plus the light of the burst
		UStaticMesh* Plane = BasicMesh(TEXT("Plane"));
		UMaterialInterface* Flash = FxMaterial(TEXT("M_MuzzleFlash"));
		TArray<UStaticMeshComponent*> Cards;
		USceneComponent* Parent = Weapon->GetRootComponent();
		const float Size = Data->WeaponType == TEXT("Pistol") ? 0.55f : 0.9f;
		if (Plane && Flash)
		{
			const FRotationMatrix Frame(Forward.Rotation());
			for (int32 Roll = 0; Roll < 2; ++Roll)
			{
				// the plane's X axis runs along the barrel, its normal is turned a quarter between the two cards
				const FQuat Turn(Forward, FMath::DegreesToRadians(90.0f * Roll));
				FQuat Orientation = Turn * FQuat(FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat());
				Cards.Add(MakeCard(Weapon, Parent, Plane, Flash, FTransform(Orientation, Muzzle + Forward * 22.0f * Size), FVector(0.6f * Size, 0.28f * Size, 1.0f)));
			}
			const FVector ToCamera = (CameraLocation(World) - Muzzle).GetSafeNormal();
			const FQuat Facing = FRotationMatrix::MakeFromZ(ToCamera.IsNearlyZero() ? FVector::UpVector : ToCamera).ToQuat();
			UStaticMeshComponent* Star = MakeCard(Weapon, Parent, Plane, Flash, FTransform(Facing, Muzzle), FVector(0.38f * Size, 0.38f * Size, 1.0f));
			Star->AddLocalRotation(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f));
			Cards.Add(Star);
		}
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Weapon);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Parent);
		Light->SetIntensity(7000.0f);
		Light->SetAttenuationRadius(520.0f);
		Light->SetLightColor(FLinearColor(1.0f, 0.62f, 0.22f));
		Light->SetCastShadows(false);
		Light->RegisterComponent();
		Light->SetWorldLocation(Muzzle + Forward * 12.0f);

		TArray<TWeakObjectPtr<UActorComponent>> ToRemove;
		for (UStaticMeshComponent* Card : Cards)
		{
			ToRemove.Add(Card);
		}
		ToRemove.Add(Light);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Weapon, [ToRemove]()
		{
			for (const TWeakObjectPtr<UActorComponent>& Component : ToRemove)
			{
				if (Component.IsValid())
				{
					Component->DestroyComponent();
				}
			}
		}), 0.065f, false);

		// tracer from the barrel to where the shot lands
		if (FVector::DistSquared(Muzzle, End) > FMath::Square(300.0f))
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			if (AFortnitePortingTracer* Tracer = World->SpawnActor<AFortnitePortingTracer>(AFortnitePortingTracer::StaticClass(), FTransform(Muzzle), Params))
			{
				Tracer->Launch(Muzzle, End, 32000.0f, 260.0f);
				Tracer->SetLifeSpan(2.0f);
			}
		}

		// a spark where it hit
		if (bHit && Plane && Flash)
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			if (AActor* Spark = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(End), Params))
			{
				USceneComponent* Root = NewObject<USceneComponent>(Spark, TEXT("Root"));
				Spark->SetRootComponent(Root);
				Root->RegisterComponent();
				const FVector ToCamera = (CameraLocation(World) - End).GetSafeNormal();
				MakeCard(Spark, Root, Plane, Flash, FTransform(FRotationMatrix::MakeFromZ(ToCamera.IsNearlyZero() ? FVector::UpVector : ToCamera).ToQuat(), End), FVector(0.16f, 0.16f, 1.0f));
				Spark->SetLifeSpan(0.07f);
			}
		}
	}

	void PlayEmpty(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data)
	{
		if (UWorld* World = Weapon ? Weapon->GetWorld() : nullptr)
		{
			USoundBase* Sound = Data && Data->EmptySound ? Data->EmptySound.Get() : WeaponSound(TEXT("Shotgun_Empty_Cue"));
			if (Sound)
			{
				UGameplayStatics::PlaySoundAtLocation(World, Sound, Weapon->GetActorLocation());
			}
		}
	}

	void PlayReload(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data)
	{
		UWorld* World = Weapon ? Weapon->GetWorld() : nullptr;
		if (!World || !Data)
		{
			return;
		}
		USoundBase* Sound = Data->ReloadSound ? Data->ReloadSound.Get() : nullptr;
		if (!Sound)
		{
			if (Data->WeaponType == TEXT("Shotgun"))
			{
				Sound = WeaponSound(TEXT("Weapon_Shotgun_Cock2_Cue"));
			}
			else if (Data->WeaponType == TEXT("Sniper"))
			{
				Sound = WeaponSound(TEXT("MilitarySniper_ReloadStart_Cue"));
			}
		}
		if (Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(World, Sound, Weapon->GetActorLocation());
		}
	}
}

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FortnitePortingCosmeticData.generated.h"

class AActor;
class APawn;
class UAnimMontage;
class UAnimSequence;
class USoundBase;
class USkeletalMesh;
class UStreamableRenderAsset;
class UTexture2D;

/**
 * Skeleton profiles follow Fortnite's body layout: "<Gender>_<Size>", e.g. "Female_Medium" or "Male_Large".
 * Every character of the same profile shares one skeleton, so cosmetics store one montage per profile.
 */
namespace FortnitePortingProfiles
{
	inline const FName DefaultProfile(TEXT("Male_Medium"));

	FORTNITEPORTINGRUNTIME_API FName GetMediumProfile(FName Profile);
}

UCLASS(Abstract, BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingCosmeticData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fortnite Porting")
	FText DisplayName;

	/** Locker/item shop icon, imported into <Asset>/Icon */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fortnite Porting")
	TObjectPtr<UTexture2D> Icon;

protected:
	/** Exact profile first, then the same gender at medium size, then Male_Medium, then anything. */
	static UAnimMontage* FindForProfile(const TMap<FName, TObjectPtr<UAnimMontage>>& Montages, FName Profile);
};

/** Music or voice line of an emote, started Time seconds into the montage */
USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingEmoteSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emote")
	TObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emote")
	float Time = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emote")
	bool bLoop = false;
};

UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingEmoteData : public UFortnitePortingCosmeticData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emote")
	TMap<FName, TObjectPtr<UAnimMontage>> MontagesByProfile;

	/** Played with the montage and stopped with it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Emote")
	TArray<FFortnitePortingEmoteSound> Sounds;

	UFUNCTION(BlueprintPure, Category = "Emote")
	UAnimMontage* GetMontage(FName Profile) const { return FindForProfile(MontagesByProfile, Profile); }

	// ── Music loop (filled automatically when the emote is imported, or on first play for older imports) ──

	/** True once AnalyzeMusicLoop ran */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emote|Loop")
	bool bLoopAnalyzed = false;

	/** The emote has music, so the dance repeats until the player moves and the music loops with it */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emote|Loop")
	bool bLoopsWithMusic = false;

	/** Montage section that repeats */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emote|Loop")
	FName LoopSection;

	/** Index in Sounds of the track that sets the pace (the longest one); it loops by itself */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emote|Loop")
	int32 MasterSound = INDEX_NONE;

	/**
	 * Dance speed that makes k laps of the repeated section last exactly m loops of the master track, so music and
	 * dance start over together. Stays within a few percent of normal speed; 1 when they cannot be matched.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Emote|Loop")
	float DancePlayRate = 1.0f;

	/**
	 * Works out the repeated section, the master track and the dance speed from any one of the montages (the dance is
	 * the same length for every body type). Makes no change to the montages or the sound waves.
	 */
	void AnalyzeMusicLoop(const UAnimMontage* Montage);
};

USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingAttachedMesh
{
	GENERATED_BODY()

	/** Static or skeletal mesh */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStreamableRenderAsset> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FName Socket = TEXT("weapon_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh")
	FTransform Offset;
};

UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingPickaxeData : public UFortnitePortingCosmeticData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickaxe")
	TArray<FFortnitePortingAttachedMesh> Meshes;

	/** Fortnite grip: "TwoHanded" (default pickaxe), "OneHanded" or "DualWield" (one item per hand) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickaxe")
	FName Style = TEXT("TwoHanded");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickaxe")
	TMap<FName, TObjectPtr<UAnimMontage>> EquipMontages;

	/** Upper body pose while holding a one handed / dual pickaxe (two handed ones use the character's pickaxe pose) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickaxe")
	TMap<FName, TObjectPtr<UAnimMontage>> HoldMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickaxe")
	TMap<FName, TObjectPtr<UAnimMontage>> SwingMontages;

	UFUNCTION(BlueprintPure, Category = "Pickaxe")
	UAnimMontage* GetEquipMontage(FName Profile) const { return FindForProfile(EquipMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Pickaxe")
	UAnimMontage* GetSwingMontage(FName Profile) const { return FindForProfile(SwingMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Pickaxe")
	UAnimMontage* GetHoldMontage(FName Profile) const { return FindForProfile(HoldMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Pickaxe")
	bool IsTwoHanded() const { return Style.IsNone() || Style == TEXT("TwoHanded"); }
};

UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingGliderData : public UFortnitePortingCosmeticData
{
	GENERATED_BODY()

public:
	UFortnitePortingGliderData();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glider")
	FFortnitePortingAttachedMesh Glider;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glider")
	TMap<FName, TObjectPtr<UAnimMontage>> GlideMontages;

	/** Maximum downward speed while gliding (cm/s) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glider")
	float FallSpeed = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glider")
	float AirControl = 0.8f;

	UFUNCTION(BlueprintPure, Category = "Glider")
	UAnimMontage* GetGlideMontage(FName Profile) const { return FindForProfile(GlideMontages, Profile); }
};

/**
 * A weapon (or any held item) from Items. Player montages play on the "UpperBody" slot,
 * so they combine with whatever the legs are doing.
 */
UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingWeaponData : public UFortnitePortingCosmeticData
{
	GENERATED_BODY()

public:
	/** "Rifle", "Shotgun", "SMG", "Pistol", "Sniper", "Launcher", "Bow", "Melee" or "Consumable" */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName WeaponType = TEXT("Rifle");

	/** First entry is the main weapon mesh (weapon_r), extra entries are offhand meshes */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TArray<FFortnitePortingAttachedMesh> Meshes;

	/** Generated BP_<Weapon>, spawned in the hand and usable as a world pickup */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AActor> WeaponActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> HoldMontages;

	/** The weapon's own jog, looped on the upper body while the character runs with it up */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> JogMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> FireMontages;

	/** The aim down the sights pose (an additive layer held while the aim button is down) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> AimMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> ReloadMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> EquipMontages;

	/** Animations of the weapon mesh itself (slide, magazine...) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Weapon Animations")
	TObjectPtr<UAnimSequence> WeaponIdleAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Weapon Animations")
	TObjectPtr<UAnimSequence> WeaponFireAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Weapon Animations")
	TObjectPtr<UAnimSequence> WeaponReloadAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Weapon Animations")
	TObjectPtr<UAnimSequence> WeaponEquipAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	bool bAutomatic = true;

	/** Seconds between shots */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	float FireInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	float Damage = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	float Range = 15000.0f;

	/** 0 = no ammo / reload (melee, consumables) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	int32 MagazineSize = 30;

	/** Used when there is no reload montage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Stats")
	float ReloadTime = 2.0f;

	/** Where the bullet leaves the gun and which way it goes, in the weapon mesh's own space (found from the mesh geometry: FP.FitMuzzles) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Muzzle")
	FVector MuzzleOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Muzzle")
	FVector MuzzleDirection = FVector(0.0, 1.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Muzzle")
	bool bMuzzleFitted = false;

	/** Sounds of the shot, the empty click and the reload (a default per weapon type is used when empty) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sounds")
	TObjectPtr<USoundBase> FireSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sounds")
	TObjectPtr<USoundBase> EmptySound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Sounds")
	TObjectPtr<USoundBase> ReloadSound;

	/** Fills the stats above with sensible values for WeaponType */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ApplyTypeDefaults();

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetHoldMontage(FName Profile) const { return FindForProfile(HoldMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetFireMontage(FName Profile) const { return FindForProfile(FireMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetAimMontage(FName Profile) const { return FindForProfile(AimMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetJogMontage(FName Profile) const { return FindForProfile(JogMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetReloadMontage(FName Profile) const { return FindForProfile(ReloadMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	UAnimMontage* GetEquipMontage(FName Profile) const { return FindForProfile(EquipMontages, Profile); }
};

/** A drivable vehicle from Vehicles, plus the driver/passenger animations per skeleton profile */
UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingVehicleData : public UFortnitePortingCosmeticData
{
	GENERATED_BODY()

public:
	/** Generated BP_<Vehicle>; drag it into the level and press E next to it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TSubclassOf<APawn> VehicleClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> PassengerMontages;

	/** Passenger holding the driver's waist while the vehicle accelerates; the plain passenger pose is used otherwise */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> PassengerDriveMontages;

	/** Driver pose while the vehicle is moving straight */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverDriveMontages;

	/** Driver poses while steering, reversing and braking; the plain driver loop is used when one is missing */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverLeftMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverRightMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverReverseMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> DriverBrakeMontages;

	/** 2 or 4: only two- and four-wheelers are supported */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	int32 WheelCount = 4;

	/** Degrees the vehicle leans into a turn at full steer (motorcycles) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float MaxLeanAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> EnterMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Player Animations")
	TMap<FName, TObjectPtr<UAnimMontage>> ExitMontages;

	/** Looping animation of the vehicle mesh itself */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UAnimSequence> VehicleIdleAnimation;

	/** Seat sockets on the vehicle mesh, the first one is the driver seat */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Seats")
	TArray<FName> SeatSockets;

	/** Driver position relative to the seat socket (or to the vehicle mesh when there is no socket) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Seats")
	FTransform DriverOffset;

	/** Use 90/-90/180 if the vehicle drives sideways or backwards */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float MeshYawOffset = 0.0f;

	/** cm/s (2500 = 90 km/h) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float MaxSpeed = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float Acceleration = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float BrakeDeceleration = 2800.0f;

	/** Degrees per second at full steering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vehicle|Driving")
	float TurnRate = 75.0f;

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UAnimMontage* GetDriverMontage(FName Profile) const { return FindForProfile(DriverMontages, Profile); }

	/** Montage for the driving state; Steer -1..1 (left/right), falls back to the plain driver loop */
	UAnimMontage* GetDrivingMontage(FName Profile, float Steer, float Throttle, bool bBraking, bool bMoving = false) const
	{
		UAnimMontage* Montage = nullptr;
		if (bBraking) Montage = FindForProfile(DriverBrakeMontages, Profile);
		else if (Throttle < -0.1f) Montage = FindForProfile(DriverReverseMontages, Profile);
		if (Montage == nullptr && Steer < -0.3f) Montage = FindForProfile(DriverLeftMontages, Profile);
		if (Montage == nullptr && Steer > 0.3f) Montage = FindForProfile(DriverRightMontages, Profile);
		if (Montage == nullptr && bMoving) Montage = FindForProfile(DriverDriveMontages, Profile);
		return Montage ? Montage : FindForProfile(DriverMontages, Profile);
	}

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UAnimMontage* GetPassengerMontage(FName Profile) const { return FindForProfile(PassengerMontages, Profile); }

	UAnimMontage* GetPassengerDriveMontage(FName Profile, bool bAccelerating) const
	{
		UAnimMontage* Montage = bAccelerating ? FindForProfile(PassengerDriveMontages, Profile) : nullptr;
		return Montage ? Montage : FindForProfile(PassengerMontages, Profile);
	}

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UAnimMontage* GetEnterMontage(FName Profile) const { return FindForProfile(EnterMontages, Profile); }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	UAnimMontage* GetExitMontage(FName Profile) const { return FindForProfile(ExitMontages, Profile); }
};

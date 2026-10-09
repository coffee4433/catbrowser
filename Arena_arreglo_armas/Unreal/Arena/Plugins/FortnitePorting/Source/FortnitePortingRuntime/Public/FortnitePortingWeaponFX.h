// Gun effects played on every machine when a weapon fires: the muzzle flash and light at the barrel tip, the tracer from the barrel to where the
// shot lands, the fire sound and a small spark where it hits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FortnitePortingWeaponFX.generated.h"

class AFortnitePortingWeapon;
class UFortnitePortingWeaponData;
class UStaticMeshComponent;
class USoundBase;

/** A streak of light that flies from the muzzle to the impact and disappears */
UCLASS(NotPlaceable, Transient)
class FORTNITEPORTINGRUNTIME_API AFortnitePortingTracer : public AActor
{
	GENERATED_BODY()

public:
	AFortnitePortingTracer();

	void Launch(const FVector& InStart, const FVector& InEnd, float Speed, float Length);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Streak;

	FVector Start = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float TotalDistance = 0.0f;
	float Travelled = 0.0f;
	float StreakLength = 250.0f;
	float FlySpeed = 25000.0f;
};

namespace FortnitePortingWeaponFX
{
	/** Everything a shot shows: flash, sound, tracer and impact. End is where the shot lands (or the end of its range) */
	FORTNITEPORTINGRUNTIME_API void PlayShot(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data, const FVector& End, bool bHit);

	FORTNITEPORTINGRUNTIME_API void PlayEmpty(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data);
	FORTNITEPORTINGRUNTIME_API void PlayReload(AFortnitePortingWeapon* Weapon, const UFortnitePortingWeaponData* Data);

	/** The sound of a weapon type when its data asset does not name one ("Rifle", "Shotgun", "Sniper"...) */
	FORTNITEPORTINGRUNTIME_API USoundBase* DefaultFireSound(FName WeaponType);
}

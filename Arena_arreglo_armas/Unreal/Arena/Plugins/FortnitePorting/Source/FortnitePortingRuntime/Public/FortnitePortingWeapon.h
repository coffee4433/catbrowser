#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FortnitePortingWeapon.generated.h"

class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class USphereComponent;
class UStaticMeshComponent;
class UFortnitePortingWeaponData;

/**
 * Parent of the generated BP_<Weapon>. Placed in a level it is a pickup (walk over it);
 * the character component also spawns it in the hand.
 */
UCLASS(Blueprintable)
class FORTNITEPORTINGRUNTIME_API AFortnitePortingWeapon : public AActor
{
	GENERATED_BODY()

public:
	AFortnitePortingWeapon();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TObjectPtr<UFortnitePortingWeaponData> WeaponData;

	/** Picked up automatically when a Fortnite Porting character touches it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	bool bIsPickup = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> WeaponStaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USphereComponent> PickupSphere;

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void ApplyWeaponData();

	/** Switches between world pickup (spinning, overlaps) and held in a hand */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void SetHeld(bool bHeld);

	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void PlayWeaponAnimation(UAnimSequence* Animation, bool bLoop = false, float PlayRate = 1.0f);

	/** Muzzle socket when the mesh has one, otherwise the front of the mesh bounds */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleLocation() const;

	/** The way the bullet leaves the barrel, in the world */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FVector GetMuzzleDirection() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

private:
	bool bHeld = false;
};

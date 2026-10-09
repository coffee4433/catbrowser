#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FortnitePortingAnimInstance.generated.h"

class ACharacter;
class UFortnitePortingCharacterComponent;

/**
 * Parent class of the generated character Anim Blueprints.
 * Exposes simple booleans so the generated state machine transitions need no math nodes.
 */
UCLASS()
class FORTNITEPORTINGRUNTIME_API UFortnitePortingAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float Speed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsOnGround = true;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsCrouching = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsStanding = true;

	/** Airborne after taking off while moving (running jump): plays Fortnite's forward jump instead of the standing one */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bJumpFromRun = false;

	/** Airborne after taking off from standstill / slow walk */
	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bJumpFromStand = false;

	/** Ground speed (cm/s) at take off from which the jump counts as a running jump */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	float RunningJumpSpeed = 150.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bHasPickaxe = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bNoPickaxe = true;

	/** Weight of the pickaxe upper body pose: 1 idle/crouched/airborne, 0 while running so the arms use the normal run animation */
	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	float PickaxeUpperAlpha = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Cosmetics")
	bool bIsGliding = false;

	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	bool bHasWeapon = false;

	/** Yaw (about the vertical) the upper body is turned from spine_01 up, so a two handed weapon at rest points ahead instead of across the body */
	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	FRotator UpperBodyTwist = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Vehicles")
	bool bIsInVehicle = false;

	UPROPERTY(BlueprintReadOnly, Category = "Traversal")
	bool bIsMantling = false;

	UPROPERTY(BlueprintReadOnly, Category = "Traversal")
	bool bIsSliding = false;

	/** 1 while sliding without a weapon: the arms take the unarmed/pickaxe pose instead of the knee slide's rifle arms */
	UPROPERTY(BlueprintReadOnly, Category = "Traversal")
	float SlideArmsAlpha = 0.0f;

	/** Left hand IK onto two-handed items (component space), driven by UFortnitePortingCharacterComponent */
	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	FVector LeftHandIKLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	FRotator LeftHandIKRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	FVector LeftHandIKJoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Weapons")
	float LeftHandIKAlpha = 0.0f;

	/** How fast the blend speed follows the real speed when speeding up / slowing down (higher = snappier) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	float SpeedEaseUp = 9.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Locomotion")
	float SpeedEaseDown = 7.0f;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	TWeakObjectPtr<ACharacter> OwnerCharacter;
	TWeakObjectPtr<UFortnitePortingCharacterComponent> CharacterComponent;
};

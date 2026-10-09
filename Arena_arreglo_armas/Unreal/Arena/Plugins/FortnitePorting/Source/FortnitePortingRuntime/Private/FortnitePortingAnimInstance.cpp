#include "FortnitePortingAnimInstance.h"

#include "FortnitePortingCharacterComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UFortnitePortingAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwnerCharacter = Cast<ACharacter>(TryGetPawnOwner());
	if (OwnerCharacter.IsValid())
	{
		CharacterComponent = OwnerCharacter->FindComponentByClass<UFortnitePortingCharacterComponent>();
	}
}

void UFortnitePortingAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// The body mesh initializes its anim instance when it registers, which happens before Blueprint-added
	// components (like UFortnitePortingCharacterComponent) exist. Keep looking until the component is found,
	// otherwise the body never sees bHasPickaxe while the weapon rig (created later) does.
	if (!OwnerCharacter.IsValid() || !CharacterComponent.IsValid())
	{
		NativeInitializeAnimation();
	}

	const ACharacter* Character = OwnerCharacter.Get();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement == nullptr)
	{
		return;
	}

	const UFortnitePortingCharacterComponent* Component = CharacterComponent.Get();

	bIsMantling = Component && Component->IsMantling();
	bIsSliding = Component && Component->IsSliding();
	bIsGliding = Component && Component->IsGliding();
	// A dual / one handed pickaxe holds its pose with its own montage. The two handed pickaxe pose of the Anim Blueprint stays off for it:
	// with the montage not yet (or no longer) at full weight it showed through as the arms raised over the head.
	bHasPickaxe = Component && Component->HasPickaxeEquipped() && !Component->PickaxeUsesOwnHold();
	bNoPickaxe = !bHasPickaxe;
	bHasWeapon = Component && Component->HasWeaponEquipped();
	// The torso node works in the mesh's component space, whose forward axis is the skeleton's own: tilting up and down is a turn about it
	UpperBodyTwist = FRotator(0.0f, Component ? Component->GetUpperBodyTwistYaw() : 0.0f, Component ? Component->GetUpperBodyAimPitch() : 0.0f);
	bIsInVehicle = Component && Component->IsInVehicle();

	// Only the visible body gets IK: the hidden weapon rig must keep the raw animation, the IK target is computed from it
	FTransform HandTarget;
	FVector JointTarget;
	const bool bIsBody = Character->GetMesh() == GetOwningComponent();
	const bool bWantsIK = bIsBody && Component && Component->GetLeftHandIKTarget(HandTarget, JointTarget);
	if (bWantsIK)
	{
		LeftHandIKLocation = HandTarget.GetLocation();
		LeftHandIKRotation = HandTarget.Rotator();
		LeftHandIKJoint = JointTarget;
	}
	LeftHandIKAlpha = FMath::FInterpConstantTo(LeftHandIKAlpha, bWantsIK ? 1.0f : 0.0f, DeltaSeconds, 8.0f);

	// Fortnite never pops between idle and run: the speed driving the blend eases in and out (~0.1 s),
	// so starting, stopping and turning around blend through the walk instead of snapping
	const float RawSpeed = Movement->Velocity.Size2D();
	Speed = FMath::FInterpTo(Speed, RawSpeed, DeltaSeconds, RawSpeed > Speed ? SpeedEaseUp : SpeedEaseDown);
	if (RawSpeed < 1.0f && Speed < 3.0f)
	{
		Speed = 0.0f;
	}
	bIsMoving = RawSpeed > 5.0f;

	SlideArmsAlpha = FMath::FInterpConstantTo(SlideArmsAlpha, bIsSliding && !bHasWeapon ? 1.0f : 0.0f, DeltaSeconds, 6.0f);
	const bool bWasInAir = bIsInAir;
	bIsInAir = Movement->IsFalling() && !bIsMantling;
	// The jump flavour is decided once, at take off, so it doesn't flip while the speed changes in the air
	if (bIsInAir && !bWasInAir)
	{
		bJumpFromRun = RawSpeed > RunningJumpSpeed;
	}
	bJumpFromStand = bIsInAir && !bJumpFromRun;
	bJumpFromRun = bIsInAir && bJumpFromRun;
	bIsOnGround = !bIsInAir;
	bIsCrouching = Movement->IsCrouching();
	bIsStanding = !bIsCrouching;

	// Fortnite runs with the pickaxe using the plain run animation; the pickaxe pose only shows when standing still
	// (or crouched / airborne). Keyed on the eased Speed so the arms blend instead of snapping.
	// The hidden weapon rig keeps the pickaxe pose: the items copy weapon_r/weapon_l from it relative to the hand
	const bool bRunningOnGround = bIsBody && Speed > 20.0f && bIsOnGround && !bIsCrouching;
	PickaxeUpperAlpha = FMath::FInterpConstantTo(PickaxeUpperAlpha, bRunningOnGround ? 0.0f : 1.0f, DeltaSeconds, 6.0f);
}

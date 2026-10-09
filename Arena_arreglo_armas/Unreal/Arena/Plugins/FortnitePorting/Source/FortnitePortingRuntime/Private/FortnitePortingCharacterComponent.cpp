#include "FortnitePortingCharacterComponent.h"

#include "FortnitePortingAnimInstance.h"
#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingEmoteMixer.h"
#include "FortnitePortingRuntime.h"
#include "FortnitePortingVehicle.h"
#include "FortnitePortingWeapon.h"
#include "FortnitePortingWeaponFX.h"
#include "EngineUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"

UFortnitePortingCharacterComponent::UFortnitePortingCharacterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

ACharacter* UFortnitePortingCharacterComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

USkeletalMeshComponent* UFortnitePortingCharacterComponent::GetBodyMesh() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetMesh() : nullptr;
}

UAnimInstance* UFortnitePortingCharacterComponent::GetAnimInstance() const
{
	const USkeletalMeshComponent* Mesh = GetBodyMesh();
	return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

float UFortnitePortingCharacterComponent::PlayMontage(UAnimMontage* Montage, float PlayRate) const
{
	UAnimInstance* AnimInstance = GetAnimInstance();
	if (Montage == nullptr || AnimInstance == nullptr)
	{
		return 0.0f;
	}

	const float Length = AnimInstance->Montage_Play(Montage, PlayRate);

	// The weapon rig runs the same Anim Blueprint, so it needs the same montages to keep weapon_r in sync
	if (UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr)
	{
		RigInstance->Montage_Play(Montage, PlayRate);
	}

	return Length;
}

void UFortnitePortingCharacterComponent::SetMontagePlayRate(UAnimMontage* Montage, float PlayRate) const
{
	if (Montage == nullptr)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		AnimInstance->Montage_SetPlayRate(Montage, PlayRate);
	}
	if (UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr)
	{
		RigInstance->Montage_SetPlayRate(Montage, PlayRate);
	}
}

void UFortnitePortingCharacterComponent::StopMontage(UAnimMontage* Montage, float BlendOutTime) const
{
	if (Montage == nullptr)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		AnimInstance->Montage_Stop(BlendOutTime, Montage);
	}

	if (UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr)
	{
		RigInstance->Montage_Stop(BlendOutTime, Montage);
	}
}

void UFortnitePortingCharacterComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("%s must be added to a Character"), *GetName());
		return;
	}

	USkeletalMeshComponent* Body = Character->GetMesh();

	// Parts copy the body pose, so they must evaluate after it to avoid a one frame lag
	TArray<USkeletalMeshComponent*> SkeletalComponents;
	Character->GetComponents<USkeletalMeshComponent>(SkeletalComponents);
	for (USkeletalMeshComponent* Component : SkeletalComponents)
	{
		if (Component != Body && Body != nullptr)
		{
			Component->AddTickPrerequisiteComponent(Body);
		}
	}

	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		if (bApplyJumpSettings)
		{
			Movement->GravityScale = GravityScale;
			Movement->JumpZVelocity = JumpZVelocity;
			Movement->AirControl = AirControl;
			Movement->BrakingDecelerationFalling = BrakingDecelerationFalling;
			Movement->FallingLateralFriction = 0.0f;
			Character->JumpMaxHoldTime = 0.0f;
			Character->JumpMaxCount = 1;
		}

		CachedAirControl = Movement->AirControl;

		if (bApplyMovementSettings)
		{
			Movement->MaxWalkSpeed = JogSpeed;
			Movement->MaxWalkSpeedCrouched = CrouchSpeed;
			Movement->NavAgentProps.bCanCrouch = true;
			Movement->bCanWalkOffLedgesWhenCrouching = true;      // crouched you still walk off the edge of a build (Fortnite); the engine default sticks you to it
			Movement->RotationRate = FRotator(0.0, TurnRate, 0.0);
		}

		// No tick prerequisite on the movement here: this component already ticks after the weapon rig,
		// which ticks after the body, which ticks after the movement. Making the movement wait for us
		// closed a cycle, so the body animated BEFORE the movement (one frame behind: the jitter when
		// turning the camera). The slide's velocity simply takes effect on the next movement update.
	}

	// A style picked before the character was in the world (replicated or restored from the locker)
	if (StyleData != nullptr && StyleSelection.Num() > 0)
	{
		ApplyStyleSelection();
	}

	// Characters are always close to the camera: keep their textures at full resolution instead of letting the
	// streamer drop mips (skins looked blurry/low resolution)
	TArray<UMeshComponent*> MeshComponents;
	Character->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		MeshComponent->bForceMipStreaming = true;
	}

	if (bAutoRegisterCosmetics)
	{
		RegisterProjectCosmetics();
	}

	CreateWeaponRig();

	// Online, only the server decides the starting state; clients receive it through NetState
	if (bEquipPickaxeOnStart && HasNetAuthority())
	{
		EquipPickaxe();
	}
}

void UFortnitePortingCharacterComponent::CreateWeaponRig()
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (Body == nullptr || WeaponRigMesh == nullptr || IsValid(WeaponRig))
	{
		return;
	}

	// Fortnite body meshes don't carry weapon_r: only Epic's master skeleton has it, and every player
	// animation moves it to place the held item in the hand. Without the bone the item can only follow
	// hand_r and never lines up with the fingers, so a hidden copy of the master skeleton runs the same
	// animations purely to read weapon_r from (the Blender addon does the same with a hidden armature).
	if (Body->GetBoneIndex(TEXT("weapon_r")) != INDEX_NONE)
	{
		return;
	}

	WeaponRig = NewObject<USkeletalMeshComponent>(GetOwner(), TEXT("FortnitePortingWeaponRig"));
	WeaponRig->SetSkeletalMeshAsset(WeaponRigMesh);
	WeaponRig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeaponRig->SetCastShadow(false);
	WeaponRig->SetVisibility(false);
	WeaponRig->SetHiddenInGame(true);
	// Hidden meshes skip animation by default; this one exists only to be animated
	WeaponRig->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	WeaponRig->SetAnimInstanceClass(Body->GetAnimClass());
	WeaponRig->RegisterComponent();
	WeaponRig->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	if (WeaponRig->GetBoneIndex(TEXT("weapon_r")) == INDEX_NONE)
	{
		UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("Weapon rig %s has no weapon_r bone; held items will follow hand_r"), *WeaponRigMesh->GetName());
		WeaponRig->DestroyComponent();
		WeaponRig = nullptr;
		return;
	}

	// Read weapon_r after the rig has animated this frame
	PrimaryComponentTick.AddPrerequisite(WeaponRig, WeaponRig->PrimaryComponentTick);

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("Held items follow weapon_r from %s"), *WeaponRigMesh->GetName());
}

FName UFortnitePortingCharacterComponent::ResolveHandAttachment(FName RequestedSocket, const TCHAR* DebugLabel, FName& OutDriverBone) const
{
	OutDriverBone = NAME_None;
	USkeletalMeshComponent* Body = GetBodyMesh();

	// Attach to the bone weapon_r hangs from on the visible body, and copy weapon_r's animated
	// offset from the rig every frame: the item stays glued to this body's hand while getting
	// the exact grip (and swing rotation) the animation authored.
	if (Body != nullptr && Body->GetBoneIndex(RequestedSocket) == INDEX_NONE && IsValid(WeaponRig) && WeaponRig->GetBoneIndex(RequestedSocket) != INDEX_NONE)
	{
		const FName ParentBone = WeaponRig->GetParentBone(RequestedSocket);
		if (!ParentBone.IsNone() && Body->DoesSocketExist(ParentBone))
		{
			OutDriverBone = RequestedSocket;
			UE_LOG(LogFortnitePortingRuntime, Log, TEXT("%s attached to %s, driven by animated %s"), DebugLabel, *ParentBone.ToString(), *RequestedSocket.ToString());
			return ParentBone;
		}
	}

	return ResolveAttachSocket(Body, RequestedSocket, DebugLabel);
}

FTransform UFortnitePortingCharacterComponent::ComputeAttachTransform(const FTransform& Offset, const FTransform& Adjustment, FName DriverBone, const UObject* ItemMesh) const
{
	FTransform Local = ApplyGripAdjustment(Offset, Adjustment);

	// Imported item skeletons carry the import axis conversion in their root bone (b_AR_Root has a 90 degree yaw),
	// so the geometry is turned relative to the root. Fortnite's weapon_r expects the item root to sit on it,
	// so undo the root's reference pose: otherwise the handle ends up ~12cm outside the fingers.
	if (const USkeletalMesh* SkeletalItem = Cast<USkeletalMesh>(ItemMesh))
	{
		const TArray<FTransform>& RefPose = SkeletalItem->GetRefSkeleton().GetRefBonePose();
		if (RefPose.Num() > 0 && !RefPose[0].Equals(FTransform::Identity))
		{
			Local = RefPose[0].Inverse() * Local;
		}
	}

	if (DriverBone.IsNone() || !IsValid(WeaponRig))
	{
		return Local;
	}

	const FName ParentBone = WeaponRig->GetParentBone(DriverBone);
	const FTransform BoneInComponent = WeaponRig->GetSocketTransform(DriverBone, RTS_Component);
	const FTransform ParentInComponent = WeaponRig->GetSocketTransform(ParentBone, RTS_Component);
	return Local * BoneInComponent.GetRelativeTransform(ParentInComponent);
}

FName UFortnitePortingCharacterComponent::ResolveAttachSocket(const USkeletalMeshComponent* Body, FName RequestedSocket, const TCHAR* DebugLabel)
{
	if (Body == nullptr || Body->DoesSocketExist(RequestedSocket))
	{
		return RequestedSocket;
	}

	const USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	const FString MeshName = Mesh ? Mesh->GetName() : TEXT("(none)");

	static const FName HandFallbacks[] = { TEXT("hand_r"), TEXT("hand_rSocket"), TEXT("ik_hand_r"), TEXT("RightHand"), TEXT("hand_R") };
	for (const FName& Fallback : HandFallbacks)
	{
		if (Fallback != RequestedSocket && Body->DoesSocketExist(Fallback))
		{
			UE_LOG(LogFortnitePortingRuntime, Warning,
				TEXT("%s: body mesh %s has no '%s' socket/bone; attaching to '%s' instead"),
				DebugLabel, *MeshName, *RequestedSocket.ToString(), *Fallback.ToString());
			return Fallback;
		}
	}

	// Last resort for a non-standard rig: any bone that looks like the right hand (ends in "_r")
	if (Mesh != nullptr)
	{
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		for (int32 Index = 0; Index < RefSkeleton.GetNum(); ++Index)
		{
			const FString BoneName = RefSkeleton.GetBoneName(Index).ToString();
			if (BoneName.Contains(TEXT("hand"), ESearchCase::IgnoreCase) && BoneName.EndsWith(TEXT("_r"), ESearchCase::IgnoreCase))
			{
				UE_LOG(LogFortnitePortingRuntime, Warning,
					TEXT("%s: body mesh %s has no '%s' socket/bone; attaching to bone '%s' instead"),
					DebugLabel, *MeshName, *RequestedSocket.ToString(), *BoneName);
				return FName(*BoneName);
			}
		}
	}

	UE_LOG(LogFortnitePortingRuntime, Error,
		TEXT("%s: body mesh %s has no '%s' socket/bone and no right-hand bone was found; it will attach at the mesh origin instead of the hand"),
		DebugLabel, *MeshName, *RequestedSocket.ToString());
	return RequestedSocket;
}

FTransform UFortnitePortingCharacterComponent::ApplyGripAdjustment(const FTransform& Offset, const FTransform& Adjustment)
{
	// Identity check first: this runs every equip, and most setups (weapon_r socket present) need no adjustment.
	if (Adjustment.Equals(FTransform::Identity))
	{
		return Offset;
	}

	FTransform Result = Offset;
	Result.SetRotation(Offset.GetRotation() * Adjustment.GetRotation());
	Result.SetLocation(Offset.GetLocation() + Offset.GetRotation().RotateVector(Adjustment.GetLocation()));
	return Result;
}

void UFortnitePortingCharacterComponent::TickGripAdjustments()
{
	if (bPickaxeEquipped && Pickaxe != nullptr)
	{
		const int32 Count = FMath::Min(PickaxeComponents.Num(), Pickaxe->Meshes.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (UMeshComponent* Component = PickaxeComponents[Index]; IsValid(Component))
			{
				const FName DriverBone = PickaxeDriverBones.IsValidIndex(Index) ? PickaxeDriverBones[Index] : NAME_None;
				Component->SetRelativeTransform(ComputeAttachTransform(Pickaxe->Meshes[Index].Offset, PickaxeGripAdjustment, DriverBone, Pickaxe->Meshes[Index].Mesh));
			}
		}
	}

	if (CurrentWeapon != nullptr)
	{
		if (IsValid(WeaponActor) && CurrentWeapon->Meshes.Num() > 0)
		{
			WeaponActor->SetActorRelativeTransform(ComputeAttachTransform(CurrentWeapon->Meshes[0].Offset, WeaponGripAdjustment, WeaponDriverBone, CurrentWeapon->Meshes[0].Mesh) * FTransform(WeaponFitRotation));
		}

		// OffhandComponents[i] corresponds to CurrentWeapon->Meshes[i + 1] (index 0 is the main weapon, spawned separately)
		const int32 Count = FMath::Min(OffhandComponents.Num(), FMath::Max(CurrentWeapon->Meshes.Num() - 1, 0));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (UMeshComponent* Component = OffhandComponents[Index]; IsValid(Component))
			{
				const FName DriverBone = OffhandDriverBones.IsValidIndex(Index) ? OffhandDriverBones[Index] : NAME_None;
				Component->SetRelativeTransform(ComputeAttachTransform(CurrentWeapon->Meshes[Index + 1].Offset, WeaponGripAdjustment, DriverBone, CurrentWeapon->Meshes[Index + 1].Mesh));
			}
		}
	}
}

void UFortnitePortingCharacterComponent::UpdateCameraCrouchOffset()
{
	ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	USpringArmComponent* Boom = Character ? Character->FindComponentByClass<USpringArmComponent>() : nullptr;
	if (!bKeepCameraHeightWhenCrouching || Movement == nullptr || Boom == nullptr || Boom->GetAttachParent() != Character->GetRootComponent())
	{
		return;
	}

	FVector Location = Boom->GetRelativeLocation();
	if (!bCameraBaseCached)
	{
		CameraBaseZ = Location.Z;
		bCameraBaseCached = true;
	}

	// The capsule centre drops by (standing - crouched) half height; raise the boom by the same amount
	const float Adjust = Character->bIsCrouched ? Character->GetDefaultHalfHeight() - Movement->GetCrouchedHalfHeight() : 0.0f;
	if (!FMath::IsNearlyEqual(Location.Z, CameraBaseZ + Adjust))
	{
		Location.Z = CameraBaseZ + Adjust;
		Boom->SetRelativeLocation(Location);
	}
}

bool UFortnitePortingCharacterComponent::GetLeftHandIKTarget(FTransform& OutHand, FVector& OutJoint) const
{
	if (!bLeftHandIKValid)
	{
		return false;
	}

	OutHand = LeftHandIKTransform;
	OutJoint = LeftHandIKJoint;
	return true;
}

bool UFortnitePortingCharacterComponent::UpdateWeaponLeftHandIK(float DeltaTime)
{
	const ACharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBodyMesh();
	const FName Type = CurrentWeapon ? FName(*CurrentWeapon->WeaponType.ToString()) : NAME_None;
	const bool bTwoHanded = Type == TEXT("Rifle") || Type == TEXT("Shotgun") || Type == TEXT("Sniper") || Type == TEXT("SMG") || Type == TEXT("Launcher");
	static const FName HandL(TEXT("hand_l"));
	static const FName LowerArmL(TEXT("lowerarm_l"));
	// The hidden weapon rig, when the skin has one, plays the raw animation; without it the body itself is the reference
	const USkeletalMeshComponent* Source = IsValid(WeaponRig) ? WeaponRig.Get() : Body;
	// Fortnite weapons carry an "Underbarrel" socket where the off hand goes. With it the hand follows the weapon at all times (drawing it,
	// aiming), the hold poses being made for another mesh of the same type; without it the learned grip below is the fallback.
	static const FName UnderbarrelSocket(TEXT("Underbarrel"));
	const USkeletalMeshComponent* WeaponMeshComponent = IsValid(WeaponActor) ? WeaponActor->WeaponMesh.Get() : nullptr;
	const bool bHasGripSocket = WeaponMeshComponent != nullptr && WeaponMeshComponent->DoesSocketExist(UnderbarrelSocket);
	const bool bPlayingEquip = ActiveWeaponMontage != nullptr && bActiveMontageIsEquip;
	const bool bBusy = ActiveWeaponMontage != nullptr && !(bHasGripSocket && bPlayingEquip);
	const bool bAimingNow = GetAimAlpha() >= 0.05f;
	if (!bLeftHandIK || Character == nullptr || Body == nullptr || Source == nullptr || !IsValid(WeaponActor) || !bTwoHanded || bCombatBlocked
		|| CurrentVehicle != nullptr || ActiveEmoteMontage != nullptr || bBusy || (bAimingNow && !bHasGripSocket)
		|| Source->GetBoneIndex(HandL) == INDEX_NONE || Source->GetBoneIndex(LowerArmL) == INDEX_NONE)
	{
		return false;
	}

	if (bHasGripSocket)
	{
		// The hand goes to the socket and takes its turn (LeftHandSocketRotation fine tunes it)
		const FTransform HandWorld = Source->GetSocketTransform(HandL, RTS_World);
		// The hand's turn comes from the socket and never from the hand itself: the bone read back is the one this IK has just moved, so taking
		// its turn as the next target made the wrist spin a little more every frame. The fixed turn is how the hold poses hold a weapon (hand axes in
		// the socket's axes, measured on the rifle, the shotgun and the sniper, which agree to a few degrees).
		static const FQuat HandInSocket = FRotationMatrix::MakeFromXY(FVector(0.69f, 0.18f, -0.70f), FVector(0.35f, -0.93f, 0.11f)).ToQuat();
		const FQuat HandRotation = bLeftHandFromSocket
			? WeaponMeshComponent->GetSocketTransform(UnderbarrelSocket).GetRotation() * HandInSocket * LeftHandSocketRotation.Quaternion()
			: HandWorld.GetRotation();
		// The palm is a hand's length from the wrist along the forearm: the wrist goes back from the socket by that much
		const FVector Fingers = (HandWorld.GetLocation() - Source->GetSocketLocation(LowerArmL)).GetSafeNormal();
		FVector Wrist = WeaponMeshComponent->GetSocketLocation(UnderbarrelSocket) - Fingers * LeftHandPalmOffset;

		// A long fore end would have the arm straight out: the hand slides back along the barrel until the arm is not stretched out
		static const FName UpperArmL(TEXT("upperarm_l"));
		const FVector Shoulder = Source->GetSocketLocation(UpperArmL);
		const float ArmLength = FVector::Dist(Shoulder, Source->GetSocketLocation(LowerArmL)) + FVector::Dist(Source->GetSocketLocation(LowerArmL), HandWorld.GetLocation());
		const FVector Barrel = (WeaponActor->GetMuzzleLocation() - WeaponActor->GetActorLocation()).GetSafeNormal();
		for (float Back = 0.0f; Back < 30.0f && FVector::Dist(Shoulder, Wrist) > ArmLength * LeftHandMaxReach; Back += 1.0f)
		{
			Wrist -= Barrel;
		}
		const FTransform TargetWorld(HandRotation, Wrist);
		const FTransform TargetCS = TargetWorld.GetRelativeTransform(Body->GetComponentTransform());
		const FTransform HandLCS = Source->GetSocketTransform(HandL, RTS_Component);
		const FVector ElbowCS = Source->GetSocketTransform(LowerArmL, RTS_Component).GetLocation();
		LeftHandIKTransform = TargetCS;
		LeftHandIKJoint = ElbowCS + (TargetCS.GetLocation() - HandLCS.GetLocation());
		bLeftHandIKValid = true;
		return true;
	}

	// At rest the animation puts the left hand near the weapon: remember where the fore end is, relative to the weapon (once its barrel is lined up)
	if (Character->GetVelocity().Size2D() < 20.0f && WeaponFitPasses >= 4)
	{
		WeaponGripSettle += DeltaTime;
		if (WeaponGripSettle > 0.6f)
		{
			// Fortnite's hold poses are shared between weapons of a type, so against this particular mesh the left hand can sit a hand's width off
			// the fore end. The hand keeps the turn the animator gave it, and only slides onto the fore end: along the barrel where the pose
			// already reaches to, and resting on the weapon rather than through it.
			const FTransform HandWorld = Source->GetSocketTransform(HandL, RTS_World);
			const FVector Grip = WeaponActor->GetActorLocation();
			const FVector Barrel = WeaponActor->GetMuzzleLocation() - Grip;
			const float Length = Barrel.Size();
			if (Length > 20.0f)
			{
				const FVector Direction = Barrel / Length;
				const float Along = FMath::Clamp(FVector::DotProduct(HandWorld.GetLocation() - Grip, Direction), Length * 0.3f, Length * 0.7f);
				const FVector OnBarrel = Grip + Direction * Along;
				const FVector Off = (HandWorld.GetLocation() - OnBarrel).GetClampedToMaxSize(3.0f);
				LeftHandOnWeapon = FTransform(HandWorld.GetRotation(), OnBarrel + Off).GetRelativeTransform(WeaponActor->GetActorTransform());
				bLeftHandOnWeaponValid = true;
			}
		}
	}
	else
	{
		WeaponGripSettle = 0.0f;
	}
	if (!bLeftHandOnWeaponValid)
	{
		return false;
	}

	// Put the left hand on that point whatever the arms are doing (walking, turning): the target is in the body's component space
	const FTransform TargetCS = (LeftHandOnWeapon * WeaponActor->GetActorTransform()).GetRelativeTransform(Body->GetComponentTransform());
	const FTransform HandLCS = Source->GetSocketTransform(HandL, RTS_Component);
	const FVector ElbowCS = Source->GetSocketTransform(LowerArmL, RTS_Component).GetLocation();
	LeftHandIKTransform = TargetCS;
	LeftHandIKJoint = ElbowCS + (TargetCS.GetLocation() - HandLCS.GetLocation());
	bLeftHandIKValid = true;
	return true;
}

void UFortnitePortingCharacterComponent::UpdateWeaponFit(float DeltaTime)
{
	// Fortnite's relaxed hold poses (Pose_Rifle_Relaxed_CMF and the like) are whole body poses that carry the weapon across the chest, and only
	// their upper half is blended in, so the hands end up holding the weapon out to the side. The weapon sits right in the hand, so it is the
	// weapon that is turned about the vertical until the barrel points ahead; the left hand then rides the fore end through the IK.
	const ACharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBodyMesh();
	const FName Type = CurrentWeapon ? FName(*CurrentWeapon->WeaponType.ToString()) : NAME_None;
	const bool bTwoHanded = Type == TEXT("Rifle") || Type == TEXT("Shotgun") || Type == TEXT("Sniper") || Type == TEXT("SMG") || Type == TEXT("Launcher");
	const bool bReady = Character && Body && IsValid(WeaponActor) && bTwoHanded && ActiveWeaponMontage == nullptr && !bCombatBlocked
		&& CurrentVehicle == nullptr && ActiveEmoteMontage == nullptr && GetAimAlpha() < 0.05f && Character->GetVelocity().Size2D() < 20.0f;
	if (!bReady || WeaponFitPasses >= 4)
	{
		WeaponFitSettle = 0.0f;
		return;
	}
	WeaponFitSettle += DeltaTime;
	if (WeaponFitSettle < 0.35f)
	{
		return;
	}
	WeaponFitSettle = 0.15f;

	const FVector HandR = Body->GetSocketLocation(TEXT("hand_r"));
	const FVector Barrel = WeaponActor->GetMuzzleLocation() - HandR;
	if (Barrel.Size() < 25.0f)
	{
		return;
	}

	// Where the barrel points now against where a weapon held ready points: ahead, a little to the character's left and a little down.
	// Both readings come from the body and the weapon alone, never from the IK hand, so this cannot chase its own tail.
	const FVector Pointing = Barrel.GetSafeNormal();
	const FVector Wanted = FRotator(RelaxedWeaponPitch, Character->GetActorRotation().Yaw - RelaxedWeaponAngle, 0.0f).Vector();
	const float Off = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Pointing, Wanted), -1.0f, 1.0f)));
	++WeaponFitPasses;

	// A hold pose that keeps the weapon anywhere near the front is the animator's, and is left exactly as it is. This only catches a weapon
	// held right across the body, which is what a "Relaxed" carry pose looks like when it stands in for the combat one.
	if (Off < CrossBodyWeaponAngle)
	{
		WeaponFitPasses = 4;
		return;
	}

	// The weapon's world rotation is Parent * Fit * Attach, so a turn that lands the barrel on Wanted in the world goes in as Parent^-1 * Turn * Parent
	const USceneComponent* Root = WeaponActor->GetRootComponent();
	const USceneComponent* Parent = Root ? Root->GetAttachParent() : nullptr;
	if (Parent == nullptr)
	{
		return;
	}
	const FQuat ParentRotation = Parent->GetSocketTransform(Root->GetAttachSocketName()).GetRotation();
	const FQuat Turn = FQuat::FindBetweenNormals(Pointing, Wanted);
	WeaponFitRotation = ParentRotation.Inverse() * Turn * ParentRotation * WeaponFitRotation;
	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("%s: barrel turned %.0f degrees onto the relaxed carry"), *CurrentWeapon->GetName(), Off);
}

void UFortnitePortingCharacterComponent::UpdateWeaponTwist(float DeltaTime)
{
	// The body keeps the pose the animation gives it: where the weapon points is set by turning the weapon in the hand (UpdateWeaponFit),
	// never by turning the spine, which is what tore the shoulder and the head round. The node stays in the graph, doing nothing.
	UpperBodyTwistYaw = FMath::FInterpTo(UpperBodyTwistYaw, 0.0f, DeltaTime, 8.0f);

	// While aiming the weapon has to point where the crosshair is: the character already turns to the camera's yaw, and the torso takes
	// a share of its pitch. The base aim rotation is replicated, so other players see the same.
	const ACharacter* Character = GetCharacter();
	const bool bAimNow = (bAiming || bAimingNet) && CurrentWeapon != nullptr && Character != nullptr;
	const float Wanted = bAimNow ? FMath::Clamp(FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch) * AimPitchShare, -50.0f, 50.0f) : 0.0f;
	UpperBodyAimPitch = FMath::FInterpTo(UpperBodyAimPitch, Wanted, DeltaTime, 12.0f);
}

void UFortnitePortingCharacterComponent::UpdateLeftHandIK()
{
	bLeftHandIKValid = false;
	if (CurrentWeapon != nullptr)
	{
		UpdateWeaponLeftHandIK(GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f);
		return;
	}

	USkeletalMeshComponent* Body = GetBodyMesh();
	if (!bLeftHandIK || !bPickaxeEquipped || Body == nullptr || !IsValid(WeaponRig) || LeftHandGripSocket.IsNone())
	{
		return;
	}

	const USkeletalMeshComponent* Item = nullptr;
	for (const UMeshComponent* Component : PickaxeComponents)
	{
		const USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(Component);
		if (IsValid(Skeletal) && Skeletal->IsVisible() && Skeletal->DoesSocketExist(LeftHandGripSocket))
		{
			Item = Skeletal;
			break;
		}
	}

	static const FName HandL(TEXT("hand_l"));
	static const FName HandR(TEXT("hand_r"));
	static const FName LowerArmL(TEXT("lowerarm_l"));
	if (Item == nullptr || WeaponRig->GetBoneIndex(HandL) == INDEX_NONE || WeaponRig->GetBoneIndex(HandR) == INDEX_NONE || WeaponRig->GetBoneIndex(LowerArmL) == INDEX_NONE)
	{
		return;
	}

	// The rig plays the same animation without IK and is attached to the body at identity, so its component
	// space is the body's component space. The item is attached to the body (right hand), expressed there too.
	const FTransform& BodyToWorld = Body->GetComponentTransform();
	const FTransform HandRCS = WeaponRig->GetSocketTransform(HandR, RTS_Component);
	const FTransform HandLCS = WeaponRig->GetSocketTransform(HandL, RTS_Component);
	const FVector ElbowCS = WeaponRig->GetSocketTransform(LowerArmL, RTS_Component).GetLocation();
	const FTransform ItemCS = Item->GetComponentTransform().GetRelativeTransform(BodyToWorld);

	// Handle axis: through the grip socket, along the item's up axis (Fortnite handles are modelled along Z)
	const FVector GripCS = ItemCS.TransformPosition(Item->GetSocketTransform(LeftHandGripSocket, RTS_Component).GetLocation());
	const FVector AxisCS = ItemCS.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (AxisCS.IsNearlyZero())
	{
		return;
	}

	// How the right hand holds the handle (grip point and handle direction in hand_r space). Fortnite's left bones
	// mirror the right ones with negated axes, so the same grip for the left hand is the negated local values.
	const FVector RightGripCS = GripCS + AxisCS * FVector::DotProduct(HandRCS.GetLocation() - GripCS, AxisCS);
	const FVector LeftGripLocal = -HandRCS.InverseTransformPosition(RightGripCS);
	const FVector LeftAxisLocal = -HandRCS.InverseTransformVectorNoScale(AxisCS);

	// Rotate the animated left hand so its grip lines up with the handle, then slide it onto the grip socket
	const FVector CurrentAxis = HandLCS.TransformVectorNoScale(LeftAxisLocal).GetSafeNormal();
	const FVector TargetAxis = FVector::DotProduct(CurrentAxis, AxisCS) >= 0.0f ? AxisCS : -AxisCS;
	const FQuat Correction = FQuat::FindBetweenNormals(CurrentAxis, TargetAxis);

	FTransform Target = HandLCS;
	Target.SetRotation(Correction * HandLCS.GetRotation());
	const FVector Delta = GripCS - Target.TransformPosition(LeftGripLocal);
	if (Delta.Size() > 40.0f)
	{
		// Pose is nowhere near a two-handed hold (equip/emote); leave the animation alone
		return;
	}

	Target.AddToTranslation(Delta);
	LeftHandIKTransform = Target;
	LeftHandIKJoint = ElbowCS + Delta;
	bLeftHandIKValid = true;
}

void UFortnitePortingCharacterComponent::DebugWeaponRig(float DeltaTime)
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (Body == nullptr)
	{
		return;
	}

	const FName Hand(TEXT("hand_r"));
	const FName Weapon(TEXT("weapon_r"));
	if (Body->DoesSocketExist(Hand))
	{
		DrawDebugCoordinateSystem(GetWorld(), Body->GetSocketLocation(Hand), Body->GetSocketRotation(Hand), 12.0f, false, -1.0f, SDPG_Foreground, 0.5f);
	}
	if (IsValid(WeaponRig) && WeaponRig->GetBoneIndex(Weapon) != INDEX_NONE)
	{
		DrawDebugCoordinateSystem(GetWorld(), WeaponRig->GetSocketLocation(Weapon), WeaponRig->GetSocketRotation(Weapon), 20.0f, false, -1.0f, SDPG_Foreground, 1.0f);
	}

	WeaponRigDebugTimer -= DeltaTime;
	if (WeaponRigDebugTimer > 0.0f)
	{
		return;
	}
	WeaponRigDebugTimer = 2.0f;

	if (!IsValid(WeaponRig))
	{
		UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("[RigDebug] no weapon rig"));
		return;
	}

	UAnimInstance* RigAnim = WeaponRig->GetAnimInstance();
	UAnimInstance* BodyAnim = Body->GetAnimInstance();
	const int32 WeaponIndex = WeaponRig->GetBoneIndex(Weapon);
	const FTransform RigLocal = WeaponRig->GetSocketTransform(Weapon, RTS_Component).GetRelativeTransform(WeaponRig->GetSocketTransform(WeaponRig->GetParentBone(Weapon), RTS_Component));
	const FTransform RefLocal = WeaponIndex != INDEX_NONE ? WeaponRig->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose()[WeaponIndex] : FTransform::Identity;
	const float HandGap = Body->DoesSocketExist(Hand) && WeaponRig->GetBoneIndex(Hand) != INDEX_NONE
		? FVector::Dist(Body->GetSocketLocation(Hand), WeaponRig->GetSocketLocation(Hand)) : -1.0f;

	UE_LOG(LogFortnitePortingRuntime, Log,
		TEXT("[RigDebug] rigAnim=%s bodyAnim=%s rigMontage=%s bodyMontage=%s parent=%s weaponLocal loc=%s rot=%s | refPose loc=%s rot=%s | hand_r gap body/rig=%.1fcm | pickaxe=%d"),
		RigAnim ? *RigAnim->GetClass()->GetName() : TEXT("NONE"),
		BodyAnim ? *BodyAnim->GetClass()->GetName() : TEXT("NONE"),
		RigAnim && RigAnim->GetCurrentActiveMontage() ? *RigAnim->GetCurrentActiveMontage()->GetName() : TEXT("-"),
		BodyAnim && BodyAnim->GetCurrentActiveMontage() ? *BodyAnim->GetCurrentActiveMontage()->GetName() : TEXT("-"),
		*WeaponRig->GetParentBone(Weapon).ToString(),
		*RigLocal.GetLocation().ToString(), *RigLocal.Rotator().ToString(),
		*RefLocal.GetLocation().ToString(), *RefLocal.Rotator().ToString(),
		HandGap, bPickaxeEquipped ? 1 : 0);
}

void UFortnitePortingCharacterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CurrentVehicle)
	{
		FinishExitVehicle();
	}
	EndAim();
	StopSlide();
	StopEmoteSounds();
	UnequipWeapon();
	UnequipPickaxe();
	StopGliding();
	if (IsValid(WeaponRig))
	{
		WeaponRig->DestroyComponent();
	}
	WeaponRig = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UFortnitePortingCharacterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement == nullptr)
	{
		return;
	}

	FallTime = Movement->IsFalling() ? FallTime + DeltaTime : 0.0f;
	MantleCooldown = FMath::Max(0.0f, MantleCooldown - DeltaTime);
	VehicleCooldown = FMath::Max(0.0f, VehicleCooldown - DeltaTime);
	SlideCooldown = FMath::Max(0.0f, SlideCooldown - DeltaTime);

	if (bEnableDefaultControls)
	{
		HandleDefaultControls();
	}

	if (IsNetworked() && HasNetAuthority())
	{
		SyncNetStateFromLocal();

		// The server's copy of a remote player's pawn never runs the controls, so it keeps its own sprint speed in
		// step with the crouch state (the owner does the same locally every frame)
		if (!DrivesMovement())
		{
			SetSprinting(bSprinting);
		}
	}

	TickSlide(DeltaTime);
	TickPickaxeCombo(DeltaTime);
	TickTurning(DeltaTime);

	if (PendingEmote)
	{
		PendingEmoteElapsed += DeltaTime;
		const ACharacter* EmoteCharacter = GetCharacter();
		const UCharacterMovementComponent* EmoteMovement = EmoteCharacter ? EmoteCharacter->GetCharacterMovement() : nullptr;
		if (PendingEmoteElapsed >= 10.0f)
		{
			UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("Discarding queued emote %s after waiting for a grounded character"), *PendingEmote->GetName());
			PendingEmote = nullptr;
			PendingEmoteElapsed = 0.0f;
		}
		else if (EmoteCharacter && EmoteMovement && !bGliding && !bMantling && !EmoteMovement->IsFalling())
		{
			UFortnitePortingEmoteData* Emote = PendingEmote;
			PendingEmote = nullptr;
			PendingEmoteElapsed = 0.0f;
			PlayEmote(Emote);
		}
	}

	TickEmote();
	TickEmoteSounds(DeltaTime);
	TickEmoteMusicFade(DeltaTime);
	TickGlider();
	TickMantle(DeltaTime);
	TickWeapon(DeltaTime);
	TickAim(DeltaTime);
	TickPickaxeHold();
	TickVehicle();
	TickGripAdjustments();
	UpdateLeftHandIK();
	UpdateWeaponTwist(DeltaTime);
	UpdateWeaponFit(DeltaTime);
	UpdateCameraCrouchOffset();
	if (bDebugWeaponRig)
	{
		DebugWeaponRig(DeltaTime);
	}

	// Auto mantle: pushing forward against a ledge while airborne (only the owning player has the input)
	if (bEnableMantle && DrivesMovement() && !bMantling && MantleCooldown <= 0.0f && Movement->IsFalling())
	{
		const FVector Input = Character->GetLastMovementInputVector().GetSafeNormal2D();
		const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
		if (!Input.IsNearlyZero() && FVector::DotProduct(Input, Forward) > 0.5f)
		{
			TryMantle();
		}
	}
}

void UFortnitePortingCharacterComponent::HandleDefaultControls()
{
	ACharacter* Character = GetCharacter();
	APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (Controller == nullptr || !Controller->IsLocalController() || bLobbyIdle || bGameplayInputBlocked)
	{
		return;
	}

	// The E press that got us out of a vehicle must not put us straight back in
	if (VehicleCooldown <= 0.0f && Controller->WasInputKeyJustPressed(InteractKey))
	{
		if (AFortnitePortingVehicle* Vehicle = FindEnterableVehicle())
		{
			RequestEnterVehicle(Vehicle);
			return;
		}
	}

	RequestSprint(Controller->IsInputKeyDown(SprintKey));

	// Crouching while sprinting slides (Fortnite). The template also crouches on SlideKey, which the slide reuses.
	// Both keys toggle: one press crouches (or slides when sprinting), the next press stands up
	const bool bCrouchPressed = Controller->WasInputKeyJustPressed(CrouchKey) || Controller->WasInputKeyJustPressed(SlideKey);
	const bool bSlidePressed = bCrouchPressed;
	if (bSlidePressed && !bSliding && TryStartSlide())
	{
		// handled
	}
	else if (bCrouchPressed)
	{
		if (Character->bIsCrouched || bSliding)
		{
			StopSlide();
			Character->UnCrouch();
		}
		else
		{
			Character->Crouch();
		}
	}

	// A crouched character cannot jump: stand up first (and keep the slide's speed), like Fortnite
	if ((bSliding || Character->bIsCrouched) && Controller->WasInputKeyJustPressed(JumpKey))
	{
		StopSlide();
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->bWantsToCrouch = false;
			Movement->UnCrouch(false);
		}
		Character->Jump();
	}

	if (Controller->WasInputKeyJustPressed(PickaxeKey))
	{
		RequestTogglePickaxe();
	}

	if (Controller->WasInputKeyJustPressed(WeaponKey))
	{
		RequestCycleWeapon();
	}

	if (bCombatBlocked)
	{
		// building: the mouse places pieces, it must not swing the pickaxe or shoot
	}
	else if (CurrentWeapon)
	{
		const bool bWantsToFire = CurrentWeapon->bAutomatic ? Controller->IsInputKeyDown(FireKey) : Controller->WasInputKeyJustPressed(FireKey);
		if (bWantsToFire)
		{
			RequestFire();
		}

		if (Controller->WasInputKeyJustPressed(ReloadKey))
		{
			RequestReload();
		}
	}
	else if (bPickaxeEquipped && Controller->WasInputKeyJustPressed(SwingKey))
	{
		RequestSwingPickaxe();
	}

	if (Controller->WasInputKeyJustPressed(GliderKey) && FallTime > 0.35f)
	{
		RequestGlider(!bGliding);
	}

	if (Controller->WasInputKeyJustPressed(EmoteKey) && Emotes.Num() > 0)
	{
		const int32 Index = NextEmoteIndex++ % Emotes.Num();
		RequestEmote(Emotes[Index]);
	}
}

void UFortnitePortingCharacterComponent::TickTurning(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!bApplyMovementSettings || Movement == nullptr)
	{
		return;
	}

	// The turn itself stays in the movement component (bOrientRotationToMovement): it runs before the camera
	// boom, so the camera never sees a rotation it hasn't followed yet (rotating the actor here, after the boom,
	// made the camera twitch sideways every frame). Easing = a turn rate proportional to what is left to turn:
	// fast at the start, soft at the end.
	float Rate = TurnRate;
	const FVector Direction = Movement->GetCurrentAcceleration().GetSafeNormal2D();
	if (bSmoothTurning && !Direction.IsNearlyZero())
	{
		const float Remaining = FMath::Abs(FMath::FindDeltaAngleDegrees(Character->GetActorRotation().Yaw, Direction.Rotation().Yaw));
		Rate = FMath::Clamp(Remaining * TurnSharpness, 120.0f, TurnRate);
	}
	Movement->RotationRate = FRotator(0.0, Rate, 0.0);
}

void UFortnitePortingCharacterComponent::SetSprinting(bool bSprint)
{
	bSprinting = bSprint;

	const ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement == nullptr || !bApplyMovementSettings)
	{
		return;
	}

	Movement->MaxWalkSpeed = bSprinting && !Movement->IsCrouching() ? SprintSpeed : JogSpeed;
}

void UFortnitePortingCharacterComponent::SetGameplayInputBlocked(bool bBlocked)
{
	bGameplayInputBlocked = bBlocked;
	if (bBlocked)
	{
		bSwingQueued = false;
		RequestSprint(false);
		StopSlide();
	}
}

void UFortnitePortingCharacterComponent::UseUpperBodySlot(UAnimMontage* Montage)
{
	static const FName UpperBodySlot(TEXT("UpperBody"));
	if (Montage == nullptr || Montage->SlotAnimTracks.Num() == 0 || Montage->SlotAnimTracks[0].SlotName == UpperBodySlot)
	{
		return;
	}

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("%s: slot %s -> UpperBody so the legs keep walking"), *Montage->GetName(), *Montage->SlotAnimTracks[0].SlotName.ToString());
	Montage->SlotAnimTracks[0].SlotName = UpperBodySlot;
}

bool UFortnitePortingCharacterComponent::CanSlide() const
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return bEnableSlide && Movement && bSprinting && !bSliding && SlideCooldown <= 0.0f
		&& Movement->IsMovingOnGround() && Movement->Velocity.Size2D() >= SlideMinSpeed
		&& !bMantling && !bGliding && CurrentVehicle == nullptr && ActiveEmoteMontage == nullptr;
}

bool UFortnitePortingCharacterComponent::TryStartSlide()
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!CanSlide() || Movement == nullptr)
	{
		return false;
	}

	SlideDirection = Movement->Velocity.GetSafeNormal2D();
	SlideSpeed = FMath::Min(static_cast<float>(Movement->Velocity.Size2D()) + SlideBoost, SlideMaxSpeed);
	SlideElapsed = 0.0f;

	// The slide drives the velocity itself: no input acceleration, friction or braking from the movement component
	SavedGroundFriction = Movement->GroundFriction;
	SavedBrakingFriction = Movement->BrakingFriction;
	SavedBrakingDecelerationWalking = Movement->BrakingDecelerationWalking;
	SavedMaxAcceleration = Movement->MaxAcceleration;
	SavedMaxWalkSpeedCrouched = Movement->MaxWalkSpeedCrouched;
	Movement->GroundFriction = 0.0f;
	Movement->BrakingFriction = 0.0f;
	Movement->BrakingDecelerationWalking = 0.0f;
	Movement->MaxAcceleration = 0.0f;
	Movement->MaxWalkSpeedCrouched = SlideMaxSpeed;

	Character->Crouch();
	bSliding = true;

	ActiveSlideMontage = SlideMontage;
	PlayMontage(ActiveSlideMontage);
	BroadcastSlide(true);
	return true;
}

void UFortnitePortingCharacterComponent::StopSlide()
{
	if (!bSliding)
	{
		return;
	}

	bSliding = false;
	SlideCooldown = 0.3f;

	const ACharacter* Character = GetCharacter();
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		Movement->GroundFriction = SavedGroundFriction;
		Movement->BrakingFriction = SavedBrakingFriction;
		Movement->BrakingDecelerationWalking = SavedBrakingDecelerationWalking;
		Movement->MaxAcceleration = SavedMaxAcceleration;
		Movement->MaxWalkSpeedCrouched = SavedMaxWalkSpeedCrouched;
	}

	StopMontage(ActiveSlideMontage, 0.25f);
	ActiveSlideMontage = nullptr;
	BroadcastSlide(false);
}

void UFortnitePortingCharacterComponent::TickSlide(float DeltaTime)
{
	// Machines that do not own the pawn only mirror the slide visuals (SetSlideVisual); the position comes from the owner
	if (!bSliding || !DrivesMovement())
	{
		return;
	}

	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;

	// Crouch released, slid off a ledge, jumped, mantled...: the slide ends and the momentum stays
	if (Movement == nullptr || !Movement->bWantsToCrouch || !Movement->IsMovingOnGround() || bMantling)
	{
		StopSlide();
		return;
	}

	SlideElapsed += DeltaTime;

	// Gravity along the floor: downhill speeds the slide up, uphill slows it down
	const FVector FloorNormal = Movement->CurrentFloor.IsWalkableFloor() ? FVector(Movement->CurrentFloor.HitResult.ImpactNormal) : FVector::UpVector;
	const FVector Gravity(0.0f, 0.0f, Movement->GetGravityZ());
	const FVector Downhill = Gravity - FloorNormal * FVector::DotProduct(Gravity, FloorNormal);
	const float SlopeAcceleration = static_cast<float>(FVector::DotProduct(Downhill, SlideDirection)) * SlideSlopeGravity;

	// Steering towards the input; pulling back brakes harder
	FVector Input = Character->GetPendingMovementInputVector().GetSafeNormal2D();
	if (Input.IsNearlyZero())
	{
		Input = Character->GetLastMovementInputVector().GetSafeNormal2D();
	}
	float Braking = SlideDeceleration;
	if (!Input.IsNearlyZero())
	{
		const float Dot = static_cast<float>(FVector::DotProduct(Input, SlideDirection));
		if (Dot > -0.3f)
		{
			SlideDirection = FMath::VInterpNormalRotationTo(SlideDirection, Input, DeltaTime, SlideTurnRate).GetSafeNormal2D();
		}
		else
		{
			Braking *= 2.0f;
		}
	}

	SlideSpeed = FMath::Clamp(SlideSpeed + (SlopeAcceleration - Braking) * DeltaTime, 0.0f, SlideMaxSpeed);
	Movement->Velocity = SlideDirection * SlideSpeed;

	// Orient-to-movement follows acceleration, which is zero while sliding: face the slide direction by hand
	const FRotator Target = SlideDirection.Rotation();
	Character->SetActorRotation(FMath::RInterpTo(Character->GetActorRotation(), FRotator(0.0, Target.Yaw, 0.0), DeltaTime, 10.0f));

	if (SlideSpeed <= SlideEndSpeed && SlideElapsed > 0.25f)
	{
		StopSlide();
	}
}

void UFortnitePortingCharacterComponent::EquipPickaxe()
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (bPickaxeEquipped || Pickaxe == nullptr || Body == nullptr || CurrentVehicle != nullptr)
	{
		return;
	}

	UnequipWeapon();

	for (const FFortnitePortingAttachedMesh& Entry : Pickaxe->Meshes)
	{
		UMeshComponent* Component = nullptr;
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Entry.Mesh))
		{
			USkeletalMeshComponent* SkeletalComponent = NewObject<USkeletalMeshComponent>(GetOwner());
			SkeletalComponent->SetSkeletalMeshAsset(SkeletalMesh);
			Component = SkeletalComponent;
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Entry.Mesh))
		{
			UStaticMeshComponent* StaticComponent = NewObject<UStaticMeshComponent>(GetOwner());
			StaticComponent->SetStaticMesh(StaticMesh);
			Component = StaticComponent;
		}

		if (Component == nullptr)
		{
			continue;
		}

		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();
		FName DriverBone;
		const FName Socket = ResolveHandAttachment(Entry.Socket, TEXT("Pickaxe"), DriverBone);
		Component->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		Component->SetRelativeTransform(ComputeAttachTransform(Entry.Offset, PickaxeGripAdjustment, DriverBone, Entry.Mesh));
		PickaxeComponents.Add(Component);
		PickaxeDriverBones.Add(DriverBone);
	}

	bPickaxeEquipped = true;
	SetPickaxeVisible(!bGliding && ActiveEmoteMontage == nullptr);
	UAnimMontage* Equip = Pickaxe->GetEquipMontage(SkeletonProfile);
	if (IsUsableMontage(Equip))
	{
		UseUpperBodySlot(Equip);
		PlayMontage(Equip);
	}
}

void UFortnitePortingCharacterComponent::UnequipPickaxe()
{
	for (UMeshComponent* Component : PickaxeComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}

	PickaxeComponents.Reset();
	PickaxeDriverBones.Reset();
	bPickaxeEquipped = false;
	bSwingQueued = false;
	SwingComboTimer = 0.0f;

	if (ActivePickaxeHold)
	{
		StopMontage(ActivePickaxeHold, 0.2f);
		ActivePickaxeHold = nullptr;
	}
}

void UFortnitePortingCharacterComponent::TogglePickaxe()
{
	if (bPickaxeEquipped)
	{
		UnequipPickaxe();
	}
	else
	{
		EquipPickaxe();
	}
}

void UFortnitePortingCharacterComponent::SwingPickaxe()
{
	if (!bPickaxeEquipped || Pickaxe == nullptr || bGliding || bMantling)
	{
		return;
	}

	UAnimMontage* Swing = Pickaxe->GetSwingMontage(SkeletonProfile);
	const UAnimInstance* AnimInstance = GetAnimInstance();
	if (!IsUsableMontage(Swing) || AnimInstance == nullptr)
	{
		return;
	}

	// A click during a swing chains the next one at the chain point (spamming clicks = holding the button)
	if (AnimInstance->Montage_IsPlaying(Swing))
	{
		TimeSinceSwingPress = 0.0f;
		bSwingQueued = true;
		return;
	}

	// Section 0 is the single swing; the following ones (left arm swing...) continue a combo still in its window
	StartSwingSection(Swing, SwingComboTimer > 0.0f ? NextSwingSection : 0);
}

void UFortnitePortingCharacterComponent::StartSwingSection(UAnimMontage* Swing, int32 Section)
{
	const int32 NumSections = FMath::Max(1, Swing->CompositeSections.Num());
	Section = Section % NumSections;
	NextSwingSection = (Section + 1) % NumSections;
	bSwingQueued = false;
	// The click that started this swing is consumed: only clicks during it chain another
	TimeSinceSwingPress = SwingInputBuffer + 1.0f;

	UseUpperBodySlot(Swing);
	PlayMontageSection(Swing, Section, SwingPlayRate);
	SwingComboTimer = Swing->GetSectionLength(Section) / FMath::Max(SwingPlayRate, 0.1f) + SwingComboWindow;

	// The server runs the combo; the other machines only replay each swing it starts
	if (IsNetworked() && HasNetAuthority())
	{
		Multicast_SwingFX(Section);
	}

	// the hit lands where the animation does: at the chain point of the section
	if (HasNetAuthority() && PickaxeDamage > 0.0f && GetWorld())
	{
		const float Delay = Swing->GetSectionLength(Section) / FMath::Max(SwingPlayRate, 0.1f) * SwingChainPoint;
		FTimerHandle Handle;
		GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]() { PickaxeHit(); }), FMath::Max(Delay, 0.05f), false);
	}
}

void UFortnitePortingCharacterComponent::PickaxeHit()
{
	ACharacter* Character = GetCharacter();
	if (Character == nullptr || !bPickaxeEquipped || bGameplayInputBlocked || GetWorld() == nullptr)
	{
		return;
	}
	const FVector Start = Character->GetActorLocation() + FVector(0.0f, 0.0f, 30.0f);
	const FVector Aim = Character->GetBaseAimRotation().Vector();
	const FVector End = Start + Aim * PickaxeReach;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingPickaxe), true, Character);
	FCollisionObjectQueryParams Targets;
	Targets.AddObjectTypesToQuery(ECC_WorldStatic);
	Targets.AddObjectTypesToQuery(ECC_WorldDynamic);
	Targets.AddObjectTypesToQuery(ECC_Pawn);
	Targets.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Targets, FCollisionShape::MakeSphere(45.0f), Params))
	{
		return;
	}
	if (AActor* HitActor = Hit.GetActor())
	{
		UGameplayStatics::ApplyPointDamage(HitActor, PickaxeDamage, Aim, Hit, Character->GetController(), Character, UDamageType::StaticClass());

		// the sound follows what was hit: built pieces and props name their material in a "Surface_X" tag, anything else counts as stone
		FName Surface = TEXT("Stone");
		for (const FName& Tag : HitActor->Tags)
		{
			if (Tag == TEXT("Surface_Wood")) { Surface = TEXT("Wood"); break; }
			if (Tag == TEXT("Surface_Metal")) { Surface = TEXT("Metal"); break; }
			if (Tag == TEXT("Surface_Stone")) { Surface = TEXT("Stone"); break; }
		}
		Multicast_PickaxeImpact(Surface, FVector_NetQuantize(Hit.ImpactPoint));
	}
}

void UFortnitePortingCharacterComponent::TickPickaxeCombo(float DeltaTime)
{
	SwingComboTimer = FMath::Max(0.0f, SwingComboTimer - DeltaTime);
	TimeSinceSwingPress += DeltaTime;
	if (bGameplayInputBlocked)
	{
		bSwingQueued = false;
		return;
	}
	if (!bPickaxeEquipped || Pickaxe == nullptr)
	{
		bSwingQueued = false;
		return;
	}

	UAnimMontage* Swing = Pickaxe->GetSwingMontage(SkeletonProfile);
	UAnimInstance* AnimInstance = GetAnimInstance();
	if (!IsUsableMontage(Swing) || AnimInstance == nullptr)
	{
		return;
	}

	// Holding the button, a queued click, or a click in the last SwingInputBuffer seconds all keep harvesting
	bool bHeld = false;
	if (bEnableDefaultControls && CurrentWeapon == nullptr && !bCombatBlocked)
	{
		const ACharacter* Character = GetCharacter();
		const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
		bHeld = Controller && Controller->IsLocalController() && Controller->IsInputKeyDown(SwingKey);
	}
	const bool bPlaying = AnimInstance->Montage_IsPlaying(Swing);
	const bool bWantsMore = bSwingQueued || TimeSinceSwingPress <= SwingInputBuffer || (bHeld && (bPlaying || SwingComboTimer > 0.0f));
	if (!bWantsMore || bGliding || bMantling)
	{
		return;
	}

	// Chain as soon as the current swing has landed its hit: the recovery is skipped, so swings flow into each other
	bool bCanChain = !bPlaying;
	if (bPlaying)
	{
		const float Position = AnimInstance->Montage_GetPosition(Swing);
		const int32 Current = Swing->GetSectionIndexFromPosition(Position);
		if (Current != INDEX_NONE)
		{
			float Start = 0.0f, End = 0.0f;
			Swing->GetSectionStartAndEndTime(Current, Start, End);
			bCanChain = Position >= Start + (End - Start) * SwingChainPoint;
		}
	}
	else if (SwingComboTimer <= 0.0f && !bHeld && !bSwingQueued)
	{
		// Combo over: the next click starts again from the single swing
		return;
	}

	if (bCanChain)
	{
		StartSwingSection(Swing, NextSwingSection);
	}
}

void UFortnitePortingCharacterComponent::PlayMontageSection(UAnimMontage* Montage, int32 SectionIndex, float PlayRate) const
{
	if (Montage == nullptr || !Montage->CompositeSections.IsValidIndex(SectionIndex))
	{
		PlayMontage(Montage, PlayRate);
		return;
	}

	PlayMontage(Montage, PlayRate);
	const FName Section = Montage->CompositeSections[SectionIndex].SectionName;
	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		AnimInstance->Montage_JumpToSection(Section, Montage);
	}
	if (UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr)
	{
		RigInstance->Montage_JumpToSection(Section, Montage);
	}
}

void UFortnitePortingCharacterComponent::SetHidePickaxe(bool bHide)
{
	bHidePickaxe = bHide;
	SetPickaxeVisible(!bHide);
}

void UFortnitePortingCharacterComponent::SetPickaxeVisible(bool bVisible)
{
	bVisible = bVisible && !bHidePickaxe;

	// Also hides the held weapon: every caller means "hands are busy" (emote, glider)
	for (UMeshComponent* Component : PickaxeComponents)
	{
		if (IsValid(Component))
		{
			Component->SetVisibility(bVisible, true);
		}
	}

	for (UMeshComponent* Component : OffhandComponents)
	{
		if (IsValid(Component))
		{
			Component->SetVisibility(bVisible, true);
		}
	}

	if (IsValid(WeaponActor))
	{
		WeaponActor->SetActorHiddenInGame(!bVisible);
	}
}

bool UFortnitePortingCharacterComponent::PlayEmote(UFortnitePortingEmoteData* Emote)
{
	UE_LOG(LogFortnitePortingRuntime, Verbose, TEXT("PlayEmote %s on %s (authority=%d, already emoting=%d)"),
		Emote ? *Emote->GetName() : TEXT("null"), GetOwner() ? *GetOwner()->GetName() : TEXT("?"), HasNetAuthority() ? 1 : 0, IsEmoting() ? 1 : 0);
	const ACharacter* Character = GetCharacter();
	if (Emote == nullptr || Character == nullptr || bGliding || bMantling || Character->GetCharacterMovement()->IsFalling())
	{
		return false;
	}

	UAnimMontage* Montage = Emote->GetMontage(SkeletonProfile);
	if (Montage == nullptr)
	{
		UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("Emote %s has no montage for profile %s"), *Emote->GetName(), *SkeletonProfile.ToString());
		return false;
	}

	if (PlayMontage(Montage) <= 0.0f)
	{
		return false;
	}

	StopEmoteSounds();
	ActiveEmoteMontage = Montage;
	ActiveEmoteData = Emote;
	EmoteElapsed = 0.0f;
	NextEmoteSound = 0;
	++EmotePlayCount;
	SetupEmoteLoop(Montage);

	// Emotes with music go through the mixer: your own always plays, other players' are ducked or faded
	if (!Emote->Sounds.IsEmpty())
	{
		EmoteMusicGain = EmoteMusicTarget = IsEmoteLocal() ? 1.0f : 0.0f;
		EmoteMusicFadeRate = 0.0f;
		if (UWorld* World = GetWorld())
		{
			if (UFortnitePortingEmoteMixer* Mixer = World->GetSubsystem<UFortnitePortingEmoteMixer>())
			{
				Mixer->Register(this);
			}
		}
	}
	SetPickaxeVisible(false);
	return true;
}

bool UFortnitePortingCharacterComponent::PlayOrQueueEmote(UFortnitePortingEmoteData* Emote)
{
	const ACharacter* Character = GetCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Emote == nullptr || Character == nullptr || Movement == nullptr)
	{
		return false;
	}
	if (bGliding || bMantling || Movement->IsFalling())
	{
		PendingEmote = Emote;
		PendingEmoteElapsed = 0.0f;
		return true;
	}
	PendingEmote = nullptr;
	PendingEmoteElapsed = 0.0f;
	return PlayEmote(Emote);
}

void UFortnitePortingCharacterComponent::StopEmote(float BlendOutTime)
{
	if (ActiveEmoteMontage == nullptr)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		StopMontage(ActiveEmoteMontage, BlendOutTime);
	}

	ActiveEmoteMontage = nullptr;
	StopEmoteSounds();
	SetPickaxeVisible(!bGliding);
}

void UFortnitePortingCharacterComponent::TickEmote()
{
	if (ActiveEmoteMontage == nullptr)
	{
		return;
	}

	const ACharacter* Character = GetCharacter();
	const UAnimInstance* AnimInstance = GetAnimInstance();
	const bool bMoved = Character && ((!bEmoteWhileMoving && Character->GetVelocity().Size2D() > 20.0f) || Character->GetCharacterMovement()->IsFalling());
	if (bMoved)
	{
		StopEmote();
	}
	else if (AnimInstance == nullptr || !AnimInstance->Montage_IsPlaying(ActiveEmoteMontage))
	{
		ActiveEmoteMontage = nullptr;
		StopEmoteSounds();
		SetPickaxeVisible(true);
	}
}

bool UFortnitePortingCharacterComponent::DeployGlider()
{
	ACharacter* Character = GetCharacter();
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (bGliding || Glider == nullptr || Character == nullptr || Body == nullptr || !Character->GetCharacterMovement()->IsFalling())
	{
		return false;
	}

	if (USkeletalMesh* GliderMesh = Cast<USkeletalMesh>(Glider->Glider.Mesh))
	{
		GliderComponent = NewObject<USkeletalMeshComponent>(GetOwner());
		GliderComponent->SetSkeletalMeshAsset(GliderMesh);
		GliderComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		GliderComponent->RegisterComponent();
		GliderComponent->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Glider->Glider.Socket);
		GliderComponent->SetRelativeTransform(Glider->Glider.Offset);
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	CachedAirControl = Movement->AirControl;
	Movement->AirControl = Glider->AirControl;

	StopEmote();
	SetPickaxeVisible(false);

	ActiveGlideMontage = Glider->GetGlideMontage(SkeletonProfile);
	PlayMontage(ActiveGlideMontage);

	bGliding = true;
	return true;
}

void UFortnitePortingCharacterComponent::StopGliding()
{
	if (!bGliding)
	{
		return;
	}

	bGliding = false;

	if (IsValid(GliderComponent))
	{
		GliderComponent->DestroyComponent();
	}
	GliderComponent = nullptr;

	if (const ACharacter* Character = GetCharacter())
	{
		Character->GetCharacterMovement()->AirControl = CachedAirControl;
	}

	if (UAnimInstance* AnimInstance = GetAnimInstance(); AnimInstance && ActiveGlideMontage)
	{
		StopMontage(ActiveGlideMontage, 0.2f);
	}
	ActiveGlideMontage = nullptr;

	SetPickaxeVisible(true);
}

void UFortnitePortingCharacterComponent::TickGlider()
{
	if (!bGliding)
	{
		return;
	}

	const ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (Movement == nullptr || !Movement->IsFalling() || Glider == nullptr)
	{
		StopGliding();
		return;
	}

	Movement->Velocity.Z = FMath::Max(Movement->Velocity.Z, -Glider->FallSpeed);
}

bool UFortnitePortingCharacterComponent::TryMantle()
{
	ACharacter* Character = GetCharacter();
	UWorld* World = GetWorld();
	if (bMantling || Character == nullptr || World == nullptr)
	{
		return false;
	}

	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Location = Character->GetActorLocation();
	const FVector Feet = Location - FVector(0.0f, 0.0f, HalfHeight);
	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingMantle), false, Character);

	// 1. There must be a wall in front of us
	FHitResult WallHit;
	const FVector WallStart = Feet + FVector(0.0f, 0.0f, MantleMinHeight);
	const FVector WallEnd = WallStart + Forward * (Radius + MantleReach);
	if (!World->SweepSingleByChannel(WallHit, WallStart, WallEnd, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(10.0f), Params)
		|| FMath::Abs(WallHit.ImpactNormal.Z) > 0.3f)
	{
		return false;
	}

	// 2. The wall must end in a walkable ledge within reach
	FHitResult LedgeHit;
	const FVector LedgeStart = FVector(WallHit.ImpactPoint.X, WallHit.ImpactPoint.Y, Feet.Z + MantleMaxHeight + 5.0f) + Forward * (Radius * 0.8f);
	const FVector LedgeEnd = FVector(LedgeStart.X, LedgeStart.Y, Feet.Z + MantleMinHeight);
	if (!World->LineTraceSingleByChannel(LedgeHit, LedgeStart, LedgeEnd, ECC_Visibility, Params)
		|| LedgeHit.bStartPenetrating || LedgeHit.ImpactNormal.Z < 0.7f)
	{
		return false;
	}

	// 3. The capsule must fit on top of the ledge
	const FVector Target = LedgeHit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 2.0f);
	if (World->OverlapBlockingTestByChannel(Target, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
	{
		return false;
	}

	StopGliding();
	StopEmote();

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	MantleEntrySpeed = Movement->Velocity.Size2D();
	bMantleBlendingOut = false;
	bMantleLanding = false;
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);

	MantleStart = Location;
	MantleTarget = Target;
	MantleElapsed = 0.0f;
	MantleActiveDuration = FMath::Max(0.2f, MantleDuration);
	bMantling = true;

	// Pick Fortnite's mantle for this ledge height (50 / 100 / 200 cm variants)
	const float LedgeHeight = LedgeHit.ImpactPoint.Z - Feet.Z;
	UAnimMontage* Montage = MantleMontage;
	if (LedgeHeight < MantleLowMaxHeight && MantleMontageLow != nullptr)
	{
		Montage = MantleMontageLow;
	}
	else if (LedgeHeight > MantleHighMinHeight && MantleMontageHigh != nullptr)
	{
		Montage = MantleMontageHigh;
	}
	Montage = Montage ? Montage : (MantleMontageLow ? MantleMontageLow.Get() : MantleMontageHigh.Get());
	ActiveMantleMontage = Montage;

	ActiveMantlePath = Montage == MantleMontageLow ? MantlePathLow : (Montage == MantleMontageHigh ? MantlePathHigh : MantlePath);

	// The capsule follows the Fortnite root path (hands stay on the ledge); the gap between where we
	// actually are and where the animation starts is warped away during the first part of the climb
	bMantleFollowsCurves = Montage != nullptr && ActiveMantlePath.IsValid();
	MantlePlayRate = 1.0f;
	if (bMantleFollowsCurves)
	{
		MantlePlayRate = FMath::Clamp(ActiveMantlePath.Length / FMath::Max(MantleMinDuration, 0.05f), 0.2f, 1.0f);
		MantleForward = Forward;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Delta = Location - Target;
		const FVector Local(FVector::DotProduct(Delta, Forward), FVector::DotProduct(Delta, Right), Delta.Z);
		const FVector PathStart = ActiveMantlePath.Points[0];
		MantleCorrection = Local - FVector(PathStart.X, 0.0f, PathStart.Z);
		MantleActiveDuration = ActiveMantlePath.Length / MantlePlayRate;
	}

	PlayMontage(Montage, MantlePlayRate);
	BroadcastMantle(true, Montage, MantlePlayRate);
	return true;
}

void UFortnitePortingCharacterComponent::TickMantle(float DeltaTime)
{
	// Machines that do not own the pawn only mirror the mantle visuals (SetMantleVisual)
	if (!DrivesMovement())
	{
		return;
	}

	if (!bMantling)
	{
		// Landing of a sprint mantle: only while still sprinting forward, and briefly, then back to locomotion
		if (bMantleLanding)
		{
			MantleLandTime += DeltaTime;
			const ACharacter* LandingCharacter = GetCharacter();
			const UCharacterMovementComponent* LandingMovement = LandingCharacter ? LandingCharacter->GetCharacterMovement() : nullptr;
			const UAnimInstance* AnimInstance = GetAnimInstance();
			const bool bPushing = LandingMovement && !LandingMovement->GetCurrentAcceleration().IsNearlyZero() && !LandingMovement->IsFalling();
			const bool bPlaying = AnimInstance && ActiveMantleMontage && AnimInstance->Montage_IsPlaying(ActiveMantleMontage);
			if (!bPlaying || !bPushing || !bSprinting || MantleLandTime >= MantleLandMaxTime)
			{
				if (bPlaying)
				{
					StopMontage(ActiveMantleMontage, MantleBlendOutTime);
				}
				bMantleLanding = false;
			}
		}
		return;
	}

	ACharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		bMantling = false;
		return;
	}

	MantleElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(MantleElapsed / MantleActiveDuration, 0.0f, 1.0f);

	// Rise first, then move over the ledge
	float VerticalAlpha = FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(Alpha / 0.6f, 0.0f, 1.0f), 2.0f);
	float ForwardAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, FMath::Clamp((Alpha - 0.35f) / 0.65f, 0.0f, 1.0f), 2.0f);

	FVector NewLocation = FMath::Lerp(MantleStart, MantleTarget, ForwardAlpha);
	NewLocation.Z = FMath::Lerp(MantleStart.Z, MantleTarget.Z, VerticalAlpha);

	// Follow Fortnite's own root path so the hands stay on the ledge and the feet land on top
	if (bMantleFollowsCurves)
	{
		const FVector Path = ActiveMantlePath.Sample(Alpha * ActiveMantlePath.Length);
		const float Warp = 1.0f - FMath::InterpEaseOut(0.0f, 1.0f, FMath::Clamp(Alpha / 0.6f, 0.0f, 1.0f), 2.0f);
		const FVector Local = FVector(Path.X, 0.0f, Path.Z) + MantleCorrection * Warp;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, MantleForward);
		NewLocation = MantleTarget + MantleForward * Local.X + Right * Local.Y + FVector::UpVector * Local.Z;
	}
	Character->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);

	// Start blending back to the run/idle before the climb ends, so the last frames overlap instead of popping
	static const FName LandSection(TEXT("Land"));
	const bool bHasLanding = ActiveMantleMontage != nullptr && ActiveMantleMontage->GetSectionIndex(LandSection) != INDEX_NONE;
	if (!bHasLanding && !bMantleBlendingOut && Alpha >= MantleBlendOutStart && ActiveMantleMontage != nullptr)
	{
		bMantleBlendingOut = true;
		StopMontage(ActiveMantleMontage, MantleBlendOutTime);
	}

	if (Alpha >= 1.0f)
	{
		bMantling = false;
		bMantleFollowsCurves = false;
		bMantleBlendingOut = false;
		MantleCooldown = 0.3f;
		BroadcastMantle(false, ActiveMantleMontage, 1.0f);
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Walking);

		// Keep running over the ledge like Fortnite instead of stopping dead on top of it
		const FVector Forward = (MantleTarget - MantleStart).GetSafeNormal2D();
		if (!Movement->GetCurrentAcceleration().IsNearlyZero() && !Forward.IsNearlyZero())
		{
			const float ExitSpeed = FMath::Min(MantleEntrySpeed, Movement->MaxWalkSpeed);
			Movement->Velocity = Forward * ExitSpeed;
		}

		// Fortnite's landing (LandToSprint) plays at normal speed as the character runs on; standing still it
		// blends straight to idle instead
		if (bHasLanding)
		{
			// LandToSprint is authored at sprint pace: only sprinting keeps it, otherwise it would look like running
			// faster than the character moves
			if (!bSprinting || Movement->GetCurrentAcceleration().IsNearlyZero())
			{
				StopMontage(ActiveMantleMontage, MantleBlendOutTime);
			}
			else
			{
				SetMontagePlayRate(ActiveMantleMontage, 1.0f);
				bMantleLanding = true;
				MantleLandTime = 0.0f;
			}
		}
	}
}

bool UFortnitePortingCharacterComponent::EquipWeapon(UFortnitePortingWeaponData* Weapon)
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	UWorld* World = GetWorld();
	if (Weapon == nullptr || Body == nullptr || World == nullptr || CurrentVehicle != nullptr || Weapon->Meshes.IsEmpty())
	{
		return false;
	}

	UnequipWeapon();
	UnequipPickaxe();

	UClass* WeaponClass = Weapon->WeaponActorClass.Get();
	if (WeaponClass == nullptr || !WeaponClass->IsChildOf(AFortnitePortingWeapon::StaticClass()))
	{
		WeaponClass = AFortnitePortingWeapon::StaticClass();
	}

	const FTransform SpawnTransform = Body->GetComponentTransform();
	AFortnitePortingWeapon* Spawned = World->SpawnActorDeferred<AFortnitePortingWeapon>(WeaponClass, SpawnTransform, GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Spawned != nullptr)
	{
		// Held copies are local to each machine (see NetState): replicating them would duplicate the weapon on clients
		Spawned->SetReplicates(false);
		Spawned->WeaponData = Weapon;
		Spawned->bIsPickup = false;
		Spawned->FinishSpawning(SpawnTransform);
		const FName Socket = ResolveHandAttachment(Weapon->Meshes[0].Socket, TEXT("Weapon"), WeaponDriverBone);
		Spawned->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		Spawned->SetActorRelativeTransform(ComputeAttachTransform(Weapon->Meshes[0].Offset, WeaponGripAdjustment, WeaponDriverBone, Weapon->Meshes[0].Mesh));
		WeaponActor = Spawned;
	}

	for (int32 Index = 1; Index < Weapon->Meshes.Num(); ++Index)
	{
		const FFortnitePortingAttachedMesh& Entry = Weapon->Meshes[Index];
		UMeshComponent* Component = nullptr;
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Entry.Mesh))
		{
			USkeletalMeshComponent* SkeletalComponent = NewObject<USkeletalMeshComponent>(GetOwner());
			SkeletalComponent->SetSkeletalMeshAsset(SkeletalMesh);
			Component = SkeletalComponent;
		}
		else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Entry.Mesh))
		{
			UStaticMeshComponent* StaticComponent = NewObject<UStaticMeshComponent>(GetOwner());
			StaticComponent->SetStaticMesh(StaticMesh);
			Component = StaticComponent;
		}

		if (Component != nullptr)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->RegisterComponent();
			FName DriverBone;
			const FName Socket = ResolveHandAttachment(Entry.Socket, TEXT("Weapon (offhand)"), DriverBone);
			Component->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
			Component->SetRelativeTransform(ComputeAttachTransform(Entry.Offset, WeaponGripAdjustment, DriverBone, Entry.Mesh));
			OffhandComponents.Add(Component);
			OffhandDriverBones.Add(DriverBone);
		}
	}

	CurrentWeapon = Weapon;
	// The fit and the learned grip are found again on every equip: a fit saved while the hold pose was not playing (an older build did
	// that) turned the barrel sideways for good, on every launch
	WeaponFitRotation = FQuat::Identity;
	bWeaponFitKnown = false;
	WeaponFitPasses = 0;
	WeaponFitSettle = 0.0f;
	bLeftHandOnWeaponValid = false;
	bWeaponGripSaved = false;
	WeaponGripSettle = 0.0f;
	CurrentWeaponIndex = Weapons.IndexOfByKey(Weapon);
	AmmoInMagazine = Weapon->MagazineSize;
	FireCooldown = 0.25f;
	ReloadRemaining = 0.0f;

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("%s equipped on %s (profile %s): hold %s, jog %s, reload %s, equip %s, attached to %s%s"),
		*Weapon->GetName(), *GetOwner()->GetName(), *SkeletonProfile.ToString(),
		*GetNameSafe(Weapon->GetHoldMontage(SkeletonProfile)), *GetNameSafe(Weapon->GetJogMontage(SkeletonProfile)),
		*GetNameSafe(Weapon->GetReloadMontage(SkeletonProfile)), *GetNameSafe(Weapon->GetEquipMontage(SkeletonProfile)),
		Spawned ? *Spawned->GetAttachParentSocketName().ToString() : TEXT("(none)"), WeaponDriverBone.IsNone() ? TEXT("") : TEXT(" (driven by the weapon rig)"));

	ActiveWeaponMontage = ResolveWeaponMontage(Weapon->GetEquipMontage(SkeletonProfile), false);
	bActiveMontageIsEquip = ActiveWeaponMontage != nullptr;
	if (ActiveWeaponMontage == nullptr || PlayMontage(ActiveWeaponMontage) <= 0.0f)
	{
		ActiveWeaponMontage = nullptr;
		bActiveMontageIsEquip = false;
		PlayHoldMontage();
	}

	if (WeaponActor)
	{
		WeaponActor->PlayWeaponAnimation(Weapon->WeaponEquipAnimation);
	}

	SetPickaxeVisible(!bGliding && ActiveEmoteMontage == nullptr);
	return true;
}

void UFortnitePortingCharacterComponent::UnequipWeapon()
{
	if (CurrentWeapon == nullptr)
	{
		DestroyWeaponVisuals();
		return;
	}

	if (GetAnimInstance() != nullptr)
	{
		StopWeaponPoseMontages(0.2f);
		for (UAnimMontage* Montage : { CurrentWeapon->GetHoldMontage(SkeletonProfile), CurrentWeapon->GetEquipMontage(SkeletonProfile), ActiveWeaponMontage.Get() })
		{
			if (Montage != nullptr)
			{
				StopMontage(Montage, 0.2f);
			}
		}
	}

	DestroyWeaponVisuals();
	CurrentWeapon = nullptr;
	ActiveWeaponMontage = nullptr;
	bActiveMontageIsEquip = false;
	ReloadRemaining = 0.0f;
}

void UFortnitePortingCharacterComponent::DestroyWeaponVisuals()
{
	if (IsValid(WeaponActor))
	{
		WeaponActor->Destroy();
	}
	WeaponActor = nullptr;

	for (UMeshComponent* Component : OffhandComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}
	OffhandComponents.Reset();
	OffhandDriverBones.Reset();
	WeaponDriverBone = NAME_None;
}

void UFortnitePortingCharacterComponent::CycleWeapon()
{
	if (Weapons.IsEmpty())
	{
		return;
	}

	int32 Next = CurrentWeapon ? CurrentWeaponIndex + 1 : 0;
	while (Weapons.IsValidIndex(Next) && Weapons[Next] == nullptr)
	{
		++Next;
	}

	if (!Weapons.IsValidIndex(Next))
	{
		UnequipWeapon();
		CurrentWeaponIndex = INDEX_NONE;
		return;
	}

	EquipWeapon(Weapons[Next]);
}

void UFortnitePortingCharacterComponent::PlayHoldMontage()
{
	UAnimMontage* Hold = CurrentWeapon ? ResolveWeaponMontage(CurrentWeapon->GetHoldMontage(SkeletonProfile), true) : nullptr;
	if (Hold != nullptr && PlayMontage(Hold) > 0.0f)
	{
		ActiveHoldMontage = Hold;
	}
}

FName UFortnitePortingCharacterComponent::WeaponSlotName() const
{
	static const FName UpperBodySlot(TEXT("UpperBody"));
	static const FName DefaultSlot(TEXT("DefaultSlot"));
	return bUpperBodySlotBroken ? DefaultSlot : UpperBodySlot;
}

UAnimMontage* UFortnitePortingCharacterComponent::ResolveWeaponMontage(UAnimMontage* Source, bool bLoop)
{
	if (!IsUsableMontage(Source))
	{
		return nullptr;
	}

	if (!bLoop && !bUpperBodySlotBroken)
	{
		// Fire, reload and draw animations play as imported, on the UpperBody slot
		return Source;
	}

	// A hold pose is two frames long (0.034 s): as an asset it ended before its blend in was over, and whether it looped depended on the
	// importer version. A transient copy that loops for hours plays like any other montage and never has to be frozen or restarted.
	const FName Slot = WeaponSlotName();
	const int32 LoopCount = bLoop ? 1000000 : 1;
	if (const TObjectPtr<UAnimMontage>* Found = WeaponMontageCopies.Find(Source))
	{
		UAnimMontage* Copy = Found->Get();
		if (Copy && Copy->SlotAnimTracks.Num() > 0 && Copy->SlotAnimTracks[0].SlotName == Slot
			&& Copy->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() > 0 && Copy->SlotAnimTracks[0].AnimTrack.AnimSegments[0].LoopingCount == LoopCount)
		{
			return Copy;
		}
	}

	if (Source->SlotAnimTracks.IsEmpty() || Source->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty())
	{
		return nullptr;
	}
	UAnimSequenceBase* Sequence = Source->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference();
	if (Sequence == nullptr)
	{
		return nullptr;
	}

	const float BlendIn = FMath::Max(Source->BlendIn.GetBlendTime(), 0.15f);
	const float BlendOut = FMath::Max(Source->BlendOut.GetBlendTime(), 0.15f);
	UAnimMontage* Copy = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, Slot, BlendIn, BlendOut, 1.0f, LoopCount);
	if (Copy == nullptr)
	{
		return Source;
	}
	WeaponMontageCopies.Add(Source, Copy);
	return Copy;
}

void UFortnitePortingCharacterComponent::StopWeaponPoseMontages(float BlendOutTime)
{
	if (ActiveHoldMontage != nullptr)
	{
		StopMontage(ActiveHoldMontage, BlendOutTime);
		ActiveHoldMontage = nullptr;
	}
	if (ActiveJogMontage != nullptr)
	{
		StopMontage(ActiveJogMontage, BlendOutTime);
		ActiveJogMontage = nullptr;
	}
}

void UFortnitePortingCharacterComponent::CheckUpperBodySlot(float DeltaTime, UAnimInstance* AnimInstance)
{
	// The generated Anim Blueprint feeds the UpperBody slot into a layered blend from spine_01. If that part of the graph is missing or
	// not wired (a hand edited or half upgraded blueprint) the hold pose plays but never reaches the mesh: the arms keep the unarmed
	// locomotion while the weapon hangs from an unanimated weapon_r. Once that is proven, every weapon montage goes full body instead.
	if (bUpperBodySlotChecked || AnimInstance == nullptr)
	{
		return;
	}

	static const FName UpperBodySlot(TEXT("UpperBody"));
	static const FName DefaultSlot(TEXT("DefaultSlot"));
	UAnimMontage* Pose = ActiveHoldMontage ? ActiveHoldMontage.Get() : ActiveJogMontage.Get();
	// A full body montage in DefaultSlot (emote, mantle, slide) legitimately stops updating everything above it: not a verdict
	const bool bMeasurable = Pose != nullptr && AnimInstance->Montage_IsPlaying(Pose) && AnimInstance->GetSlotMontageGlobalWeight(UpperBodySlot) >= 0.9f
		&& AnimInstance->GetSlotMontageGlobalWeight(DefaultSlot) < 0.01f && ActiveEmoteMontage == nullptr && !bMantling && !bSliding && !bGliding
		&& CurrentVehicle == nullptr && !bLobbyIdle;
	if (!bMeasurable)
	{
		UpperBodySlotSettled = 0.0f;
		return;
	}

	UpperBodySlotSettled += DeltaTime;
	if (UpperBodySlotSettled < 0.4f)
	{
		return;
	}

	bUpperBodySlotChecked = true;
	if (AnimInstance->GetSlotNodeGlobalWeight(UpperBodySlot) > 0.01f)
	{
		return;
	}

	bUpperBodySlotBroken = true;
	UE_LOG(LogFortnitePortingRuntime, Error,
		TEXT("%s: %s plays the weapon pose on the UpperBody slot, but that slot never reaches the final pose. Weapon animations go full body for now; "
			"run FP.UpgradeAnimBlueprints in the editor (or send the skin again) to rebuild the upper body layer of the Anim Blueprint."),
		*GetOwner()->GetName(), *GetNameSafe(AnimInstance->GetClass()));
	StopWeaponPoseMontages(0.1f);
	WeaponMontageCopies.Empty();
}

bool UFortnitePortingCharacterComponent::FireWeapon()
{
	// Local entry point: the owner aims (the camera only exists there), the server fires
	ACharacter* Character = GetCharacter();
	if (CurrentWeapon == nullptr || Character == nullptr)
	{
		return false;
	}

	FVector ViewLocation = Character->GetPawnViewLocation();
	FRotator ViewRotation = Character->GetActorRotation();
	if (AController* Controller = Character->GetController())
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}

	if (HasNetAuthority())
	{
		return FireWeaponAuthoritative(ViewLocation, ViewRotation.Vector());
	}

	// Client: cheap local checks so a held button does not flood the server, which validates everything again
	if (FireCooldown > 0.0f || ReloadRemaining > 0.0f || bGliding || bMantling || CurrentVehicle != nullptr)
	{
		return false;
	}

	if (CurrentWeapon->MagazineSize > 0 && AmmoInMagazine <= 0)
	{
		RequestReload();
		return false;
	}

	FireCooldown = CurrentWeapon->FireInterval;
	if (bFaceCameraWhenFiring)
	{
		Character->SetActorRotation(FRotator(0.0f, ViewRotation.Yaw, 0.0f));
	}
	Server_Fire(ViewLocation, ViewRotation.Vector());
	return true;
}

bool UFortnitePortingCharacterComponent::FireWeaponAuthoritative(FVector Origin, FVector Direction)
{
	ACharacter* Character = GetCharacter();
	const bool bRemoteShot = !DrivesMovement();
	// A remote client's shots arrive a little apart: tolerate jitter in its cooldown instead of dropping them
	const float CooldownTolerance = bRemoteShot ? 0.05f : 0.0f;
	if (CurrentWeapon == nullptr || Character == nullptr || FireCooldown > CooldownTolerance || ReloadRemaining > 0.0f || bGliding || bMantling || CurrentVehicle != nullptr)
	{
		return false;
	}

	if (CurrentWeapon->MagazineSize > 0 && AmmoInMagazine <= 0)
	{
		ReloadAuthoritative();
		return false;
	}

	// Never trust the client: keep the shot near the character and the direction valid
	if (FVector::DistSquared(Origin, Character->GetActorLocation()) > FMath::Square(800.0f))
	{
		Origin = Character->GetPawnViewLocation();
	}
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		Direction = Character->GetActorForwardVector();
	}

	FireCooldown = CurrentWeapon->FireInterval;
	if (CurrentWeapon->MagazineSize > 0)
	{
		AmmoInMagazine--;
	}

	// Where the shot lands: guns trace from the camera so shots go where the player aims, short range (melee) from the character
	const FVector Start = CurrentWeapon->Range < 1000.0f ? Character->GetPawnViewLocation() : Origin;
	const FVector End = Start + Direction * CurrentWeapon->Range;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingFire), true, Character);
	if (WeaponActor)
	{
		Params.AddIgnoredActor(WeaponActor);
	}
	// players (the capsule), walls built by players and the level all count as targets
	FCollisionObjectQueryParams Targets;
	Targets.AddObjectTypesToQuery(ECC_WorldStatic);
	Targets.AddObjectTypesToQuery(ECC_WorldDynamic);
	Targets.AddObjectTypesToQuery(ECC_Pawn);
	Targets.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, Targets, Params);
	const FVector ShotEnd = bHit ? FVector(Hit.ImpactPoint) : End;

	FireFX(Direction, ShotEnd, bHit);
	if (IsNetworked())
	{
		Multicast_FireFX(Direction, ShotEnd, bHit);
	}

	if (CurrentWeapon->Damage > 0.0f && bHit)
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			UGameplayStatics::ApplyPointDamage(HitActor, CurrentWeapon->Damage, Direction, Hit, Character->GetController(), Character, UDamageType::StaticClass());
		}

		if (UPrimitiveComponent* HitComponent = Hit.GetComponent(); HitComponent && HitComponent->IsSimulatingPhysics())
		{
			HitComponent->AddImpulseAtLocation(Direction * CurrentWeapon->Damage * 800.0f, Hit.ImpactPoint);
		}
	}

	return true;
}

void UFortnitePortingCharacterComponent::FireFX(const FVector& Direction, const FVector& End, bool bHit)
{
	ACharacter* Character = GetCharacter();
	if (CurrentWeapon == nullptr || Character == nullptr)
	{
		return;
	}

	StopEmote();

	// Only the machine that owns the pawn turns it; everybody else receives the rotation through movement replication
	if (bFaceCameraWhenFiring && DrivesMovement())
	{
		Character->SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
	}

	if (UAnimMontage* Fire = CurrentWeapon->GetFireMontage(SkeletonProfile); IsUsableMontage(Fire) && PlayMontage(Fire) > 0.0f)
	{
		ActiveWeaponMontage = Fire;
		bActiveMontageIsEquip = false;
	}

	if (WeaponActor)
	{
		// the gun's own motion (the pump of a shotgun, the bolt of a sniper) is spread over the time between two shots instead of being
		// over in a blink, and the gun goes back to its idle pose afterwards
		float Rate = 1.0f;
		float Length = 0.0f;
		if (const UAnimSequence* FireAnimation = CurrentWeapon->WeaponFireAnimation)
		{
			Length = FireAnimation->GetPlayLength();
			Rate = FMath::Clamp(Length / FMath::Max(CurrentWeapon->FireInterval * 0.9f, 0.25f), 0.35f, 1.0f);
		}
		WeaponActor->PlayWeaponAnimation(CurrentWeapon->WeaponFireAnimation, false, Rate);
		if (Length > 0.0f && CurrentWeapon->WeaponIdleAnimation && GetWorld())
		{
			FTimerHandle Handle;
			GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(WeaponActor, [Weapon = WeaponActor.Get(), Data = CurrentWeapon.Get()]()
			{
				if (Weapon && Data && Data->WeaponIdleAnimation)
				{
					Weapon->PlayWeaponAnimation(Data->WeaponIdleAnimation, true);
				}
			}), Length / Rate + 0.05f, false);
		}
		// flash, light, sound, tracer and spark at the barrel: it is the exact same point the tracer leaves from
		FortnitePortingWeaponFX::PlayShot(WeaponActor, CurrentWeapon, End, bHit);
	}

	// the shot kicks the sight up, more with the heavy guns (only the player who fired feels it)
	if (APlayerController* Shooter = Cast<APlayerController>(Character->GetController()); Shooter && Shooter->IsLocalController())
	{
		const FName Type = CurrentWeapon->WeaponType;
		const float Kick = Type == TEXT("Shotgun") ? 3.5f : (Type == TEXT("Sniper") ? 4.5f : (Type == TEXT("Pistol") ? 1.2f : 0.6f));
		PendingKick += Kick * (1.0f - 0.5f * AimAlpha);      // applied smoothly over the next frames (see TickAim)
	}
}

bool UFortnitePortingCharacterComponent::ReloadWeapon()
{
	if (HasNetAuthority())
	{
		return ReloadAuthoritative();
	}

	Server_Reload();
	return true;
}

bool UFortnitePortingCharacterComponent::ReloadAuthoritative()
{
	if (CurrentWeapon == nullptr || CurrentWeapon->MagazineSize <= 0 || ReloadRemaining > 0.0f || AmmoInMagazine >= CurrentWeapon->MagazineSize || CurrentVehicle != nullptr)
	{
		return false;
	}

	const float Duration = ReloadFX();
	ReloadRemaining = FMath::Max(Duration, 0.1f);

	if (IsNetworked())
	{
		Multicast_ReloadFX();
	}
	return true;
}

float UFortnitePortingCharacterComponent::ReloadFX()
{
	float Duration = CurrentWeapon ? CurrentWeapon->ReloadTime : 0.0f;
	if (CurrentWeapon == nullptr)
	{
		return Duration;
	}

	if (UAnimMontage* Reload = ResolveWeaponMontage(CurrentWeapon->GetReloadMontage(SkeletonProfile), false))
	{
		const float Length = PlayMontage(Reload);
		if (Length > 0.0f)
		{
			ActiveWeaponMontage = Reload;
			bActiveMontageIsEquip = false;
			Duration = Length;
		}
	}

	if (WeaponActor)
	{
		WeaponActor->PlayWeaponAnimation(CurrentWeapon->WeaponReloadAnimation);
		FortnitePortingWeaponFX::PlayReload(WeaponActor, CurrentWeapon);
	}

	return Duration;
}

void UFortnitePortingCharacterComponent::PickupWeapon(AFortnitePortingWeapon* Pickup)
{
	// Pickups are replicated level actors: only the server hands them out (and destroys them for everybody)
	if (Pickup == nullptr || Pickup->WeaponData == nullptr || CurrentVehicle != nullptr || !HasNetAuthority())
	{
		return;
	}

	UFortnitePortingWeaponData* Weapon = Pickup->WeaponData;
	Weapons.AddUnique(Weapon);
	Pickup->Destroy();
	EquipWeapon(Weapon);
	OnInventoryChanged.Broadcast();
}

void UFortnitePortingCharacterComponent::TickWeapon(float DeltaTime)
{
	FireCooldown = FMath::Max(0.0f, FireCooldown - DeltaTime);
	if (ReloadRemaining > 0.0f)
	{
		ReloadRemaining -= DeltaTime;
		if (ReloadRemaining <= 0.0f)
		{
			ReloadRemaining = 0.0f;
			if (CurrentWeapon)
			{
				AmmoInMagazine = CurrentWeapon->MagazineSize;
			}
		}
	}

	// building or editing: the weapon and the pickaxe go out of the hands (the building animations own the arms)
	if (bCombatBlocked != bHeldHidden)
	{
		bHeldHidden = bCombatBlocked;
		if (WeaponActor)
		{
			WeaponActor->SetActorHiddenInGame(bHeldHidden);
		}
		SetPickaxeVisible(!bHeldHidden && !bGliding && ActiveEmoteMontage == nullptr);
		if (bHeldHidden && CurrentWeapon)
		{
			StopWeaponPoseMontages(0.12f);
			ActiveWeaponMontage = nullptr;
			bActiveMontageIsEquip = false;
		}
	}

	UAnimInstance* AnimInstance = GetAnimInstance();
	if (CurrentWeapon == nullptr || CurrentVehicle != nullptr || AnimInstance == nullptr || bCombatBlocked)
	{
		return;
	}

	// Fire/reload/equip share the UpperBody slot group with the hold pose, so it is restored after them
	if (ActiveWeaponMontage && !AnimInstance->Montage_IsPlaying(ActiveWeaponMontage))
	{
		ActiveWeaponMontage = nullptr;
		bActiveMontageIsEquip = false;
	}

	if (ActiveWeaponMontage != nullptr)
	{
		return;
	}

	CheckUpperBodySlot(DeltaTime, AnimInstance);

	// Running with the weapon up: the weapon's own jog plays on the upper body, so the arms move with the stride. The hold pose is one frame
	// and kept the arms stiff while the legs ran. Standing still, aiming or any fire / reload / equip brings the hold pose back.
	if (UAnimMontage* Jog = ResolveWeaponMontage(CurrentWeapon->GetJogMontage(SkeletonProfile), true))
	{
		const ACharacter* Runner = GetCharacter();
		const bool bRunning = Runner != nullptr && Runner->GetVelocity().Size2D() > 60.0f && !Runner->GetCharacterMovement()->IsFalling()
			&& !Runner->GetCharacterMovement()->IsCrouching() && GetAimAlpha() < 0.05f && !bAiming;
		if (bRunning)
		{
			if (!AnimInstance->Montage_IsPlaying(Jog))
			{
				// Same slot group as the hold pose: starting the jog blends the hold out
				if (PlayMontage(Jog) > 0.0f)
				{
					ActiveJogMontage = Jog;
					ActiveHoldMontage = nullptr;
				}
			}
			return;
		}
		if (AnimInstance->Montage_IsPlaying(Jog))
		{
			StopMontage(Jog, 0.2f);
			ActiveJogMontage = nullptr;
		}
	}

	// The aim pose shares the slot with the hold pose: while it is up the hold must not be started again, or the two cut each other off
	// every frame and the arms hang half way between them
	if (UAnimMontage* Aim = CurrentWeapon->GetAimMontage(SkeletonProfile); Aim != nullptr && AnimInstance->Montage_IsPlaying(Aim))
	{
		return;
	}

	if (UAnimMontage* Hold = ResolveWeaponMontage(CurrentWeapon->GetHoldMontage(SkeletonProfile), true))
	{
		if (!AnimInstance->Montage_IsPlaying(Hold))
		{
			PlayHoldMontage();
		}
	}
	else if (UAnimMontage* Equip = CurrentWeapon->GetEquipMontage(SkeletonProfile); IsUsableMontage(Equip) && !AnimInstance->Montage_IsActive(Equip))
	{
		// No usable hold pose (Fortnite's are additive layers): the end of the draw is the weapon-in-hands pose
		HoldLastFrame(Equip);
	}
}

bool UFortnitePortingCharacterComponent::IsUsableMontage(const UAnimMontage* Montage)
{
	if (Montage == nullptr)
	{
		return false;
	}

	static TMap<TWeakObjectPtr<const UAnimMontage>, bool> Cache;
	if (const bool* Cached = Cache.Find(Montage))
	{
		return *Cached;
	}

	bool bUsable = true;
	for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
	{
		if (Track.AnimTrack.AnimSegments.IsEmpty())
		{
			continue;
		}

		const UAnimSequence* Sequence = Cast<UAnimSequence>(Track.AnimTrack.AnimSegments[0].GetAnimReference());
		if (Sequence == nullptr)
		{
			continue;
		}

		// An animation made for the weapon's own skeleton (_W, _WEP, _Weapon) has no tracks for the body: played on it the arms
		// fall back to the reference pose, a T pose. Older imports of some weapons picked those as the fire animation.
		const FString SequenceName = Sequence->GetName();
		if (SequenceName.EndsWith(TEXT("_W")) || SequenceName.EndsWith(TEXT("_WEP")) || SequenceName.Contains(TEXT("_Weapon")))
		{
			bUsable = false;
			break;
		}

		if (Sequence->IsValidAdditive())
		{
			bUsable = true;      // a properly flagged additive layer (recoil, aim) plays on top of the pose below
		}
		else if (const USkeleton* Skeleton = Sequence->GetSkeleton())
		{
			const int32 Pelvis = Skeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("pelvis"));
			if (Pelvis != INDEX_NONE)
			{
				FTransform Pose;
				Sequence->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(Pelvis), FAnimExtractContext(0.0), false);
				bUsable = !Pose.GetScale3D().IsNearlyZero(0.01);
			}
		}
		break;
	}

	if (!bUsable)
	{
		UE_LOG(LogFortnitePortingRuntime, Warning, TEXT("%s is an additive pose (it would collapse the body): ignored. Send the item again to pick another animation."), *Montage->GetName());
	}

	Cache.Add(Montage, bUsable);
	return bUsable;
}

void UFortnitePortingCharacterComponent::HoldLastFrame(UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = GetAnimInstance();
	if (Montage == nullptr || AnimInstance == nullptr)
	{
		return;
	}

	// Stop just before the auto blend out region, otherwise the held montage would fade away. A pose a few hundredths of a second long has
	// no such region (it is shorter than its own blend out), so it is told not to blend out on its own: it is held until it is stopped.
	// This has to be set before it plays: a running instance has already taken the flag, faded out at once and was started again
	// every few frames, which moved the whole arm up and down.
	const float End = FMath::Max(0.0f, Montage->GetPlayLength() - Montage->BlendOut.GetBlendTime() - 0.05f);
	if (End <= 0.0f)
	{
		Montage->bEnableAutoBlendOut = false;
	}

	UseUpperBodySlot(Montage);
	if (PlayMontage(Montage) <= 0.0f)
	{
		return;
	}

	// The montage is held with a play rate of zero, never Montage_Pause: a paused montage stops blending in, so the pose stayed at weight
	// zero and the arms hung down with the weapon across the hips.
	for (UAnimInstance* Instance : { AnimInstance, IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr })
	{
		if (Instance != nullptr)
		{
			Instance->Montage_SetPosition(Montage, End);
			Instance->Montage_SetPlayRate(Montage, 0.0f);
		}
	}
}

bool UFortnitePortingCharacterComponent::PickaxeUsesOwnHold() const
{
	return bPickaxeEquipped && Pickaxe && !Pickaxe->IsTwoHanded() && IsUsableMontage(Pickaxe->GetHoldMontage(SkeletonProfile));
}

void UFortnitePortingCharacterComponent::TickPickaxeHold()
{
	if (bLobbyIdle)
	{
		UAnimInstance* LobbyInstance = GetAnimInstance();
		// A dance in the lobby has the stage: the idle comes back when the dance ends or is stopped
		if (ActiveEmoteMontage == nullptr && LobbyInstance && IsUsableMontage(LobbyIdleMontage) && !LobbyInstance->Montage_IsPlaying(LobbyIdleMontage))
		{
			PlayMontage(LobbyIdleMontage);
		}
		return;
	}

	UAnimInstance* AnimInstance = GetAnimInstance();
	const bool bWantsHold = AnimInstance && bPickaxeEquipped && Pickaxe && !Pickaxe->IsTwoHanded() && CurrentWeapon == nullptr && !bCombatBlocked
		&& !bGliding && !bMantling && ActiveEmoteMontage == nullptr && CurrentVehicle == nullptr;
	if (!bWantsHold)
	{
		if (ActivePickaxeHold)
		{
			StopMontage(ActivePickaxeHold, 0.2f);
			ActivePickaxeHold = nullptr;
		}
		return;
	}

	// Swings and the draw own the upper body while they play
	UAnimMontage* Swing = Pickaxe->GetSwingMontage(SkeletonProfile);
	UAnimMontage* Equip = Pickaxe->GetEquipMontage(SkeletonProfile);
	if ((Swing && AnimInstance->Montage_IsPlaying(Swing)) || (Equip && Equip != ActivePickaxeHold && AnimInstance->Montage_IsPlaying(Equip)))
	{
		return;
	}

	// One handed / dual pickaxes hold their own pose; the character's pickaxe pose is the two handed one
	UAnimMontage* Hold = Pickaxe->GetHoldMontage(SkeletonProfile);
	if (IsUsableMontage(Hold))
	{
		// The hold is taken again after building, a swing or a run, from whatever pose the arms were in: a soft blend in keeps that
		// from showing as a snap (the montage asset comes with 0.1 s)
		if (!FMath::IsNearlyEqual(Hold->BlendIn.GetBlendTime(), 0.17f, 0.01f))
		{
			Hold->BlendIn.SetBlendTime(0.17f);      // the time the upper body weight takes (6 per second)
		}
		// Like the two handed pose, the dual / one handed hold gives the visible arms back to the run animation while
		// running (it is a static idle pose, so walking with it looks stiff). The weapon rig keeps playing it: the
		// items copy weapon_r/weapon_l relative to the hand, so they stay gripped in the running hand.
		// The body drops the hold past 20 cm/s and takes it again under the same 20: the anim instance brings the upper body weight back
		// (PickaxeUpperAlpha) as soon as the speed is under 20, so a restart later (it used to be under 8) left the arms in the bare
		// base pose for a moment (a pickaxe pointing up) before the hold blended in.
		const UFortnitePortingAnimInstance* Locomotion = Cast<UFortnitePortingAnimInstance>(AnimInstance);
		const bool bGroundMoving = Locomotion && Locomotion->bIsOnGround && !Locomotion->bIsCrouching;
		const bool bBodyHolding = AnimInstance->Montage_IsPlaying(Hold);
		const bool bRunning = bGroundMoving && Locomotion->Speed > 20.0f;

		UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr;
		const bool bRigHolding = RigInstance && RigInstance->Montage_IsPlaying(Hold);
		// The hold is an idle a few seconds long with no loop of its own: left alone it ends, blends out (the arms drop to the
		// base pose for a moment, with the pickaxes pointing back) and starts over, which shows as a strange arm movement every
		// few seconds. Loop it by jumping back to the start before the blend out begins (the idle loops seamlessly).
		for (UAnimInstance* LoopInstance : { AnimInstance, RigInstance })
		{
			if (LoopInstance && LoopInstance->Montage_IsPlaying(Hold) && LoopInstance->Montage_GetPosition(Hold) >= Hold->GetPlayLength() - 0.3f)
			{
				LoopInstance->Montage_SetPosition(Hold, 0.0f);
			}
		}
		if (bRunning)
		{
			if (bBodyHolding)
			{
				AnimInstance->Montage_Stop(0.2f, Hold);
			}
			if (RigInstance && !bRigHolding)
			{
				// start at the phase the body was at: the pickaxes are copied from the rig, so a rig that is out of phase with the
				// hands shows as the pickaxes moving strangely against them
				const float Phase = bBodyHolding ? AnimInstance->Montage_GetPosition(Hold) : 0.0f;
				RigInstance->Montage_Play(Hold, 1.0f, EMontagePlayReturnType::MontageLength, Phase, false);
			}
		}
		else if (!bBodyHolding)
		{
			UseUpperBodySlot(Hold);
			if (bRigHolding)
			{
				// take the hold again in step with the rig, which kept playing it while running
				AnimInstance->Montage_Play(Hold, 1.0f, EMontagePlayReturnType::MontageLength, RigInstance->Montage_GetPosition(Hold), false);
			}
			else
			{
				PlayMontage(Hold);
			}
		}
		ActivePickaxeHold = Hold;
	}
	else if (IsUsableMontage(Equip) && (ActivePickaxeHold != Equip || !AnimInstance->Montage_IsActive(Equip)))
	{
		HoldLastFrame(Equip);
		ActivePickaxeHold = Equip;
	}
}

void UFortnitePortingCharacterComponent::SetMontageNextSection(UAnimMontage* Montage, FName Section, FName NextSection) const
{
	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		AnimInstance->Montage_SetNextSection(Section, NextSection, Montage);
	}
	// The weapon rig runs the same montages and must loop with the body to keep weapon_r in sync
	if (UAnimInstance* RigInstance = IsValid(WeaponRig) ? WeaponRig->GetAnimInstance() : nullptr)
	{
		RigInstance->Montage_SetNextSection(Section, NextSection, Montage);
	}
}

void UFortnitePortingCharacterComponent::SetupEmoteLoop(UAnimMontage* Montage)
{
	bEmoteLooping = false;
	EmoteLoopStart = 0.0f;
	LastEmotePosition = 0.0f;
	EmoteMasterSound = INDEX_NONE;

	if (Montage == nullptr || ActiveEmoteData == nullptr)
	{
		return;
	}

	// Emotes imported from now on carry this analysis; older ones are analysed here, the first time they play
	UFortnitePortingEmoteData* Data = ActiveEmoteData;
	if (!Data->bLoopAnalyzed)
	{
		Data->AnalyzeMusicLoop(Montage);
	}
	if (!Data->bLoopsWithMusic)
	{
		return;
	}

	const int32 SectionIndex = Montage->GetSectionIndex(Data->LoopSection);
	if (SectionIndex == INDEX_NONE)
	{
		return;
	}

	// The montage repeats the section (already linked when it was imported with the loop; linked now otherwise)
	if (Montage->CompositeSections[SectionIndex].NextSectionName != Data->LoopSection)
	{
		SetMontageNextSection(Montage, Data->LoopSection, Data->LoopSection);
	}

	float SectionStart = 0.0f;
	float SectionEnd = 0.0f;
	Montage->GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
	EmoteLoopStart = SectionStart;
	bEmoteLooping = SectionEnd > SectionStart;

	// The master track loops by itself and is never restarted; the dance adapts to it
	EmoteMasterSound = Data->MasterSound;
	if (Data->Sounds.IsValidIndex(EmoteMasterSound))
	{
		if (USoundWave* MasterWave = Cast<USoundWave>(Data->Sounds[EmoteMasterSound].Sound); MasterWave != nullptr && !MasterWave->bLooping)
		{
			MasterWave->bLooping = true;
			ForcedLoopWaves.Add(MasterWave);
		}
	}

	if (!FMath::IsNearlyEqual(Data->DancePlayRate, 1.0f, 0.002f))
	{
		SetMontagePlayRate(Montage, Data->DancePlayRate);
	}

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("Emote %s: loops section '%s' from %.2f s, master track %d, dance speed x%.3f"),
		*Data->GetName(), *Data->LoopSection.ToString(), SectionStart, EmoteMasterSound, Data->DancePlayRate);
}

void UFortnitePortingCharacterComponent::SpawnEmoteSound(int32 SoundIndex, float StartTime)
{
	if (ActiveEmoteData == nullptr || !ActiveEmoteData->Sounds.IsValidIndex(SoundIndex) || GetOwner() == nullptr)
	{
		return;
	}

	const FFortnitePortingEmoteSound& Entry = ActiveEmoteData->Sounds[SoundIndex];
	if (Entry.Sound == nullptr)
	{
		return;
	}

	// A muted song must keep its place: it plays on in silence and is faded in, in sync, when it becomes the one heard
	if (Entry.Sound->VirtualizationMode != EVirtualizationMode::PlayWhenSilent)
	{
		Entry.Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	}

	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(
		Entry.Sound, GetOwner()->GetRootComponent(), NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		false, EmoteMusicGain * EmoteMusicVolume, 1.0f, FMath::Max(StartTime, 0.0f));
	if (Audio != nullptr)
	{
		EmoteAudio.Add(Audio);
		EmoteAudioIndex.Add(SoundIndex);
	}

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("Emote sound %d (%s) started at %.2f s into the track, spawned=%d, loops by itself=%d"),
		SoundIndex, *Entry.Sound->GetName(), StartTime, Audio != nullptr ? 1 : 0, Entry.Sound->IsLooping() ? 1 : 0);
}

void UFortnitePortingCharacterComponent::RestartEmoteSoundsForLoop()
{
	if (ActiveEmoteData == nullptr)
	{
		return;
	}

	const TArray<FFortnitePortingEmoteSound>& Sounds = ActiveEmoteData->Sounds;
	UE_LOG(LogFortnitePortingRuntime, Verbose, TEXT("Emote loop lap: other sounds brought back to montage time %.2f s"), EmoteLoopStart);

	// Forget audio that already finished
	for (int32 Index = EmoteAudio.Num() - 1; Index >= 0; --Index)
	{
		if (!IsValid(EmoteAudio[Index]))
		{
			EmoteAudio.RemoveAt(Index);
			EmoteAudioIndex.RemoveAt(Index);
		}
	}

	auto StopSound = [this](int32 SoundIndex)
	{
		for (int32 Index = EmoteAudio.Num() - 1; Index >= 0; --Index)
		{
			if (EmoteAudioIndex[Index] == SoundIndex)
			{
				EmoteAudio[Index]->Stop();
				EmoteAudio.RemoveAt(Index);
				EmoteAudioIndex.RemoveAt(Index);
			}
		}
	};

	int32 FirstInsideLoop = INDEX_NONE;
	for (int32 SoundIndex = 0; SoundIndex < Sounds.Num(); ++SoundIndex)
	{
		const FFortnitePortingEmoteSound& Entry = Sounds[SoundIndex];
		if (Entry.Sound == nullptr || SoundIndex == EmoteMasterSound)
		{
			continue;
		}

		if (Entry.Time >= EmoteLoopStart - 0.01f)
		{
			// Starts inside the repeated section: it is started again, at its time, on every lap
			StopSound(SoundIndex);
			if (FirstInsideLoop == INDEX_NONE)
			{
				FirstInsideLoop = SoundIndex;
			}
			continue;
		}

		// Started before the repeated section (the music runs through the intro): put it where the loop begins.
		// A track that ends before the lap comes round (or never loops) is simply played again from that point.
		float Offset = EmoteLoopStart - Entry.Time;
		float Duration = Entry.Sound->GetDuration();
		if (const USoundWave* Wave = Cast<USoundWave>(Entry.Sound))
		{
			Duration = Wave->Duration;
		}
		if (Duration > KINDA_SMALL_NUMBER)
		{
			Offset = FMath::Fmod(Offset, Duration);
		}

		StopSound(SoundIndex);
		SpawnEmoteSound(SoundIndex, Offset);
	}

	if (FirstInsideLoop != INDEX_NONE)
	{
		NextEmoteSound = FMath::Min(NextEmoteSound, FirstInsideLoop);
	}
}

void UFortnitePortingCharacterComponent::TickEmoteSounds(float DeltaTime)
{
	if (ActiveEmoteData == nullptr || ActiveEmoteMontage == nullptr)
	{
		return;
	}

	// TickEmote ends the emote when the montage stops; until then the music follows the montage, not a stopwatch
	const UAnimInstance* AnimInstance = GetAnimInstance();
	if (AnimInstance == nullptr || !AnimInstance->Montage_IsPlaying(ActiveEmoteMontage))
	{
		return;
	}

	EmoteElapsed += DeltaTime;
	const float Position = AnimInstance->Montage_GetPosition(ActiveEmoteMontage);

	// The repeated section ran out and the montage jumped back to its start: bring the music back to that point too
	if (bEmoteLooping && Position + 0.05f < LastEmotePosition)
	{
		RestartEmoteSoundsForLoop();
	}
	LastEmotePosition = Position;

	const TArray<FFortnitePortingEmoteSound>& Sounds = ActiveEmoteData->Sounds;
	while (Sounds.IsValidIndex(NextEmoteSound) && Sounds[NextEmoteSound].Time <= Position)
	{
		const int32 SoundIndex = NextEmoteSound++;
		// A frame hitch can make a sound start a little late: skip into it by the same amount
		const float Late = Position - Sounds[SoundIndex].Time;
		SpawnEmoteSound(SoundIndex, Late > 0.05f ? Late : 0.0f);
	}
}

void UFortnitePortingCharacterComponent::SetEmoteMusicGain(float Gain, float FadeTime)
{
	Gain = FMath::Clamp(Gain, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(Gain, EmoteMusicTarget, 0.005f))
	{
		return;
	}

	// Only the real changes (a song comes in or goes out) are worth a line; the small glides while a dancer walks are not
	UE_LOG(LogFortnitePortingRuntime, Verbose, TEXT("Emote music of %s (%s): volume %.2f -> %.2f over %.2f s"),
		*GetOwner()->GetName(), IsEmoteLocal() ? TEXT("own") : TEXT("other player"), EmoteMusicTarget, Gain, FadeTime);

	EmoteMusicTarget = Gain;
	EmoteMusicFadeRate = FadeTime > KINDA_SMALL_NUMBER ? FMath::Abs(Gain - EmoteMusicGain) / FadeTime : 0.0f;
	if (EmoteMusicFadeRate <= 0.0f)
	{
		TickEmoteMusicFade(0.0f);
	}
}

void UFortnitePortingCharacterComponent::TickEmoteMusicFade(float DeltaTime)
{
	if (FMath::IsNearlyEqual(EmoteMusicGain, EmoteMusicTarget, 0.0001f))
	{
		return;
	}

	EmoteMusicGain = EmoteMusicFadeRate > 0.0f ? FMath::FInterpConstantTo(EmoteMusicGain, EmoteMusicTarget, DeltaTime, EmoteMusicFadeRate) : EmoteMusicTarget;

	// One volume factor for everything: the audio components were created with it as their base volume
	for (UAudioComponent* Audio : EmoteAudio)
	{
		if (IsValid(Audio))
		{
			Audio->SetVolumeMultiplier(EmoteMusicGain * EmoteMusicVolume);
		}
	}
}

void UFortnitePortingCharacterComponent::StopEmoteSounds()
{
	if (UWorld* World = GetWorld())
	{
		if (UFortnitePortingEmoteMixer* Mixer = World->GetSubsystem<UFortnitePortingEmoteMixer>())
		{
			Mixer->Unregister(this);
		}
	}

	// A smooth fade-out; the mixer brings the next song in over a longer fade at the same time, so there is no jump
	for (UAudioComponent* Audio : EmoteAudio)
	{
		if (IsValid(Audio))
		{
			Audio->FadeOut(0.8f, 0.0f);
		}
	}

	EmoteAudio.Reset();
	EmoteAudioIndex.Reset();
	EmoteMusicGain = EmoteMusicTarget = 1.0f;
	EmoteMusicFadeRate = 0.0f;
	for (const TWeakObjectPtr<USoundWave>& Wave : ForcedLoopWaves)
	{
		if (Wave.IsValid())
		{
			Wave->bLooping = false;
		}
	}
	ForcedLoopWaves.Reset();
	EmoteMasterSound = INDEX_NONE;
	ActiveEmoteData = nullptr;
	NextEmoteSound = 0;
	EmoteElapsed = 0.0f;
	bEmoteLooping = false;
	EmoteLoopStart = 0.0f;
	LastEmotePosition = 0.0f;
}

void UFortnitePortingCharacterComponent::PlayLobbyIdle()
{
	if (bLobbyIdle)
	{
		return;
	}

	bLobbyIdle = true;
	bPickaxeBeforeLobby = bPickaxeEquipped || bPickaxeBeforeLobby;
	UnequipWeapon();
	UnequipPickaxe();
	StopEmote();

	if (IsUsableMontage(LobbyIdleMontage))
	{
		PlayMontage(LobbyIdleMontage);
	}
}

void UFortnitePortingCharacterComponent::StopLobbyIdle()
{
	if (!bLobbyIdle)
	{
		return;
	}

	bLobbyIdle = false;
	if (LobbyIdleMontage)
	{
		StopMontage(LobbyIdleMontage, 0.25f);
	}

	if (bPickaxeBeforeLobby)
	{
		bPickaxeBeforeLobby = false;
		EquipPickaxe();
	}
}

void UFortnitePortingCharacterComponent::SetPickaxe(UFortnitePortingPickaxeData* NewPickaxe)
{
	if (NewPickaxe == Pickaxe)
	{
		return;
	}

	const bool bWasEquipped = bPickaxeEquipped;
	UnequipPickaxe();
	Pickaxe = NewPickaxe;
	if (bWasEquipped && CurrentWeapon == nullptr && !bLobbyIdle)
	{
		EquipPickaxe();
	}
}

void UFortnitePortingCharacterComponent::SetGlider(UFortnitePortingGliderData* NewGlider)
{
	if (NewGlider == Glider)
	{
		return;
	}

	StopGliding();
	Glider = NewGlider;
}

void UFortnitePortingCharacterComponent::RegisterProjectCosmetics()
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	auto Collect = [this, &Registry](UClass* Class, TFunctionRef<void(UObject*)> Add)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.PackagePaths.Add(FName(*CosmeticsRoot));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& AssetData : Assets)
		{
			if (UObject* Object = AssetData.GetAsset())
			{
				Add(Object);
			}
		}
	};

	Collect(UFortnitePortingWeaponData::StaticClass(), [this](UObject* Object) { Weapons.AddUnique(Cast<UFortnitePortingWeaponData>(Object)); });
	Collect(UFortnitePortingEmoteData::StaticClass(), [this](UObject* Object) { Emotes.AddUnique(Cast<UFortnitePortingEmoteData>(Object)); });
	Collect(UFortnitePortingPickaxeData::StaticClass(), [this](UObject* Object) { AvailablePickaxes.AddUnique(Cast<UFortnitePortingPickaxeData>(Object)); });
	Collect(UFortnitePortingGliderData::StaticClass(), [this](UObject* Object) { AvailableGliders.AddUnique(Cast<UFortnitePortingGliderData>(Object)); });

	// Older imports could register the same item twice (the weapon key then equipped it twice in a row)
	auto RemoveDuplicates = [](auto& Items)
	{
		TSet<const UObject*> Seen;
		Items.RemoveAll([&Seen](const auto& Item)
		{
			bool bAlreadySeen = false;
			Seen.Add(Item.Get(), &bAlreadySeen);
			return Item == nullptr || bAlreadySeen;
		});
	};
	RemoveDuplicates(Weapons);
	RemoveDuplicates(Emotes);
	if (Pickaxe != nullptr)
	{
		AvailablePickaxes.AddUnique(Pickaxe);
	}
	else if (AvailablePickaxes.Num() > 0 && HasNetAuthority())
	{
		// Replicated: clients get the server's choice instead of picking their own default
		Pickaxe = AvailablePickaxes[0];
	}

	if (Glider != nullptr)
	{
		AvailableGliders.AddUnique(Glider);
	}
	else if (AvailableGliders.Num() > 0 && HasNetAuthority())
	{
		Glider = AvailableGliders[0];
	}

	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("%s: %d weapons, %d emotes, %d pickaxes, %d gliders available"),
		*GetOwner()->GetName(), Weapons.Num(), Emotes.Num(), AvailablePickaxes.Num(), AvailableGliders.Num());
}

AFortnitePortingVehicle* UFortnitePortingCharacterComponent::FindEnterableVehicle() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr)
	{
		return nullptr;
	}

	AFortnitePortingVehicle* Best = nullptr;
	float BestDistance = VehicleSearchRadius;
	for (TActorIterator<AFortnitePortingVehicle> It(World); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Owner->GetActorLocation());
		if (Distance < BestDistance && It->CanEnter(Owner))
		{
			Best = *It;
			BestDistance = Distance;
		}
	}

	return Best;
}

bool UFortnitePortingCharacterComponent::EnterVehicle(AFortnitePortingVehicle* Vehicle)
{
	// With somebody already driving, the next one to get in sits behind
	return EnterVehicleAs(Vehicle, Vehicle != nullptr && Vehicle->GetDriver() != nullptr);
}

bool UFortnitePortingCharacterComponent::EnterVehicleAs(AFortnitePortingVehicle* Vehicle, bool bPassenger)
{
	ACharacter* Character = GetCharacter();
	if (Vehicle == nullptr || Character == nullptr || CurrentVehicle != nullptr || !Vehicle->CanEnter(Character))
	{
		return false;
	}

	StopEmote();
	StopGliding();
	UnequipPickaxe();
	UnequipWeapon();
	if (Character->bIsCrouched)
	{
		Character->UnCrouch();
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->DisableMovement();

	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	CachedCapsuleCollision = Capsule->GetCollisionEnabled();
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (!(bPassenger ? Vehicle->SetPassenger(Character) : Vehicle->SetDriver(Character)))
	{
		Capsule->SetCollisionEnabled(CachedCapsuleCollision);
		Movement->SetMovementMode(MOVE_Walking);
		return false;
	}

	CurrentVehicle = Vehicle;
	bIsPassenger = bPassenger;
	bExitingVehicle = false;

	const UFortnitePortingVehicleData* Data = Vehicle->VehicleData;
	ActiveVehicleMontage = (Data && !bPassenger) ? Data->GetEnterMontage(SkeletonProfile) : nullptr;
	if (ActiveVehicleMontage == nullptr || PlayMontage(ActiveVehicleMontage) <= 0.0f)
	{
		ActiveVehicleMontage = Data ? (bPassenger ? Data->GetPassengerMontage(SkeletonProfile) : Data->GetDriverMontage(SkeletonProfile)) : nullptr;
		if (ActiveVehicleMontage == nullptr && Data)
		{
			ActiveVehicleMontage = Data->GetDriverMontage(SkeletonProfile);
		}
		PlayMontage(ActiveVehicleMontage);
	}

	return true;
}

void UFortnitePortingCharacterComponent::ExitVehicle()
{
	if (CurrentVehicle == nullptr || bExitingVehicle)
	{
		return;
	}

	// Exit animations are authored from the seat, so they play before detaching
	const UFortnitePortingVehicleData* Data = CurrentVehicle->VehicleData;
	UAnimMontage* ExitMontage = Data ? Data->GetExitMontage(SkeletonProfile) : nullptr;
	if (ExitMontage && PlayMontage(ExitMontage) > 0.0f)
	{
		ActiveVehicleMontage = ExitMontage;
		bExitingVehicle = true;
		return;
	}

	FinishExitVehicle();
}

void UFortnitePortingCharacterComponent::FinishExitVehicle()
{
	ACharacter* Character = GetCharacter();
	AFortnitePortingVehicle* Vehicle = CurrentVehicle;
	CurrentVehicle = nullptr;
	bExitingVehicle = false;
	VehicleCooldown = 0.5f;
	if (Character == nullptr)
	{
		return;
	}

	FVector ExitLocation = Character->GetActorLocation();
	if (IsValid(Vehicle))
	{
		ExitLocation = bIsPassenger ? Vehicle->ReleasePassenger() : Vehicle->ReleaseDriver();
		Character->SetActorRotation(FRotator(0.0f, Vehicle->GetActorRotation().Yaw, 0.0f));
	}
	else
	{
		Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	Character->SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	Character->GetCapsuleComponent()->SetCollisionEnabled(CachedCapsuleCollision);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	if (UAnimInstance* AnimInstance = GetAnimInstance(); AnimInstance && ActiveVehicleMontage)
	{
		StopMontage(ActiveVehicleMontage, 0.2f);
	}
	ActiveVehicleMontage = nullptr;
}

void UFortnitePortingCharacterComponent::TickVehicle()
{
	if (CurrentVehicle == nullptr)
	{
		return;
	}

	if (!IsValid(CurrentVehicle))
	{
		FinishExitVehicle();
		return;
	}

	const UAnimInstance* AnimInstance = GetAnimInstance();
	const bool bMontageActive = AnimInstance && ActiveVehicleMontage && AnimInstance->Montage_IsPlaying(ActiveVehicleMontage);
	if (bExitingVehicle)
	{
		if (!bMontageActive)
		{
			FinishExitVehicle();
		}
		return;
	}

	// Enter animation finished (or the loop stopped): sit with the driver loop that matches how it is being driven
	const UFortnitePortingVehicleData* Data = CurrentVehicle->VehicleData;
	UAnimMontage* Wanted = nullptr;
	if (bIsPassenger)
	{
		// Holds on to the driver's waist while the driver accelerates
		Wanted = Data ? Data->GetPassengerDriveMontage(SkeletonProfile, CurrentVehicle->GetThrottleInput() > 0.1f && CurrentVehicle->GetSpeed() > 100.0f) : nullptr;
		if (Wanted == nullptr && Data)
		{
			Wanted = Data->GetDriverMontage(SkeletonProfile);
		}
	}
	else if (Data)
	{
		Wanted = Data->GetDrivingMontage(SkeletonProfile, CurrentVehicle->GetSteerInput(), CurrentVehicle->GetThrottleInput(), CurrentVehicle->IsBraking(), FMath::Abs(CurrentVehicle->GetSpeed()) > 150.0f);
	}
	const bool bEntering = bMontageActive && Data && ActiveVehicleMontage == Data->GetEnterMontage(SkeletonProfile);
	if (!bEntering && Wanted && (!bMontageActive || Wanted != ActiveVehicleMontage))
	{
		ActiveVehicleMontage = Wanted;
		PlayMontage(ActiveVehicleMontage);
	}
}

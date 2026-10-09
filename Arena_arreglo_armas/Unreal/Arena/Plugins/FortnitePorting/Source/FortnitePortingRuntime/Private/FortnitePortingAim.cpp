// Aiming down the sights: holding the aim button zooms the camera, brings it close to the shoulder and shows the sight. The sniper
// gets a real scope. Local to the player who owns the character.

#include "FortnitePortingCharacterComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingReticle.h"
#include "FortnitePortingWeapon.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/MeshComponent.h"

namespace
{
	/** Hides (for the owner only) every mesh of the character: the head and hair are separate meshes that would fill the scope */
	void SetBodyHiddenForOwner(ACharacter* Character, AActor* Weapon, bool bHidden)
	{
		TArray<UMeshComponent*> Meshes;
		Character->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			Mesh->SetOwnerNoSee(bHidden);
		}
		// the weapon in the hands is an actor of its own, attached to the body
		TArray<AActor*> Attached;
		Character->GetAttachedActors(Attached, true, true);
		if (Weapon)
		{
			Attached.AddUnique(Weapon);
		}
		for (AActor* Actor : Attached)
		{
			TArray<UMeshComponent*> ActorMeshes;
			Actor->GetComponents<UMeshComponent>(ActorMeshes);
			for (UMeshComponent* Mesh : ActorMeshes)
			{
				Mesh->SetHiddenInGame(bHidden);
			}
		}
	}

	float ZoomOf(const UFortnitePortingWeaponData* Weapon)
	{
		const FName Type = Weapon->WeaponType;
		if (Type == TEXT("Sniper")) { return 0.2f; }
		if (Type == TEXT("Rifle")) { return 0.65f; }
		if (Type == TEXT("SMG")) { return 0.72f; }
		if (Type == TEXT("Shotgun")) { return 0.75f; }
		return 0.72f;
	}
}

void UFortnitePortingCharacterComponent::ApplyAimMontage(bool bWantsAim)
{
	UAnimInstance* Anim = GetAnimInstance();
	UAnimMontage* AimMontage = (Anim && CurrentWeapon) ? CurrentWeapon->GetAimMontage(SkeletonProfile) : nullptr;
	if (AimMontage == nullptr || !IsUsableMontage(AimMontage))
	{
		return;
	}
	if (bWantsAim && !Anim->Montage_IsPlaying(AimMontage))
	{
		// The aim poses are a single frame (0.034 s), like the hold ones: played as they are they fade out at once and are started again
		// every frame. They are held on their frame instead.
		HoldLastFrame(AimMontage);
	}
	else if (!bWantsAim && Anim->Montage_IsPlaying(AimMontage))
	{
		StopMontage(AimMontage, 0.2f);
	}
}

void UFortnitePortingCharacterComponent::TickAim(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		// another player: only the aim pose, from what the server says
		if (Character && CurrentWeapon && CurrentWeapon->Range >= 1000.0f)
		{
			ApplyAimMontage(bAimingNet || (HasNetAuthority() && bAiming));
		}
		return;
	}

	if (!ReticleWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		ReticleWidget = SNew(SFortnitePortingReticle).Component(this);
		GEngine->GameViewport->AddViewportWidgetContent(ReticleWidget.ToSharedRef(), 35);
	}

	const bool bWants = bEnableAiming && CurrentWeapon != nullptr && CurrentWeapon->Range >= 1000.0f && !bCombatBlocked && !bGliding && !bMantling
		&& CurrentVehicle == nullptr && ReloadRemaining <= 0.0f && (bDebugForceAim || PC->IsInputKeyDown(AimKey));
	// the recoil of the shots is spread over a few frames: it never fights with the mouse or snaps the camera
	if (PendingKick > 0.001f)
	{
		const float Step = FMath::Min(PendingKick, FMath::Max(PendingKick * DeltaTime * 14.0f, 0.02f));
		PendingKick -= Step;
		FRotator View = PC->GetControlRotation();
		View.Pitch = FMath::ClampAngle(FRotator::NormalizeAxis(View.Pitch) + Step, -80.0f, 80.0f);
		PC->SetControlRotation(View);
	}

	bAiming = bWants;
	if (bWants != bAimSent)
	{
		bAimSent = bWants;
		if (HasNetAuthority())
		{
			bAimingNet = bWants;
		}
		else
		{
			Server_SetAiming(bWants);
		}
	}
	ApplyAimMontage(bWants);
	AimAlpha = FMath::FInterpTo(AimAlpha, bWants ? 1.0f : 0.0f, DeltaTime, 11.0f);

	UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
	USpringArmComponent* Boom = Character->FindComponentByClass<USpringArmComponent>();
	if (AimAlpha < 0.002f)
	{
		AimAlpha = 0.0f;
		if (bAimBaseSaved)
		{
			// give the camera back exactly as it was
			if (Camera) { Camera->SetFieldOfView(AimBaseFOV); }
			if (Boom) { Boom->TargetArmLength = AimBaseArm; }
			if (bBodyHiddenForScope)
			{
				SetBodyHiddenForOwner(Character, WeaponActor, false);
			}
			bBodyHiddenForScope = false;
			bAimBaseSaved = false;
		}
		return;
	}

	if (!bAimBaseSaved)
	{
		AimBaseFOV = Camera ? Camera->FieldOfView : 90.0f;
		AimBaseArm = Boom ? Boom->TargetArmLength : 0.0f;
		bAimBaseSaved = true;
	}
	if (CurrentWeapon == nullptr)
	{
		return;
	}

	const bool bSniper = CurrentWeapon->WeaponType == TEXT("Sniper");
	if (Camera)
	{
		Camera->SetFieldOfView(FMath::Lerp(AimBaseFOV, AimBaseFOV * ZoomOf(CurrentWeapon), AimAlpha));
	}
	if (Boom)
	{
		const float AimArm = bSniper ? 10.0f : FMath::Min(AimBaseArm, 150.0f);
		Boom->TargetArmLength = FMath::Lerp(AimBaseArm, AimArm, AimAlpha);
	}

	// through the scope the body would be in the way
	const bool bHideBody = bSniper && AimAlpha > 0.6f;
	if (bHideBody != bBodyHiddenForScope)
	{
		SetBodyHiddenForOwner(Character, WeaponActor, bHideBody);
		bBodyHiddenForScope = bHideBody;
	}

	// the character looks where the sight points
	if (bWants && DrivesMovement())
	{
		Character->SetActorRotation(FRotator(0.0f, PC->GetControlRotation().Yaw, 0.0f));
	}
}

void UFortnitePortingCharacterComponent::EndAim()
{
	if (ReticleWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(ReticleWidget.ToSharedRef());
	}
	ReticleWidget.Reset();

	ACharacter* Character = GetCharacter();
	if (Character && bAimBaseSaved)
	{
		if (UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>()) { Camera->SetFieldOfView(AimBaseFOV); }
		if (USpringArmComponent* Boom = Character->FindComponentByClass<USpringArmComponent>()) { Boom->TargetArmLength = AimBaseArm; }
		if (bBodyHiddenForScope) { SetBodyHiddenForOwner(Character, WeaponActor, false); }
	}
	bAimBaseSaved = false;
	bBodyHiddenForScope = false;
	bAiming = false;
	AimAlpha = 0.0f;
}

// Networking layer of UFortnitePortingCharacterComponent.
//
// The server is authoritative. The owning client sends requests (Request* -> Server_* RPCs), the server runs the normal
// local code (EquipPickaxe, FireWeapon...) and mirrors the persistent result in NetState, while transient events (shots,
// swings, reloads, slide, mantle) travel as multicasts. Every machine then plays the same visuals.

#include "FortnitePortingCharacterComponent.h"

#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingRuntime.h"
#include "FortnitePortingVehicle.h"
#include "FortnitePortingWeapon.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

void UFortnitePortingCharacterComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UFortnitePortingCharacterComponent, NetState);
	DOREPLIFETIME(UFortnitePortingCharacterComponent, Weapons);
	DOREPLIFETIME(UFortnitePortingCharacterComponent, Pickaxe);
	DOREPLIFETIME(UFortnitePortingCharacterComponent, Glider);
	DOREPLIFETIME(UFortnitePortingCharacterComponent, StyleSelection);
	DOREPLIFETIME_CONDITION(UFortnitePortingCharacterComponent, AmmoInMagazine, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UFortnitePortingCharacterComponent, bAimingNet, COND_SkipOwner);
}

bool UFortnitePortingCharacterComponent::IsNetworked() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr && Owner->GetNetMode() != NM_Standalone;
}

bool UFortnitePortingCharacterComponent::HasNetAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner == nullptr || Owner->HasAuthority();
}

bool UFortnitePortingCharacterComponent::DrivesMovement() const
{
	const ACharacter* Character = GetCharacter();
	if (Character == nullptr)
	{
		return false;
	}
	return !IsNetworked() || Character->IsLocallyControlled();
}

void UFortnitePortingCharacterComponent::SetServerAcceptsClientPosition(bool bAccept) const
{
	const ACharacter* Character = GetCharacter();
	if (UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr)
	{
		// Slide and mantle move the pawn in ways the server does not simulate: for their duration the server trusts
		// the owner's position instead of "correcting" the owner back to where its own simulation stands still
		Movement->bIgnoreClientMovementErrorChecksAndCorrection = bAccept;
		Movement->bServerAcceptClientAuthoritativePosition = bAccept;
	}
}

// ── Requests (local player -> server) ────────────────────────────────────────

void UFortnitePortingCharacterComponent::RequestTogglePickaxe()
{
	if (HasNetAuthority())
	{
		TogglePickaxe();
	}
	else
	{
		Server_TogglePickaxe();
	}
}

void UFortnitePortingCharacterComponent::RequestSwingPickaxe()
{
	if (HasNetAuthority())
	{
		SwingPickaxe();
	}
	else
	{
		Server_SwingPickaxe();
	}
}

void UFortnitePortingCharacterComponent::RequestCycleWeapon()
{
	if (HasNetAuthority())
	{
		CycleWeapon();
	}
	else
	{
		Server_CycleWeapon();
	}
}

void UFortnitePortingCharacterComponent::RequestEquipInventoryWeapon(UFortnitePortingWeaponData* Weapon)
{
	if (Weapon == nullptr || !Weapons.Contains(Weapon))
	{
		return;
	}
	if (HasNetAuthority())
	{
		EquipWeapon(Weapon);
	}
	else
	{
		Server_EquipInventoryWeapon(Weapon);
	}
}

void UFortnitePortingCharacterComponent::RequestDropInventoryWeapon(UFortnitePortingWeaponData* Weapon)
{
	if (Weapon == nullptr || !Weapons.Contains(Weapon))
	{
		return;
	}
	if (HasNetAuthority())
	{
		Server_DropInventoryWeapon_Implementation(Weapon);
	}
	else
	{
		Server_DropInventoryWeapon(Weapon);
	}
}

void UFortnitePortingCharacterComponent::RequestReorderInventoryWeapon(UFortnitePortingWeaponData* Weapon, int32 NewIndex)
{
	if (Weapon == nullptr || !Weapons.Contains(Weapon) || !Weapons.IsValidIndex(NewIndex))
	{
		return;
	}
	if (HasNetAuthority())
	{
		ReorderInventoryWeapon(Weapon, NewIndex);
	}
	else
	{
		Server_ReorderInventoryWeapon(Weapon, NewIndex);
	}
}

void UFortnitePortingCharacterComponent::Server_ReorderInventoryWeapon_Implementation(UFortnitePortingWeaponData* Weapon, int32 NewIndex)
{
	if (HasNetAuthority() && Weapon != nullptr && Weapons.Contains(Weapon) && Weapons.IsValidIndex(NewIndex))
	{
		ReorderInventoryWeapon(Weapon, NewIndex);
	}
}

void UFortnitePortingCharacterComponent::ReorderInventoryWeapon(UFortnitePortingWeaponData* Weapon, int32 NewIndex)
{
	const int32 CurrentIndex = Weapons.IndexOfByKey(Weapon);
	if (CurrentIndex == INDEX_NONE || !Weapons.IsValidIndex(NewIndex) || CurrentIndex == NewIndex)
	{
		return;
	}

	Weapons.RemoveAt(CurrentIndex);
	Weapons.Insert(Weapon, NewIndex);
	CurrentWeaponIndex = CurrentWeapon ? Weapons.IndexOfByKey(CurrentWeapon) : INDEX_NONE;
	OnInventoryChanged.Broadcast();
	if (AActor* ComponentOwner = GetOwner())
	{
		ComponentOwner->ForceNetUpdate();
	}
}

void UFortnitePortingCharacterComponent::RequestFire()
{
	FireWeapon();
}

void UFortnitePortingCharacterComponent::RequestReload()
{
	ReloadWeapon();
}

void UFortnitePortingCharacterComponent::RequestGlider(bool bDeploy)
{
	if (HasNetAuthority())
	{
		if (bDeploy)
		{
			DeployGlider();
		}
		else
		{
			StopGliding();
		}
	}
	else
	{
		Server_Glider(bDeploy);
	}
}

void UFortnitePortingCharacterComponent::RequestEmote(UFortnitePortingEmoteData* Emote)
{
	if (HasNetAuthority())
	{
		PlayOrQueueEmote(Emote);
	}
	else
	{
		Server_PlayEmote(Emote);
	}
}

void UFortnitePortingCharacterComponent::RequestEnterVehicle(AFortnitePortingVehicle* Vehicle)
{
	if (HasNetAuthority())
	{
		EnterVehicle(Vehicle);
	}
	else
	{
		Server_EnterVehicle(Vehicle);
	}
}

void UFortnitePortingCharacterComponent::RequestExitVehicle()
{
	if (HasNetAuthority())
	{
		ExitVehicle();
	}
	else
	{
		Server_ExitVehicle();
	}
}

void UFortnitePortingCharacterComponent::RequestSprint(bool bSprint)
{
	const bool bChanged = bSprint != bSprinting;

	// Applied right away for the owner's own prediction, the server follows
	SetSprinting(bSprint);

	if (bChanged && !HasNetAuthority())
	{
		Server_Sprint(bSprint);
	}
}

// ── Server RPCs ──────────────────────────────────────────────────────────────

void UFortnitePortingCharacterComponent::Server_TogglePickaxe_Implementation()
{
	TogglePickaxe();
}

void UFortnitePortingCharacterComponent::Server_SwingPickaxe_Implementation()
{
	SwingPickaxe();
}

void UFortnitePortingCharacterComponent::Server_CycleWeapon_Implementation()
{
	CycleWeapon();
}

void UFortnitePortingCharacterComponent::Server_EquipInventoryWeapon_Implementation(UFortnitePortingWeaponData* Weapon)
{
	if (Weapon != nullptr && Weapons.Contains(Weapon))
	{
		EquipWeapon(Weapon);
	}
}

void UFortnitePortingCharacterComponent::Server_DropInventoryWeapon_Implementation(UFortnitePortingWeaponData* Weapon)
{
	if (Weapon == nullptr || !Weapons.Contains(Weapon) || !HasNetAuthority() || GetCharacter() == nullptr || GetWorld() == nullptr)
	{
		return;
	}

	ACharacter* Character = GetCharacter();
	const FVector Forward = Character->GetActorForwardVector();
	FVector DropLocation = Character->GetActorLocation() + Forward * 160.0f - FVector(0.0f, 0.0f, 50.0f);
	const FVector TraceStart = Character->GetActorLocation() + Forward * 160.0f + FVector(0.0f, 0.0f, 220.0f);
	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DropInventoryWeapon), false, Character);
	if (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceStart - FVector(0.0f, 0.0f, 1000.0f), ECC_WorldStatic, QueryParams))
	{
		DropLocation = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 35.0f);
	}

	UClass* WeaponClass = Weapon->WeaponActorClass.Get();
	if (WeaponClass == nullptr || !WeaponClass->IsChildOf(AFortnitePortingWeapon::StaticClass()))
	{
		WeaponClass = AFortnitePortingWeapon::StaticClass();
	}

	const FTransform SpawnTransform(Character->GetActorRotation(), DropLocation);
	AFortnitePortingWeapon* Pickup = GetWorld()->SpawnActorDeferred<AFortnitePortingWeapon>(
		WeaponClass, SpawnTransform, Character, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Pickup == nullptr)
	{
		UE_LOG(LogFortnitePortingRuntime, Error, TEXT("Could not spawn dropped weapon %s"), *GetNameSafe(Weapon));
		return;
	}
	Pickup->WeaponData = Weapon;
	Pickup->bIsPickup = true;
	Pickup->FinishSpawning(SpawnTransform);

	const bool bWasEquipped = CurrentWeapon == Weapon;
	const int32 PreviousIndex = Weapons.IndexOfByKey(Weapon);
	if (bWasEquipped)
	{
		UnequipWeapon();
	}
	Weapons.RemoveSingle(Weapon);
	CurrentWeaponIndex = CurrentWeapon ? Weapons.IndexOfByKey(CurrentWeapon) : INDEX_NONE;

	if (bWasEquipped && !Weapons.IsEmpty())
	{
		EquipWeapon(Weapons[FMath::Clamp(PreviousIndex, 0, Weapons.Num() - 1)]);
	}
	else if (bWasEquipped)
	{
		SyncNetStateFromLocal();
	}
	OnInventoryChanged.Broadcast();
}

void UFortnitePortingCharacterComponent::Server_Fire_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	FireWeaponAuthoritative(Origin, Direction);
}

void UFortnitePortingCharacterComponent::Server_Reload_Implementation()
{
	ReloadAuthoritative();
}

void UFortnitePortingCharacterComponent::Server_Glider_Implementation(bool bDeploy)
{
	if (bDeploy)
	{
		DeployGlider();
	}
	else
	{
		StopGliding();
	}
}

void UFortnitePortingCharacterComponent::Server_PlayEmote_Implementation(UFortnitePortingEmoteData* Emote)
{
	// Only emotes this character actually owns
	if (Emote != nullptr && Emotes.Contains(Emote))
	{
		PlayOrQueueEmote(Emote);
	}
}

void UFortnitePortingCharacterComponent::Server_EnterVehicle_Implementation(AFortnitePortingVehicle* Vehicle)
{
	EnterVehicle(Vehicle);
}

void UFortnitePortingCharacterComponent::Server_ExitVehicle_Implementation()
{
	ExitVehicle();
}

void UFortnitePortingCharacterComponent::Server_Sprint_Implementation(bool bSprint)
{
	SetSprinting(bSprint);
}

void UFortnitePortingCharacterComponent::Server_SlideState_Implementation(bool bSlide)
{
	SetSlideVisual(bSlide);
	SetServerAcceptsClientPosition(bSlide);
	Multicast_SlideVisual(bSlide);
}

void UFortnitePortingCharacterComponent::Server_MantleState_Implementation(bool bMantle, UAnimMontage* Montage, float PlayRate)
{
	SetMantleVisual(bMantle, Montage, PlayRate);
	SetServerAcceptsClientPosition(bMantle);
	Multicast_MantleVisual(bMantle, Montage, PlayRate);
}

// ── Multicasts (visual events) ───────────────────────────────────────────────

void UFortnitePortingCharacterComponent::Multicast_FireFX_Implementation(FVector_NetQuantizeNormal Direction, FVector_NetQuantize End, bool bHit)
{
	// The server already played it when it fired
	if (!HasNetAuthority())
	{
		FireFX(Direction, End, bHit);
	}
}

void UFortnitePortingCharacterComponent::Multicast_ReloadFX_Implementation()
{
	if (!HasNetAuthority() && CurrentWeapon != nullptr)
	{
		ReloadFX();
	}
}

void UFortnitePortingCharacterComponent::Multicast_SwingFX_Implementation(int32 Section)
{
	if (HasNetAuthority() || !bPickaxeEquipped || Pickaxe == nullptr)
	{
		return;
	}

	UAnimMontage* Swing = Pickaxe->GetSwingMontage(SkeletonProfile);
	if (IsUsableMontage(Swing))
	{
		UseUpperBodySlot(Swing);
		PlayMontageSection(Swing, Section, SwingPlayRate);
	}
}

void UFortnitePortingCharacterComponent::Multicast_SlideVisual_Implementation(bool bSlide)
{
	// The server and the owner already did it; only the other clients mirror it
	if (!HasNetAuthority() && !DrivesMovement())
	{
		SetSlideVisual(bSlide);
	}
}

void UFortnitePortingCharacterComponent::Multicast_MantleVisual_Implementation(bool bMantle, UAnimMontage* Montage, float PlayRate)
{
	if (!HasNetAuthority() && !DrivesMovement())
	{
		SetMantleVisual(bMantle, Montage, PlayRate);
	}
}

// ── Slide / mantle mirroring ─────────────────────────────────────────────────

void UFortnitePortingCharacterComponent::SetSlideVisual(bool bSlide)
{
	if (bSlide == bSliding)
	{
		return;
	}

	if (bSlide)
	{
		bSliding = true;
		ActiveSlideMontage = SlideMontage;
		PlayMontage(ActiveSlideMontage);
	}
	else
	{
		bSliding = false;
		StopMontage(ActiveSlideMontage, 0.25f);
		ActiveSlideMontage = nullptr;
	}
}

void UFortnitePortingCharacterComponent::SetMantleVisual(bool bMantle, UAnimMontage* Montage, float PlayRate)
{
	if (bMantle)
	{
		StopGliding();
		StopEmote();
		bMantling = true;
		ActiveMantleMontage = Montage;
		if (Montage != nullptr)
		{
			PlayMontage(Montage, PlayRate);
		}
		return;
	}

	bMantling = false;

	// A montage with a landing section plays it out as the character runs on; otherwise it blends away
	static const FName LandSection(TEXT("Land"));
	if (ActiveMantleMontage != nullptr && ActiveMantleMontage->GetSectionIndex(LandSection) == INDEX_NONE)
	{
		StopMontage(ActiveMantleMontage, MantleBlendOutTime);
	}
}

void UFortnitePortingCharacterComponent::BroadcastSlide(bool bSlide)
{
	if (!IsNetworked())
	{
		return;
	}

	if (DrivesMovement())
	{
		if (UCharacterMovementComponent* Movement = GetCharacter()->GetCharacterMovement())
		{
			Movement->bClientIgnoreMovementCorrections = bSlide;
		}
	}

	if (HasNetAuthority())
	{
		Multicast_SlideVisual(bSlide);
	}
	else if (DrivesMovement())
	{
		Server_SlideState(bSlide);
	}
}

void UFortnitePortingCharacterComponent::BroadcastMantle(bool bMantle, UAnimMontage* Montage, float PlayRate)
{
	if (!IsNetworked())
	{
		return;
	}

	if (DrivesMovement())
	{
		if (UCharacterMovementComponent* Movement = GetCharacter()->GetCharacterMovement())
		{
			Movement->bClientIgnoreMovementCorrections = bMantle;
		}
	}

	if (HasNetAuthority())
	{
		Multicast_MantleVisual(bMantle, Montage, PlayRate);
	}
	else if (DrivesMovement())
	{
		Server_MantleState(bMantle, Montage, PlayRate);
	}
}

// ── NetState mirroring ───────────────────────────────────────────────────────

void UFortnitePortingCharacterComponent::SyncNetStateFromLocal()
{
	NetState.bPickaxe = bPickaxeEquipped;
	NetState.bGliding = bGliding;
	NetState.Weapon = CurrentWeapon;
	NetState.Emote = ActiveEmoteMontage != nullptr ? ActiveEmoteData.Get() : nullptr;
	NetState.Vehicle = CurrentVehicle;
	NetState.bVehiclePassenger = CurrentVehicle != nullptr && bIsPassenger;

	if (EmotePlayCount != SyncedEmotePlayCount)
	{
		SyncedEmotePlayCount = EmotePlayCount;
		++NetState.EmoteSerial;
	}
}

void UFortnitePortingCharacterComponent::OnRep_NetState()
{
	ApplyNetState();
}

void UFortnitePortingCharacterComponent::OnRep_Weapons()
{
	if (CurrentWeapon != nullptr)
	{
		CurrentWeaponIndex = Weapons.IndexOfByKey(CurrentWeapon);
	}
	OnInventoryChanged.Broadcast();
}

void UFortnitePortingCharacterComponent::OnRep_Pickaxe()
{
	// The server swapped the pickaxe: rebuild the one in the hands, then catch up with the rest of the state
	if (bPickaxeEquipped)
	{
		UnequipPickaxe();
		EquipPickaxe();
	}
	ApplyNetState();
}

void UFortnitePortingCharacterComponent::OnRep_Glider()
{
	if (bGliding)
	{
		StopGliding();
	}
	ApplyNetState();
}

void UFortnitePortingCharacterComponent::ApplyNetState()
{
	if (HasNetAuthority() || GetCharacter() == nullptr)
	{
		return;
	}

	if (NetState.bPickaxe != bPickaxeEquipped)
	{
		if (NetState.bPickaxe)
		{
			EquipPickaxe();
		}
		else
		{
			UnequipPickaxe();
		}
	}

	if (NetState.Weapon != CurrentWeapon)
	{
		if (NetState.Weapon != nullptr)
		{
			EquipWeapon(NetState.Weapon);
		}
		else
		{
			UnequipWeapon();
		}
	}

	if (NetState.bGliding != bGliding)
	{
		if (NetState.bGliding)
		{
			DeployGlider();
		}
		else
		{
			StopGliding();
		}
	}

	if (NetState.EmoteSerial != LastAppliedEmoteSerial)
	{
		LastAppliedEmoteSerial = NetState.EmoteSerial;
		if (NetState.Emote != nullptr)
		{
			PlayOrQueueEmote(NetState.Emote);
		}
	}
	if (NetState.Emote == nullptr && IsEmoting())
	{
		StopEmote();
	}

	if (NetState.Vehicle != CurrentVehicle)
	{
		if (NetState.Vehicle != nullptr)
		{
			EnterVehicleAs(NetState.Vehicle, NetState.bVehiclePassenger);
		}
		else
		{
			ExitVehicle();
		}
	}
}

void UFortnitePortingCharacterComponent::Multicast_PickaxeImpact_Implementation(FName Surface, FVector_NetQuantize Point)
{
	// Fortnite's own impact cues: melee on wood, heavy hits on stone and metal (brick counts as stone)
	const TCHAR* Cue = TEXT("Melee_Weapon_Impact_Wood_A");
	if (Surface == TEXT("Stone"))
	{
		Cue = TEXT("Bullet_Impact_Stone_Heavy");
	}
	else if (Surface == TEXT("Metal"))
	{
		Cue = TEXT("Bullet_Impact_Metal_Heavy");
	}
	const FString Name = FString::Printf(TEXT("%s_%02d"), Cue, FMath::RandRange(1, 4));
	if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Arena/Sounds/Hit/%s.%s"), *Name, *Name)))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, FVector(Point), 0.55f);
	}
}

void UFortnitePortingCharacterComponent::Server_SetAiming_Implementation(bool bAim)
{
	bAimingNet = bAim;
}

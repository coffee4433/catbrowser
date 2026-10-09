#include "FortnitePortingCharacterComponent.h"

#include "FortnitePortingRuntime.h"
#include "FortnitePortingStyleData.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"

void UFortnitePortingCharacterComponent::SetStyleSelection(const TArray<int32>& Selection)
{
	if (StyleData == nullptr)
	{
		return;
	}

	const TArray<int32> Clean = StyleData->Sanitize(Selection);
	if (IsNetworked() && !HasNetAuthority())
	{
		// The server decides and replicates it back (OnRep_StyleSelection)
		Server_SetStyleSelection(Clean);
		return;
	}

	StyleSelection = Clean;
	ApplyStyleSelection();
}

void UFortnitePortingCharacterComponent::Server_SetStyleSelection_Implementation(const TArray<int32>& Selection)
{
	if (StyleData == nullptr)
	{
		return;
	}

	StyleSelection = StyleData->Sanitize(Selection);
	ApplyStyleSelection();
}

void UFortnitePortingCharacterComponent::OnRep_StyleSelection()
{
	ApplyStyleSelection();
}

void UFortnitePortingCharacterComponent::ApplyStyleSelection()
{
	ACharacter* Character = GetCharacter();
	if (Character == nullptr || StyleData == nullptr)
	{
		return;
	}

	USkeletalMeshComponent* Body = Character->GetMesh();
	const TArray<int32> Selection = StyleData->Sanitize(StyleSelection);

	FString SelectionText;
	for (const int32 Pick : Selection)
	{
		SelectionText += FString::Printf(TEXT("%d "), Pick);
	}
	UE_LOG(LogFortnitePortingRuntime, Log, TEXT("Style of %s (%s): [ %s]"), *Character->GetName(), *UEnum::GetValueAsString(Character->GetLocalRole()), *SelectionText);

	TArray<USkeletalMeshComponent*> Components;
	Character->GetComponents<USkeletalMeshComponent>(Components);

	// 1. Remember the outfit's own look the first time, then go back to it
	if (!bStyleBaselineTaken)
	{
		for (USkeletalMeshComponent* Component : Components)
		{
			// The hidden weapon rig is driven by the weapon code, styles never touch it
			if (WeaponRigMesh != nullptr && Component->GetSkeletalMeshAsset() == WeaponRigMesh)
			{
				continue;
			}

			FFortnitePortingStyleBaseline Baseline;
			Baseline.Component = Component;
			Baseline.Mesh = Component->GetSkeletalMeshAsset();
			Baseline.AnimClass = Component->GetAnimClass();
			Baseline.bVisible = Component->IsVisible();
			for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
			{
				Baseline.Materials.Add(Component->GetMaterial(Slot));
			}
			StyleBaselines.Add(Baseline);
		}
		bStyleBaselineTaken = true;
	}

	for (USkeletalMeshComponent* Part : StyleParts)
	{
		if (Part != nullptr)
		{
			Part->DestroyComponent();
		}
	}
	StyleParts.Reset();

	for (const FFortnitePortingStyleBaseline& Baseline : StyleBaselines)
	{
		USkeletalMeshComponent* Component = Baseline.Component;
		if (Component == nullptr)
		{
			continue;
		}

		// Overrides belong to slots, not to meshes: clear them before the mesh changes or they land on the new mesh
		Component->EmptyOverrideMaterials();
		if (Component->GetSkeletalMeshAsset() != Baseline.Mesh)
		{
			Component->SetSkeletalMeshAsset(Baseline.Mesh);
		}
		if (Baseline.AnimClass != nullptr && Component->GetAnimClass() != Baseline.AnimClass)
		{
			Component->SetAnimInstanceClass(Baseline.AnimClass);
		}

		// Only what differs from the mesh's own materials is an override
		const USkeletalMesh* BaselineMesh = Component->GetSkeletalMeshAsset();
		for (int32 Slot = 0; Slot < Baseline.Materials.Num(); ++Slot)
		{
			UMaterialInterface* MeshMaterial = BaselineMesh != nullptr && BaselineMesh->GetMaterials().IsValidIndex(Slot) ? BaselineMesh->GetMaterials()[Slot].MaterialInterface.Get() : nullptr;
			if (Baseline.Materials[Slot] != MeshMaterial)
			{
				Component->SetMaterial(Slot, Baseline.Materials[Slot]);
			}
		}
		Component->SetVisibility(Baseline.bVisible);
	}

	Components.Reset();
	Character->GetComponents<USkeletalMeshComponent>(Components);

	auto FindPart = [&Components](FName Name) -> USkeletalMeshComponent*
	{
		for (USkeletalMeshComponent* Component : Components)
		{
			if (Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	};

	// 2. Parts of the picked options
	for (int32 ChannelIndex = 0; ChannelIndex < StyleData->Channels.Num(); ++ChannelIndex)
	{
		const FFortnitePortingStyleChannel& Channel = StyleData->Channels[ChannelIndex];
		const int32 Pick = Selection[ChannelIndex];
		if (Pick <= 0 || !Channel.Options.IsValidIndex(Pick))
		{
			continue;
		}

		const FFortnitePortingStyleOption& Option = Channel.Options[Pick];

		if (Option.BodyMesh != nullptr && Body != nullptr && Body->GetSkeletalMeshAsset() != Option.BodyMesh)
		{
			Body->EmptyOverrideMaterials();
			Body->SetSkeletalMeshAsset(Option.BodyMesh);
		}

		// Parts the outfit shows by default in this channel that this option does not
		for (const FFortnitePortingStylePart& DefaultPart : Channel.Options[0].Parts)
		{
			const bool bKept = Option.Parts.ContainsByPredicate([&DefaultPart](const FFortnitePortingStylePart& Part) { return Part.ComponentName == DefaultPart.ComponentName; });
			if (!bKept)
			{
				if (USkeletalMeshComponent* Hidden = FindPart(DefaultPart.ComponentName))
				{
					Hidden->SetVisibility(false);
				}
			}
		}

		for (const FFortnitePortingStylePart& Part : Option.Parts)
		{
			if (Part.Mesh == nullptr)
			{
				continue;
			}

			if (USkeletalMeshComponent* Existing = FindPart(Part.ComponentName))
			{
				if (Existing->GetSkeletalMeshAsset() != Part.Mesh)
				{
					Existing->EmptyOverrideMaterials();
					Existing->SetSkeletalMeshAsset(Part.Mesh);
				}
				if (Part.AnimClass != nullptr && Existing->GetAnimClass() != Part.AnimClass)
				{
					Existing->SetAnimInstanceClass(Part.AnimClass);
				}
				Existing->SetVisibility(true);
				continue;
			}

			// A part the outfit does not have: a new component following the body like the ones of the Blueprint
			USkeletalMeshComponent* NewPart = NewObject<USkeletalMeshComponent>(Character, MakeUniqueObjectName(Character, USkeletalMeshComponent::StaticClass(), Part.ComponentName));
			NewPart->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			NewPart->bForceMipStreaming = true;
			NewPart->SetSkeletalMeshAsset(Part.Mesh);
			if (Part.AnimClass != nullptr)
			{
				NewPart->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				NewPart->SetAnimInstanceClass(Part.AnimClass);
			}
			NewPart->RegisterComponent();
			NewPart->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Part.Socket);
			if (Body != nullptr)
			{
				NewPart->AddTickPrerequisiteComponent(Body);
			}
			StyleParts.Add(NewPart);
			Components.Add(NewPart);
		}
	}

	// 3. Materials last: setting a mesh resets its materials
	for (int32 ChannelIndex = 0; ChannelIndex < StyleData->Channels.Num(); ++ChannelIndex)
	{
		const FFortnitePortingStyleChannel& Channel = StyleData->Channels[ChannelIndex];
		const int32 Pick = Selection[ChannelIndex];
		if (Pick <= 0 || !Channel.Options.IsValidIndex(Pick))
		{
			continue;
		}

		for (const FFortnitePortingStyleMaterial& Entry : Channel.Options[Pick].Materials)
		{
			if (Entry.Mesh == nullptr || Entry.Material == nullptr)
			{
				continue;
			}

			for (USkeletalMeshComponent* Component : Components)
			{
				if (Component->GetSkeletalMeshAsset() == Entry.Mesh && Entry.Slot >= 0 && Entry.Slot < Component->GetNumMaterials())
				{
					Component->SetMaterial(Entry.Slot, Entry.Material);
				}
			}
		}
	}
}

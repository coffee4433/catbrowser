#include "FortnitePortingWeapon.h"

#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

AFortnitePortingWeapon::AFortnitePortingWeapon()
{
	PrimaryActorTick.bCanEverTick = true;

	// Pickups placed in a level replicate so that taking one removes it for everybody. The character component turns
	// replication off on the copies it spawns in a hand: every machine builds its own from the replicated NetState.
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(Root);
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	WeaponStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponStaticMesh"));
	WeaponStaticMesh->SetupAttachment(Root);
	WeaponStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(Root);
	PickupSphere->SetSphereRadius(90.0f);
	PickupSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupSphere->SetGenerateOverlapEvents(true);
}

void AFortnitePortingWeapon::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyWeaponData();
}

void AFortnitePortingWeapon::BeginPlay()
{
	Super::BeginPlay();
	ApplyWeaponData();
	SetHeld(!bIsPickup);
}

void AFortnitePortingWeapon::ApplyWeaponData()
{
	if (WeaponData == nullptr || WeaponData->Meshes.IsEmpty())
	{
		return;
	}

	UStreamableRenderAsset* Mesh = WeaponData->Meshes[0].Mesh;
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);

	WeaponMesh->SetSkeletalMeshAsset(SkeletalMesh);
	WeaponMesh->SetVisibility(SkeletalMesh != nullptr);
	WeaponStaticMesh->SetStaticMesh(StaticMesh);
	WeaponStaticMesh->SetVisibility(StaticMesh != nullptr);

	if (SkeletalMesh && WeaponData->WeaponIdleAnimation)
	{
		PlayWeaponAnimation(WeaponData->WeaponIdleAnimation, true);
	}
}

void AFortnitePortingWeapon::SetHeld(bool bInHeld)
{
	bHeld = bInHeld;
	PickupSphere->SetCollisionEnabled(bHeld || !bIsPickup ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	SetActorEnableCollision(!bHeld);
	if (bHeld)
	{
		WeaponMesh->SetRelativeRotation(FRotator::ZeroRotator);
		WeaponStaticMesh->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AFortnitePortingWeapon::PlayWeaponAnimation(UAnimSequence* Animation, bool bLoop, float PlayRate)
{
	if (Animation && WeaponMesh->GetSkeletalMeshAsset())
	{
		WeaponMesh->PlayAnimation(Animation, bLoop);
		WeaponMesh->SetPlayRate(PlayRate);
	}
}

FVector AFortnitePortingWeapon::GetMuzzleDirection() const
{
	if (WeaponData && WeaponData->bMuzzleFitted)
	{
		const USceneComponent* Visible = WeaponMesh->GetSkeletalMeshAsset() ? static_cast<const USceneComponent*>(WeaponMesh.Get()) : static_cast<const USceneComponent*>(WeaponStaticMesh.Get());
		return Visible->GetComponentTransform().TransformVectorNoScale(WeaponData->MuzzleDirection.GetSafeNormal());
	}
	return GetActorForwardVector();
}

FVector AFortnitePortingWeapon::GetMuzzleLocation() const
{
	// the barrel tip found in the mesh when the weapon was imported (FP.FitMuzzles): exactly where the gun ends, whatever the weapon
	if (WeaponData && WeaponData->bMuzzleFitted)
	{
		const USceneComponent* Visible = WeaponMesh->GetSkeletalMeshAsset() ? static_cast<const USceneComponent*>(WeaponMesh.Get()) : static_cast<const USceneComponent*>(WeaponStaticMesh.Get());
		return Visible->GetComponentTransform().TransformPosition(WeaponData->MuzzleOffset);
	}

	for (const FName Socket : { FName(TEXT("Muzzle")), FName(TEXT("MuzzleFlash")), FName(TEXT("muzzle")), FName(TEXT("FX_Muzzle")) })
	{
		if (WeaponMesh->DoesSocketExist(Socket))
		{
			return WeaponMesh->GetSocketLocation(Socket);
		}
	}

	const UPrimitiveComponent* Visible = WeaponMesh->GetSkeletalMeshAsset() ? static_cast<const UPrimitiveComponent*>(WeaponMesh.Get()) : static_cast<const UPrimitiveComponent*>(WeaponStaticMesh.Get());
	const FBoxSphereBounds Bounds = Visible->Bounds;
	return Bounds.Origin + GetActorForwardVector() * Bounds.BoxExtent.GetMax();
}

void AFortnitePortingWeapon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsPickup && !bHeld)
	{
		const FRotator Spin(0.0f, 90.0f * DeltaSeconds, 0.0f);
		WeaponMesh->AddLocalRotation(Spin);
		WeaponStaticMesh->AddLocalRotation(Spin);
	}
}

void AFortnitePortingWeapon::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);

	if (!bIsPickup || bHeld || OtherActor == nullptr)
	{
		return;
	}

	if (UFortnitePortingCharacterComponent* Component = OtherActor->FindComponentByClass<UFortnitePortingCharacterComponent>())
	{
		Component->PickupWeapon(this);
	}
}

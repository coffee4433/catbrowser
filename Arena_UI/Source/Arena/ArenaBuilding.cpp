#include "ArenaBuilding.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

AArenaBuilding::AArenaBuilding()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	SetReplicateMovement(false);
	NetUpdateFrequency = 4.0f;
	NetDormancy = DORM_Initial;   // a building only talks to the network while one of its doors changes
	SetNetCullDistanceSquared(250000.0f * 250000.0f);   // 2.5 km: doors are only interesting up close

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetMobility(EComponentMobility::Static);
	RootComponent = Body;
}

void AArenaBuilding::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Body->SetStaticMesh(BodyMesh);
	RebuildDoors();
}

void AArenaBuilding::BeginPlay()
{
	Super::BeginPlay();
	// a building spawned in the editor is rebuilt in OnConstruction, but one loaded from the map starts here too
	if (DoorComponents.Num() != Doors.Num())
	{
		RebuildDoors();
	}
}

void AArenaBuilding::RebuildDoors()
{
	for (UStaticMeshComponent* Old : DoorComponents)
	{
		if (Old)
		{
			Old->DestroyComponent();
		}
	}
	DoorComponents.Reset();
	Angles.Reset();

	for (int32 Index = 0; Index < Doors.Num(); ++Index)
	{
		UStaticMeshComponent* Leaf = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Leaf->SetMobility(EComponentMobility::Movable);
		Leaf->SetStaticMesh(DoorMesh);
		Leaf->SetupAttachment(RootComponent);
		Leaf->SetRelativeLocationAndRotation(Doors[Index].Location, FRotator(0.0f, Doors[Index].Yaw, 0.0f));
		Leaf->RegisterComponent();
		DoorComponents.Add(Leaf);
		Angles.Add(0.0f);
	}
	// bring the leaves to the replicated state (a client that joins late sees the open doors open)
	for (int32 Index = 0; Index < Doors.Num(); ++Index)
	{
		if (OpenMask & (1 << Index))
		{
			Angles[Index] = ((SwingMask & (1 << Index)) ? -OpenAngle : OpenAngle);
			DoorComponents[Index]->SetRelativeRotation(FRotator(0.0f, Doors[Index].Yaw + Angles[Index], 0.0f));
		}
	}
}

int32 AArenaBuilding::FindDoorNear(const FVector& WorldLocation, float MaxDistance, float& OutDistance) const
{
	int32 Best = INDEX_NONE;
	OutDistance = MaxDistance;
	const FTransform Local = GetActorTransform();
	for (int32 Index = 0; Index < Doors.Num(); ++Index)
	{
		// distance to the middle of the leaf, not to the hinge
		const FVector Middle = Local.TransformPosition(Doors[Index].Location + FRotator(0.0f, Doors[Index].Yaw, 0.0f).Vector() * 55.0f);
		const float Distance = FVector::Dist(Middle, WorldLocation);
		if (Distance < OutDistance)
		{
			OutDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

void AArenaBuilding::ToggleDoor(int32 Index, const FVector& UserLocation)
{
	if (!HasAuthority() || !Doors.IsValidIndex(Index) || Index > 30)
	{
		return;
	}
	FlushNetDormancy();
	const int32 Bit = 1 << Index;
	if (OpenMask & Bit)
	{
		OpenMask &= ~Bit;
	}
	else
	{
		// the closed leaf points along its yaw: its normal tells which side the user is on and the door opens away from them
		const FTransform Local = GetActorTransform();
		const FVector Normal = Local.TransformVectorNoScale(FRotator(0.0f, Doors[Index].Yaw + 90.0f, 0.0f).Vector());
		const FVector Hinge = Local.TransformPosition(Doors[Index].Location);
		const bool bUserOnNormalSide = FVector::DotProduct(UserLocation - Hinge, Normal) > 0.0f;
		if (bUserOnNormalSide)
		{
			SwingMask |= Bit;
		}
		else
		{
			SwingMask &= ~Bit;
		}
		OpenMask |= Bit;
	}
	OnRep_Doors();
}

void AArenaBuilding::OnRep_Doors()
{
	SetActorTickEnabled(true);
}

void AArenaBuilding::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bool bMoving = false;
	for (int32 Index = 0; Index < DoorComponents.Num(); ++Index)
	{
		const bool bOpen = (OpenMask & (1 << Index)) != 0;
		const float Target = bOpen ? ((SwingMask & (1 << Index)) ? -OpenAngle : OpenAngle) : 0.0f;
		float& Angle = Angles[Index];
		if (!FMath::IsNearlyEqual(Angle, Target, 0.1f))
		{
			Angle = FMath::FInterpConstantTo(Angle, Target, DeltaSeconds, DoorSpeed);
			DoorComponents[Index]->SetRelativeRotation(FRotator(0.0f, Doors[Index].Yaw + Angle, 0.0f));
			bMoving = true;
		}
	}
	if (!bMoving)
	{
		SetActorTickEnabled(false);
	}
}

void AArenaBuilding::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArenaBuilding, OpenMask);
	DOREPLIFETIME(AArenaBuilding, SwingMask);
}

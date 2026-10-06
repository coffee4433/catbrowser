// A house / shop / tower of the island: one static body with doors that swing open (E key), the same for every player in the match.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaBuilding.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

/** A hinged door of a building: the pivot is the hinge, in the building's local space */
USTRUCT(BlueprintType)
struct ARENA_API FArenaDoorSocket
{
	GENERATED_BODY()

	/** Hinge position (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	FVector Location = FVector::ZeroVector;

	/** Yaw of the closed door: the door leaf extends along its local +X */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	float Yaw = 0.0f;
};

UCLASS()
class ARENA_API AArenaBuilding : public AActor
{
	GENERATED_BODY()

public:

	AArenaBuilding();

	/** The walls, floor and roof */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	TObjectPtr<UStaticMesh> BodyMesh;

	/** The leaf that is placed on every socket */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	TObjectPtr<UStaticMesh> DoorMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	TArray<FArenaDoorSocket> Doors;

	/** How far the door swings (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	float OpenAngle = 100.0f;

	/** Degrees per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	float DoorSpeed = 220.0f;

	/** Index of the closest door within reach of the location (-1 = none) and its distance */
	int32 FindDoorNear(const FVector& WorldLocation, float MaxDistance, float& OutDistance) const;

	/** Server: opens or closes the door; the side the user stands on decides which way it swings */
	void ToggleDoor(int32 Index, const FVector& UserLocation);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	virtual void BeginPlay() override;

	/** Bit i set: door i is open. Bit i of SwingMask: it swings the other way */
	UPROPERTY(ReplicatedUsing = OnRep_Doors)
	int32 OpenMask = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Doors)
	int32 SwingMask = 0;

	UFUNCTION()
	void OnRep_Doors();

private:

	void RebuildDoors();

	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> DoorComponents;

	TArray<float> Angles;
};

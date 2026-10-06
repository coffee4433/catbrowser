// One wall, floor, stair or roof a player built. The server owns it; the shape (edited tiles), material and health are replicated.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaBuildTypes.h"
#include "ArenaBuildPiece.generated.h"

class UStaticMeshComponent;
class UBoxComponent;

UCLASS()
class ARENA_API AArenaBuildPiece : public AActor
{
	GENERATED_BODY()

public:

	AArenaBuildPiece();

	/** Server: sets what the piece is. Location and rotation are set by the caller (Rotation steps are only kept for the edit grid) */
	void Init(EArenaBuildPiece InPiece, EArenaBuildMaterial InMaterial, int32 InShapeIndex, bool bInMirror, int32 InRotation, const FIntVector& InCell, int32 InEdge, APawn* InBuilder, int32 InBuildTextureIndex);

	/** Server: swaps the shape (an edit). The caller moves the actor when the turn changed */
	void SetShape(int32 InShapeIndex, bool bInMirror, int32 InRotation);

	EArenaBuildPiece GetPiece() const { return Piece; }
	EArenaBuildMaterial GetMaterial() const { return Material; }
	int32 GetShapeIndex() const { return ShapeIndex; }
	bool IsMirrored() const { return bMirror; }
	int32 GetRotation() const { return Rotation; }
	const FString& GetKey() const { return Key; }
	const FIntVector& GetCell() const { return Cell; }
	int32 GetEdge() const { return Edge; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const;

	/** World time (on this machine) of the last hit the piece took, for the health card */
	float GetLastHitTime() const { return LastHitTime; }

	/** The tile mask of the current shape as the edit grid shows it, including mirror and turn */
	uint16 GetCurrentMask() const;

	/** True when the shape is the stair spiral */
	bool IsSpiral() const;

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The piece lost its support (the foundation under it broke): after Delay it breaks too. Delays grow with the height, so a tower
	 *  crumbles from the bottom to the top */
	void CollapseAfter(float Delay);
	bool IsCollapsing() const { return bCollapsing; }

protected:

	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Look();

	UFUNCTION()
	void OnRep_Health(float OldHealth);

public:

	/** Chips fly off where the piece was hit (weapons and the pickaxe) */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDamaged(FVector Point, float HealthFraction);

	/** The piece breaks apart: the pieces of it burst and the piece disappears */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBroken(FVector Point);

	/** Everybody hears and sees an edit being made: the confirm cue and the editor's animation */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEdited(APawn* Editor);

private:

	/** The thunk of a new piece and the builder's animation, played once on every machine when the piece appears */
	void PlayBuiltFeedback();

	void ApplyLook();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> StairCollisionRamp;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	EArenaBuildPiece Piece = EArenaBuildPiece::Wall;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	EArenaBuildMaterial Material = EArenaBuildMaterial::Wood;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	int32 ShapeIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	bool bMirror = false;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	int32 Rotation = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Look)
	int32 BuildTextureIndex = 1;

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 150.0f;

	float LastHitTime = -1000.0f;
	bool bCollapsing = false;
	bool bBroken = false;
	FTimerHandle CollapseTimer;

	UPROPERTY(Replicated)
	FString Key;

	UPROPERTY(Replicated)
	FIntVector Cell = FIntVector::ZeroValue;

	UPROPERTY(Replicated)
	int32 Edge = 0;

	UPROPERTY(Replicated)
	TObjectPtr<APawn> Builder;

	/** Server time the piece was made: late joiners do not replay the sounds of everything that is already standing */
	UPROPERTY(Replicated)
	float BuiltServerTime = -1000.0f;
};

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FortnitePortingStyleData.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class UTexture2D;

/** A character part (hat, glasses, tail...) a style option shows */
USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingStylePart
{
	GENERATED_BODY()

	/** Component that shows the part: the outfit's own component of this part type when it has one, a new one otherwise */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName ComponentName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<USkeletalMesh> Mesh;

	/** Parts Anim Blueprint: copies the body pose (or runs the tail physics) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TSubclassOf<UAnimInstance> AnimClass;

	/** Socket of the body the part is attached to (none: it follows the body pose) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FName Socket;
};

/** A material a style option puts on one slot of a mesh, wherever that mesh is worn */
USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingStyleMaterial
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	int32 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UMaterialInterface> Material;
};

/** One choice of a channel ("Checkered", "Glasses on"...). Option 0 of every channel is what the outfit itself shows. */
USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingStyleOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FText Name;

	/** Button picture (null: the outfit's icon) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FFortnitePortingStylePart> Parts;

	/** A different body mesh (jacket on / off, other clothes); null keeps the outfit's body */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FFortnitePortingStyleMaterial> Materials;
};

/** One thing of the outfit the player can change ("Style", "Glasses", "Tail"...); the options exclude each other */
USTRUCT(BlueprintType)
struct FORTNITEPORTINGRUNTIME_API FFortnitePortingStyleChannel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FFortnitePortingStyleOption> Options;
};

/**
 * Every style of one outfit, imported next to it. The channels combine freely (tail on + glasses off + checkered):
 * the character component applies the picked option of each channel on top of the outfit's own look, so a skin with
 * hundreds of combinations is still one Blueprint.
 */
UCLASS(BlueprintType)
class FORTNITEPORTINGRUNTIME_API UFortnitePortingStyleData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TArray<FFortnitePortingStyleChannel> Channels;

	/** Channels with more than one option: the only ones the player can change */
	UFUNCTION(BlueprintPure, Category = "Style")
	bool HasChoices() const;

	/** A selection with one entry per channel, every entry in range (missing or wrong ones become the outfit's own look) */
	TArray<int32> Sanitize(const TArray<int32>& Selection) const;
};

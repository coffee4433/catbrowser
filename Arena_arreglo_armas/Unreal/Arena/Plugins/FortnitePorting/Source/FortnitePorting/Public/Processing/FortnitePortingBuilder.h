#pragma once

#include "CoreMinimal.h"
#include "Utilities/JsonWrapper.h"
#include "FortnitePortingCharacterComponent.h"

class UAnimBlueprint;
class UAnimMontage;
class UAnimSequence;
class UBlendSpace1D;
class UBlueprint;
class USkeletalMesh;
class USkeleton;
class UTexture2D;
class UFortnitePortingCharacterComponent;
class UFortnitePortingCosmeticData;

struct FImportedMesh
{
	FJsonWrapper Json;
	UObject* Object = nullptr;
	bool bIsOverride = false;
	// Style exports do not paint the shared meshes: the materials a style swaps in (slot, material) are kept here and
	// applied to the style's own Blueprint instead
	TArray<TPair<int32, class UMaterialInterface*>> SlotMaterials;
	// Epic's master skeleton for the body (has weapon_r); used for its bones, not as a visible part
	bool bIsMasterSkeleton = false;
};

/**
 * Turns an export that carries "UnrealSetup" data into ready to use assets:
 *   /Game/FortnitePorting/Characters/<Name>/{Meshes, Textures, Materials, Animations, Blueprints}
 *   /Game/FortnitePorting/{Pickaxes, Gliders, Emotes, Backpacks, Weapons, Vehicles}/<Name>/...
 *   /Game/FortnitePorting/Shared/Skeletons/SK_FP_<Profile>   (one skeleton per Fortnite body profile)
 */
class FFortnitePortingBuilder
{
public:
	/** Adds the layers the generator has gained since a skin was imported to every imported character Anim Blueprint (also run from FP.UpgradeAnimBlueprints) */
	static int32 UpgradeCharacterAnimBlueprints();
	FFortnitePortingBuilder(const FJsonWrapper& InSetup, const FString& InAssetsRoot);

	static FString GetRootPath(const FJsonWrapper& Setup);

	// Where the assets of this export go. Equal to GetRootPath, except for outfit styles: they live in
	// Characters/<Outfit>/Styles/<Style> but reuse the meshes, materials, textures and animations of the outfit itself.
	static FString GetBaseRootPath(const FJsonWrapper& Setup);
	static void FlushNotifications();

	void BuildFromMeshes(const TArray<FImportedMesh>& Meshes);
	void BuildEmote();

	/** Imports an exported sound (.wav) of the app's Assets folder as a Sound Wave in Folder */
	class USoundWave* ImportSound(const FString& GamePath, const FString& Folder, bool bLoop) const;

	/** Logs and queues an editor notification shown after the import batch */
	static void Notify(const FString& Message, bool bIsError = false);

private:
	struct FMontageSection
	{
		UAnimSequence* Sequence = nullptr;
		FName Name;
		bool bLoop = false;
	};

	// A mesh the character shows as a part (the body included), as it arrived from the app
	struct FPartInput
	{
		USkeletalMesh* Mesh = nullptr;
		int32 Type = -1;
		FJsonWrapper Json;
	};

	struct FCharacterPart
	{
		USkeletalMesh* Mesh = nullptr;
		FName ComponentName;
		FName Socket;
		UAnimBlueprint* PartsBlueprint = nullptr;
	};

	struct FBlendSample
	{
		UAnimSequence* Sequence = nullptr;
		float Speed = 0.0f;
	};

	struct FPendingNotification
	{
		FString Message;
		bool bIsError = false;
	};

	FJsonWrapper Setup;
	FString AssetsRoot;
	FString Kind;
	FString DisplayName;
	FString FolderName;
	FString RootPath;
	UTexture2D* IconTexture = nullptr;

	void ImportIcon();

	void BuildCharacter(const TArray<FImportedMesh>& Meshes);

	// One option of a style channel: its parts and materials are written into the outfit's style data asset
	void BuildStyleOption(const TArray<FImportedMesh>& Meshes);

	/** Post process Anim Blueprint that keeps sleeves on the arms of a body mesh (cleared when the body has none) */
	static void AssignBodyFix(USkeletalMesh* Body, const FString& BlueprintFolder);

	/** Components, Parts Anim Blueprints (pose copy, tail physics, blinking) for every part but SkipIndex */
	void PrepareCharacterParts(const TArray<FPartInput>& Parts, int32 SkipIndex, USkeleton* Skeleton, USkeletalMesh* Body, const FString& BlueprintFolder, const FString& NamePrefix, TArray<FCharacterPart>& OutCharacterParts);
	void BuildPickaxe(const TArray<FImportedMesh>& Meshes);
	void BuildGlider(const TArray<FImportedMesh>& Meshes);
	void BuildWeapon(const TArray<FImportedMesh>& Meshes);
	void BuildVehicle(const TArray<FImportedMesh>& Meshes);

	FJsonWrapper FindVariant(FName Profile) const;
	TMap<FString, TArray<FJsonWrapper>> GetVariantAnimations(FName Profile) const;
	UAnimSequence* ImportAnimation(const FString& GamePath, USkeleton* Skeleton, const FString& Folder) const;
	UAnimMontage* ImportMontage(const TArray<FJsonWrapper>& Entries, USkeleton* Skeleton, const FString& Folder, const FString& Name, bool bLoopLastSection, FName SlotName = NAME_None) const;
	TMap<FString, UAnimSequence*> ImportItemAnimations(USkeleton* Skeleton, const FString& Folder) const;
	template <typename T> T* FindOrCreateData() const;
	void Save() const;

	static TMap<FName, USkeleton*> GetProfileSkeletons();
	static USkeleton* EnsureProfileSkeleton(FName Profile, USkeletalMesh* Mesh);
	static bool ShareSkeleton(USkeleton* Skeleton, USkeletalMesh* Mesh);
	/** Adds weapon_r/weapon_l to Skeleton from Setup's "HandSockets" (Epic's real body skeleton) when missing */
	static void EnsureHandSockets(USkeleton* Skeleton, const FJsonWrapper& Setup);
	static void ApplyRetargetSettings(USkeleton* Skeleton);
	static UBlendSpace1D* CreateBlendSpace(const FString& Folder, const FString& Name, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, float MaxSpeed, const TArray<FBlendSample>& Samples);
	static UAnimMontage* CreateMontage(const FString& Folder, const FString& Name, USkeleton* Skeleton, const TArray<FMontageSection>& Sections, FName SlotName = NAME_None);
	static UBlueprint* CreateCharacterBlueprint(const FString& Folder, const FString& Name, USkeletalMesh* Body, UAnimBlueprint* AnimBlueprint, const TArray<FCharacterPart>& Parts, FName Profile, UAnimMontage* MantleMontage);
	static UBlueprint* FindOrCreateActorBlueprint(const FString& Folder, const FString& Name, UClass* ParentClass, bool& bOutCreated);
	static void AssignLibraryCosmetics(UFortnitePortingCharacterComponent* Component);
	static void AssignWeaponRig(UBlueprint* Blueprint, USkeletalMesh* RigMesh);
	static void AssignLocomotion(UBlueprint* Blueprint, UAnimMontage* SlideMontage, float RunSpeed, float SprintSpeedValue, float CrouchSpeedValue);
	static void SetMontageSlot(UAnimMontage* Montage, FName SlotName);
	struct FMantleAsset
	{
		UAnimMontage* Montage = nullptr;
		FFortnitePortingMantlePath Path;
	};
	static void AssignMantles(UBlueprint* Blueprint, const FMantleAsset& Low, const FMantleAsset& Mid, const FMantleAsset& High);
	static void RegisterWithCharacters(UFortnitePortingCosmeticData* Data);

	static inline TArray<FPendingNotification> PendingNotifications;
};

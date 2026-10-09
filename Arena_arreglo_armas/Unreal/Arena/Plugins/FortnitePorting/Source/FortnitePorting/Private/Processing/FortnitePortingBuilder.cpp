#include "Processing/FortnitePortingBuilder.h"
#include "Utilities/RagdollUtils.h"

#include "EditorAssetLibrary.h"
#include "FortnitePorting.h"
#include "FortnitePortingAnimInstance.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingStyleData.h"
#include "FortnitePortingVehicle.h"
#include "FortnitePortingWeapon.h"
#include "ObjectTools.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Sound/SoundWave.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/UEFAnimFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Character.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Processing/AnimBlueprintGenerator.h"
#include "Utilities/TextureUtils.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	const FString RootFolder = TEXT("/Game/FortnitePorting");
	const FString SkeletonFolder = RootFolder / TEXT("Shared/Skeletons");
	const FString SkeletonPrefix = TEXT("SK_FP_");
	const FString ThirdPersonCharacterPackage = TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter");

	// Must match the default speeds of UFortnitePortingCharacterComponent
	constexpr float WalkSpeed = 160.0f;
	constexpr float JogSpeed = 450.0f;
	constexpr float SprintSpeed = 650.0f;
	constexpr float CrouchSpeed = 250.0f;

	constexpr int32 BodyPartType = 1; // EFortCustomPartType::Body

	template <typename T>
	T* FindAsset(const FString& Folder, const FString& Name)
	{
		const FString PackagePath = Folder / Name;
		const FString ObjectPath = PackagePath + TEXT(".") + Name;
		if (T* Loaded = FindObject<T>(nullptr, *ObjectPath))
		{
			return Loaded;
		}

		return FPackageName::DoesPackageExist(PackagePath) ? LoadObject<T>(nullptr, *ObjectPath) : nullptr;
	}

	template <typename T>
	T* CreateAsset(const FString& Folder, const FString& Name)
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		T* Asset = NewObject<T>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		return Asset;
	}

	TArray<FAssetData> FindAssets(const UClass* Class, const FString& Path, bool bRecursiveClasses = true)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Path));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.bRecursiveClasses = bRecursiveClasses;

		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
		return Assets;
	}

	UClass* ResolveCharacterParentClass()
	{
		// Inherit input, camera and movement from the template character when the project has one
		if (FPackageName::DoesPackageExist(ThirdPersonCharacterPackage))
		{
			if (UClass* ThirdPersonClass = LoadClass<ACharacter>(nullptr, *(ThirdPersonCharacterPackage + TEXT(".BP_ThirdPersonCharacter_C"))))
			{
				return ThirdPersonClass;
			}
		}

		return ACharacter::StaticClass();
	}

	void CopySockets(const USkeleton* From, USkeleton* To)
	{
		if (From == nullptr || To == nullptr || From == To)
		{
			return;
		}

		for (USkeletalMeshSocket* Socket : From->Sockets)
		{
			if (Socket != nullptr && To->FindSocket(Socket->SocketName) == nullptr)
			{
				To->Sockets.Add(DuplicateObject<USkeletalMeshSocket>(Socket, To));
			}
		}
	}

	// Virtual bones live on the skeleton, not the mesh: a mesh built against a skeleton with virtual
	// bones (Epic's master skeleton has VB spine_05_weapon_r, VB root_prop...) needs them on the
	// shared skeleton too, otherwise the mesh is flagged incompatible and anim nodes hit missing bones.
	void CopyVirtualBones(const USkeleton* From, USkeleton* To)
	{
		if (From == nullptr || To == nullptr || From == To)
		{
			return;
		}

		const FReferenceSkeleton& ToRef = To->GetReferenceSkeleton();
		for (const FVirtualBone& Bone : From->GetVirtualBones())
		{
			const bool bExists = To->GetVirtualBones().ContainsByPredicate([&Bone](const FVirtualBone& Existing)
			{
				return Existing.VirtualBoneName == Bone.VirtualBoneName;
			});
			if (bExists)
			{
				continue;
			}

			if (ToRef.FindBoneIndex(Bone.SourceBoneName) == INDEX_NONE || ToRef.FindBoneIndex(Bone.TargetBoneName) == INDEX_NONE)
			{
				UE_LOG(LogFortnitePorting, Warning, TEXT("Virtual bone %s skipped on %s: %s or %s is missing"), *Bone.VirtualBoneName.ToString(), *To->GetName(), *Bone.SourceBoneName.ToString(), *Bone.TargetBoneName.ToString());
				continue;
			}

			FName NewName = Bone.VirtualBoneName;
			To->AddNewNamedVirtualBone(Bone.SourceBoneName, Bone.TargetBoneName, NewName);
		}
	}

	void SetPlayLength(UAnimSequenceBase* Animation, float Length)
	{
		// Public setter availability changes between engine versions, the reflected property does not
		if (FFloatProperty* Property = FindFProperty<FFloatProperty>(UAnimSequenceBase::StaticClass(), TEXT("SequenceLength")))
		{
			Property->SetPropertyValue_InContainer(Animation, Length);
		}
		else
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Could not update the length of %s"), *Animation->GetName());
		}
	}

	FName MakeComponentName(int32 PartType, TSet<FName>& UsedNames)
	{
		static const TCHAR* PartNames[] = { TEXT("Head"), TEXT("BodyPart"), TEXT("Hat"), TEXT("Backpack"), TEXT("Tail"), TEXT("Face"), TEXT("Gameplay") };
		const FString BaseName = PartType >= 0 && PartType < UE_ARRAY_COUNT(PartNames) ? PartNames[PartType] : TEXT("Part");

		FName Candidate(*BaseName);
		for (int32 Suffix = 2; UsedNames.Contains(Candidate); ++Suffix)
		{
			Candidate = FName(*FString::Printf(TEXT("%s%d"), *BaseName, Suffix));
		}

		UsedNames.Add(Candidate);
		return Candidate;
	}
}

FFortnitePortingBuilder::FFortnitePortingBuilder(const FJsonWrapper& InSetup, const FString& InAssetsRoot)
	: Setup(InSetup), AssetsRoot(InAssetsRoot)
{
	Kind = Setup.Get<FString>("Kind");
	DisplayName = Setup.Get<FString>("DisplayName");
	RootPath = GetRootPath(Setup);
	FolderName = FPaths::GetCleanFilename(RootPath);

	// Style assets are named <Outfit>_<Style> (their folder is only named after the style)
	if (const FJsonWrapper Style = Setup["Style"]; Style.IsValid())
	{
		FolderName = FString::Printf(TEXT("%s_%s"), *FPaths::GetCleanFilename(GetBaseRootPath(Setup)), *FPaths::GetCleanFilename(RootPath));
	}
}

FString FFortnitePortingBuilder::GetRootPath(const FJsonWrapper& Setup)
{
	const FString SetupKind = Setup.Get<FString>("Kind");
	FString Category = TEXT("Cosmetics");
	if (SetupKind == TEXT("Character")) Category = TEXT("Characters");
	else if (SetupKind == TEXT("Pickaxe")) Category = TEXT("Pickaxes");
	else if (SetupKind == TEXT("Glider")) Category = TEXT("Gliders");
	else if (SetupKind == TEXT("Emote")) Category = TEXT("Emotes");
	else if (SetupKind == TEXT("Backpack")) Category = TEXT("Backpacks");
	else if (SetupKind == TEXT("Weapon")) Category = TEXT("Weapons");
	else if (SetupKind == TEXT("Vehicle")) Category = TEXT("Vehicles");

	const FJsonWrapper Style = Setup["Style"];
	FString Folder = ObjectTools::SanitizeObjectName(Style.IsValid() ? Style.Get<FString>("BaseFolderName") : Setup.Get<FString>("FolderName"));
	if (Folder.IsEmpty())
	{
		Folder = TEXT("Unnamed");
	}

	if (Style.IsValid())
	{
		FString StyleFolder = ObjectTools::SanitizeObjectName(Style.Get<FString>("Folder"));
		if (StyleFolder.IsEmpty())
		{
			StyleFolder = TEXT("Style");
		}
		return RootFolder / Category / Folder / TEXT("Styles") / StyleFolder;
	}

	return RootFolder / Category / Folder;
}

FString FFortnitePortingBuilder::GetBaseRootPath(const FJsonWrapper& Setup)
{
	const FJsonWrapper Style = Setup["Style"];
	if (!Style.IsValid())
	{
		return GetRootPath(Setup);
	}

	FString Folder = ObjectTools::SanitizeObjectName(Style.Get<FString>("BaseFolderName"));
	if (Folder.IsEmpty())
	{
		Folder = TEXT("Unnamed");
	}
	return RootFolder / TEXT("Characters") / Folder;
}

void FFortnitePortingBuilder::Notify(const FString& Message, bool bIsError)
{
	if (bIsError)
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("%s"), *Message);
	}
	else
	{
		UE_LOG(LogFortnitePorting, Log, TEXT("%s"), *Message);
	}

	PendingNotifications.Add(FPendingNotification{ Message, bIsError });
}

void FFortnitePortingBuilder::FlushNotifications()
{
	for (const FPendingNotification& Notification : PendingNotifications)
	{
		FNotificationInfo Info(FText::FromString(Notification.Message));
		Info.ExpireDuration = Notification.bIsError ? 12.0f : 8.0f;
		Info.bFireAndForget = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(Notification.bIsError ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success);
		}
	}

	PendingNotifications.Reset();
}

void FFortnitePortingBuilder::ImportIcon()
{
	const FString GamePath = Setup["Icon"].Get<FString>("Path");
	if (GamePath.IsEmpty())
	{
		return;
	}

	const FString FilePath = FTextureUtils::FindExportedImage(AssetsRoot, GamePath);
	if (FilePath.IsEmpty())
	{
		FTextureUtils::RecordMissing(GamePath, AssetsRoot);
		return;
	}

	IconTexture = FTextureUtils::CreateTexture(FilePath, RootPath / TEXT("Icon") / FString::Printf(TEXT("T_%s_Icon"), *FolderName), true, TC_EditorIcon, true);
}

void FFortnitePortingBuilder::BuildFromMeshes(const TArray<FImportedMesh>& Meshes)
{
	const bool bIsStyle = Setup["Style"].IsValid();

	// A style has its own icon when the export carries one (the locker falls back to the outfit's icon otherwise)
	ImportIcon();

	if (Kind == TEXT("Character"))
	{
		// every skin can fall as a ragdoll when it is eliminated
		for (const FImportedMesh& Imported : Meshes)
		{
			FPRagdoll::EnsurePhysicsAsset(Cast<USkeletalMesh>(Imported.Object));
		}
		if (bIsStyle)
		{
			BuildStyleOption(Meshes);
		}
		else
		{
			BuildCharacter(Meshes);
		}
	}
	else if (Kind == TEXT("Pickaxe"))
	{
		BuildPickaxe(Meshes);
	}
	else if (Kind == TEXT("Glider"))
	{
		BuildGlider(Meshes);
	}
	else if (Kind == TEXT("Weapon"))
	{
		BuildWeapon(Meshes);
	}
	else if (Kind == TEXT("Vehicle"))
	{
		BuildVehicle(Meshes);
	}
	else
	{
		Save();
		Notify(FString::Printf(TEXT("'%s' importado en %s"), *DisplayName, *RootPath));
	}
}

FJsonWrapper FFortnitePortingBuilder::FindVariant(FName Profile) const
{
	const TArray<FJsonWrapper> Variants = Setup.GetArray("Variants");
	for (const FName Candidate : { Profile, FortnitePortingProfiles::GetMediumProfile(Profile), FortnitePortingProfiles::DefaultProfile })
	{
		for (const FJsonWrapper& Variant : Variants)
		{
			if (FName(*Variant.Get<FString>("Profile")) == Candidate)
			{
				return Variant;
			}
		}
	}

	return Variants.Num() > 0 ? Variants[0] : FJsonWrapper();
}

TMap<FString, TArray<FJsonWrapper>> FFortnitePortingBuilder::GetVariantAnimations(FName Profile) const
{
	TMap<FString, TArray<FJsonWrapper>> Slots;
	for (const FJsonWrapper& Animation : FindVariant(Profile).GetArray("Animations"))
	{
		Slots.FindOrAdd(Animation.Get<FString>("Slot")).Add(Animation);
	}

	return Slots;
}

namespace
{
	/**
	 * Fortnite animations keep the character's world travel on the root bone (a mantle's root sits ~9-19 m ahead
	 * and 50 cm up). Movement is driven by the Character here, so pin the root to its reference pose;
	 * otherwise the mesh jumps away from the capsule and seems to vanish.
	 */
	void LockRoot(UAnimSequence* Sequence)
	{
		if (Sequence == nullptr || (Sequence->bForceRootLock && Sequence->RootMotionRootLock == ERootMotionRootLock::RefPose))
		{
			return;
		}

		Sequence->Modify();
		Sequence->bForceRootLock = true;
		Sequence->RootMotionRootLock = ERootMotionRootLock::RefPose;
		Sequence->bEnableRootMotion = false;
		Sequence->MarkPackageDirty();
	}
}

UAnimSequence* FFortnitePortingBuilder::ImportAnimation(const FString& GamePath, USkeleton* Skeleton, const FString& Folder) const
{
	if (GamePath.IsEmpty() || Skeleton == nullptr)
	{
		return nullptr;
	}

	FString SourcePackage;
	FString ObjectName;
	if (!GamePath.Split(TEXT("."), &SourcePackage, &ObjectName))
	{
		SourcePackage = GamePath;
		ObjectName = FPackageName::GetShortName(GamePath);
	}

	if (UAnimSequence* Existing = FindAsset<UAnimSequence>(Folder, ObjectName))
	{
		// Imported before the shared skeleton had weapon_r, so that track was dropped: import it again
		// (replaced in place, montages and blend spaces keep pointing at it)
		static const FName WeaponBone(TEXT("weapon_r"));
		const IAnimationDataModel* DataModel = Existing->GetDataModel();
		const bool bMissingWeaponTrack = Skeleton->GetReferenceSkeleton().FindBoneIndex(WeaponBone) != INDEX_NONE
			&& DataModel != nullptr && !DataModel->IsValidBoneTrackName(WeaponBone);
		if (!bMissingWeaponTrack)
		{
			LockRoot(Existing);
			return Existing;
		}

		UE_LOG(LogFortnitePorting, Log, TEXT("Reimporting %s to add its weapon_r track"), *ObjectName);
	}

	const FString FilePath = FPaths::Combine(AssetsRoot, SourcePackage + TEXT(".ueanim"));
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("Animation file not found: %s"), *FilePath);
		return nullptr;
	}

	UEFAnimFactory* Factory = NewObject<UEFAnimFactory>();
	Factory->SettingsImporter->Skeleton = Skeleton;
	Factory->SettingsImporter->bInitialized = true;
	Factory->bImport = true;
	Factory->bImportAll = true;

	UPackage* Package = CreatePackage(*(Folder / ObjectName));
	bool bCanceled = false;
	UObject* Imported = Factory->FactoryCreateFile(UAnimSequence::StaticClass(), Package, FName(*ObjectName), RF_Public | RF_Standalone, FilePath, nullptr, GWarn, bCanceled);

	UAnimSequence* Sequence = Cast<UAnimSequence>(Imported);
	if (Sequence != nullptr)
	{
		LockRoot(Sequence);
		Sequence->MarkPackageDirty();
	}

	return Sequence;
}

namespace
{
	/** Additive pose/noise exported as a plain animation: zero scale on the pelvis, it collapses the body */
	bool IsCollapsedPose(const UAnimSequence* Sequence)
	{
		const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
		static const FName Pelvis(TEXT("pelvis"));
		if (DataModel == nullptr || !DataModel->IsValidBoneTrackName(Pelvis) || DataModel->GetNumberOfKeys() <= 0)
		{
			return false;
		}

		return DataModel->GetBoneTrackTransform(Pelvis, FFrameNumber(0)).GetScale3D().IsNearlyZero(0.01);
	}
}

UAnimMontage* FFortnitePortingBuilder::ImportMontage(const TArray<FJsonWrapper>& Entries, USkeleton* Skeleton, const FString& Folder, const FString& Name, bool bLoopLastSection, FName SlotName) const
{
	if (UAnimMontage* Existing = FindAsset<UAnimMontage>(Folder, Name))
	{
		// Sections imported before the skeleton had weapon_r are reimported in place
		for (const FJsonWrapper& Entry : Entries)
		{
			ImportAnimation(Entry.Get<FString>("Path"), Skeleton, Folder);
		}
		return Existing;
	}

	TArray<FMontageSection> Sections;
	TSet<FName> UsedNames;
	for (const FJsonWrapper& Entry : Entries)
	{
		UAnimSequence* Sequence = ImportAnimation(Entry.Get<FString>("Path"), Skeleton, Folder);
		if (Sequence == nullptr)
		{
			continue;
		}

		// Fortnite's recoil and aim layers are additive: the app flags them and they are set up as additive sequences (the exported keys are the
		// deltas themselves, which is what the AnimScaled base pose means), so they play on top of the pose below instead of replacing it
		if (Entry.Get<bool>("Additive"))
		{
			if (Sequence->AdditiveAnimType != AAT_LocalSpaceBase || Sequence->RefPoseType != ABPT_AnimScaled)
			{
				Sequence->Modify();
				Sequence->AdditiveAnimType = AAT_LocalSpaceBase;
				Sequence->RefPoseType = ABPT_AnimScaled;
				Sequence->RefFrameIndex = 0;
				Sequence->PostEditChange();
				Sequence->MarkPackageDirty();
			}
		}
		else if (IsCollapsedPose(Sequence))
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("%s is an additive pose (it collapses the body), not used in %s"), *Sequence->GetName(), *Name);
			continue;
		}

		FString SectionName = Entry.Get<FString>("Section");
		if (SectionName.IsEmpty())
		{
			SectionName = Sequence->GetName();
		}

		FName UniqueName(*SectionName);
		for (int32 Suffix = 2; UsedNames.Contains(UniqueName); ++Suffix)
		{
			UniqueName = FName(*FString::Printf(TEXT("%s_%d"), *SectionName, Suffix));
		}
		UsedNames.Add(UniqueName);

		Sections.Add({ Sequence, UniqueName, Entry.Get<bool>("Loop") });
	}

	if (Sections.IsEmpty())
	{
		return nullptr;
	}

	if (bLoopLastSection)
	{
		Sections.Last().bLoop = true;
	}

	return CreateMontage(Folder, Name, Skeleton, Sections, SlotName);
}

void FFortnitePortingBuilder::SetMontageSlot(UAnimMontage* Montage, FName SlotName)
{
	if (Montage == nullptr || Montage->SlotAnimTracks.Num() == 0 || Montage->SlotAnimTracks[0].SlotName == SlotName)
	{
		return;
	}

	Montage->Modify();
	Montage->SlotAnimTracks[0].SlotName = SlotName;
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	UE_LOG(LogFortnitePorting, Log, TEXT("%s now plays on the %s slot"), *Montage->GetName(), *SlotName.ToString());
}

TMap<FString, UAnimSequence*> FFortnitePortingBuilder::ImportItemAnimations(USkeleton* Skeleton, const FString& Folder) const
{
	TMap<FString, UAnimSequence*> Result;
	if (Skeleton == nullptr)
	{
		return Result;
	}

	for (const FJsonWrapper& Animation : Setup.GetArray("ItemAnimations"))
	{
		const FString Slot = Animation.Get<FString>("Slot");
		if (Result.Contains(Slot))
		{
			continue;
		}

		if (UAnimSequence* Sequence = ImportAnimation(Animation.Get<FString>("Path"), Skeleton, Folder))
		{
			Result.Add(Slot, Sequence);
		}
	}

	return Result;
}

template <typename T>
T* FFortnitePortingBuilder::FindOrCreateData() const
{
	const FString Folder = RootPath / TEXT("Blueprints");
	const FString Name = TEXT("DA_") + FolderName;

	T* Data = FindAsset<T>(Folder, Name);
	if (Data == nullptr)
	{
		Data = CreateAsset<T>(Folder, Name);
	}

	Data->Modify();
	Data->DisplayName = FText::FromString(DisplayName);
	if (IconTexture != nullptr)
	{
		Data->Icon = IconTexture;
	}
	Data->MarkPackageDirty();
	return Data;
}

void FFortnitePortingBuilder::Save() const
{
	UEditorAssetLibrary::SaveDirectory(RootPath, true, true);
	if (UEditorAssetLibrary::DoesDirectoryExist(SkeletonFolder))
	{
		UEditorAssetLibrary::SaveDirectory(SkeletonFolder, true, true);
	}
}

TMap<FName, USkeleton*> FFortnitePortingBuilder::GetProfileSkeletons()
{
	TMap<FName, USkeleton*> Skeletons;
	for (const FAssetData& AssetData : FindAssets(USkeleton::StaticClass(), SkeletonFolder))
	{
		const FString AssetName = AssetData.AssetName.ToString();
		if (!AssetName.StartsWith(SkeletonPrefix))
		{
			continue;
		}

		USkeleton* Skeleton = Cast<USkeleton>(AssetData.GetAsset());
		if (Skeleton != nullptr && Skeleton->GetReferenceSkeleton().GetNum() > 0)
		{
			Skeletons.Add(FName(*AssetName.RightChop(SkeletonPrefix.Len())), Skeleton);
		}
	}

	// Every body profile shares Fortnite's bone names: make the profile skeletons compatible with each other so an
	// emote/pickaxe/weapon animation made for one body type plays on every skin (Male, Female, Small, Large...),
	// also on skins imported after the item
	for (const TPair<FName, USkeleton*>& Target : Skeletons)
	{
		for (const TPair<FName, USkeleton*>& Source : Skeletons)
		{
			if (Target.Value != Source.Value && !Target.Value->GetCompatibleSkeletons().Contains(TSoftObjectPtr<USkeleton>(Source.Value)))
			{
				Target.Value->AddCompatibleSkeleton(Source.Value);
				Target.Value->MarkPackageDirty();
			}
		}
	}

	return Skeletons;
}

USkeleton* FFortnitePortingBuilder::EnsureProfileSkeleton(FName Profile, USkeletalMesh* Mesh)
{
	const FString Name = SkeletonPrefix + Profile.ToString();
	if (USkeleton* Existing = FindAsset<USkeleton>(SkeletonFolder, Name); Existing && Existing->GetReferenceSkeleton().GetNum() > 0)
	{
		return Existing;
	}

	USkeleton* Skeleton = FindAsset<USkeleton>(SkeletonFolder, Name);
	if (Skeleton == nullptr)
	{
		Skeleton = CreateAsset<USkeleton>(SkeletonFolder, Name);
	}

	if (!Skeleton->MergeAllBonesToBoneTree(Mesh))
	{
		Notify(FString::Printf(TEXT("No se pudo crear el esqueleto compartido %s, se usará el esqueleto propio de %s"), *Name, *Mesh->GetName()), true);
		return Mesh->GetSkeleton();
	}

	Skeleton->SetPreviewMesh(Mesh);
	Skeleton->MarkPackageDirty();
	return Skeleton;
}

bool FFortnitePortingBuilder::ShareSkeleton(USkeleton* Skeleton, USkeletalMesh* Mesh)
{
	if (Skeleton == nullptr || Mesh == nullptr)
	{
		return false;
	}

	USkeleton* Original = Mesh->GetSkeleton();
	if (Original == Skeleton)
	{
		return true;
	}

	if (!Skeleton->MergeAllBonesToBoneTree(Mesh))
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("%s has an incompatible hierarchy with %s and keeps its own skeleton"), *Mesh->GetName(), *Skeleton->GetName());

		// Which bones disagree: same name, different parent
		const FReferenceSkeleton& MeshRef = Mesh->GetRefSkeleton();
		const FReferenceSkeleton& SkeletonRef = Skeleton->GetReferenceSkeleton();
		int32 Reported = 0;
		for (int32 Bone = 0; Bone < MeshRef.GetRawBoneNum() && Reported < 12; ++Bone)
		{
			const int32 InSkeleton = SkeletonRef.FindBoneIndex(MeshRef.GetBoneName(Bone));
			if (InSkeleton == INDEX_NONE)
			{
				continue;
			}
			const int32 MeshParent = MeshRef.GetParentIndex(Bone);
			const int32 SkeletonParent = SkeletonRef.GetParentIndex(InSkeleton);
			const FName MeshParentName = MeshParent == INDEX_NONE ? NAME_None : MeshRef.GetBoneName(MeshParent);
			const FName SkeletonParentName = SkeletonParent == INDEX_NONE ? NAME_None : SkeletonRef.GetBoneName(SkeletonParent);
			if (MeshParentName != SkeletonParentName)
			{
				UE_LOG(LogFortnitePorting, Warning, TEXT("  bone %s: parent %s in the mesh, %s in the skeleton"), *MeshRef.GetBoneName(Bone).ToString(), *MeshParentName.ToString(), *SkeletonParentName.ToString());
				++Reported;
			}
		}
		return false;
	}

	CopySockets(Original, Skeleton);
	CopyVirtualBones(Original, Skeleton);
	Mesh->SetSkeleton(Skeleton);
	Mesh->MarkPackageDirty();
	Skeleton->MarkPackageDirty();
	return true;
}

void FFortnitePortingBuilder::EnsureHandSockets(USkeleton* Skeleton, const FJsonWrapper& Setup)
{
	if (Skeleton == nullptr)
	{
		return;
	}

	for (const FJsonWrapper& SocketData : Setup.GetArray(TEXT("HandSockets")))
	{
		const FString Name = SocketData.Get<FString>(TEXT("Name"));
		if (Name.IsEmpty())
		{
			continue;
		}

		const FName SocketName(*Name);
		if (Skeleton->FindSocket(SocketName) != nullptr)
		{
			continue;
		}

		const FName BoneName(*SocketData.Get<FString>(TEXT("Bone")));
		if (BoneName.IsNone() || Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("No se pudo crear el socket '%s': el esqueleto no tiene el hueso '%s'"), *SocketName.ToString(), *BoneName.ToString());
			continue;
		}

		USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
		NewSocket->SocketName = SocketName;
		NewSocket->BoneName = BoneName;
		NewSocket->RelativeLocation = SocketData.Get<FVector>(TEXT("Location"));
		NewSocket->RelativeRotation = SocketData.Get<FRotator>(TEXT("Rotation"));
		Skeleton->Sockets.Add(NewSocket);
		Skeleton->MarkPackageDirty();

		UE_LOG(LogFortnitePorting, Log, TEXT("Creado el socket '%s' en el hueso '%s' (tomado del esqueleto base de Fortnite)"), *SocketName.ToString(), *BoneName.ToString());
	}
}

void FFortnitePortingBuilder::ApplyRetargetSettings(USkeleton* Skeleton)
{
	// Body bones take their length from each mesh so shared/fallback animations fit every body proportion;
	// root, pelvis, IK, prop and facial bones keep their animated translation.
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	// Only raw bones: virtual bones (copied from Epic's master skeleton) are listed after them in the
	// reference skeleton but have no bone tree entry, so indexing them overflows the bone tree.
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetRawBoneNum(); ++BoneIndex)
	{
		const FString BoneName = ReferenceSkeleton.GetBoneName(BoneIndex).ToString().ToLower();
		const bool bKeepAnimation = BoneIndex == 0
			|| BoneName == TEXT("pelvis")
			|| BoneName.StartsWith(TEXT("root"))
			|| BoneName.StartsWith(TEXT("ik_"))
			|| BoneName.StartsWith(TEXT("weapon"))
			|| BoneName.StartsWith(TEXT("attach"))
			|| BoneName.StartsWith(TEXT("prop"))
			|| BoneName.StartsWith(TEXT("warp"))
			|| BoneName.StartsWith(TEXT("facial"))
			|| BoneName.StartsWith(TEXT("fn_"));

		Skeleton->SetBoneTranslationRetargetingMode(BoneIndex, bKeepAnimation ? EBoneTranslationRetargetingMode::Animation : EBoneTranslationRetargetingMode::Skeleton);
	}

	Skeleton->MarkPackageDirty();
}

namespace
{
	/**
	 * Fortnite ships some locomotion "poses" as additive deltas (zero scale and translation on every bone, e.g.
	 * Journey_Pickaxe_Sprint_Pose). Exported as plain animations they collapse the character to a point.
	 */
	bool IsAdditiveDelta(const UAnimSequence* Sequence)
	{
		const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
		static const FName Pelvis(TEXT("pelvis"));
		if (DataModel == nullptr || !DataModel->IsValidBoneTrackName(Pelvis) || DataModel->GetNumberOfKeys() <= 0)
		{
			return false;
		}

		const FTransform Key = DataModel->GetBoneTrackTransform(Pelvis, FFrameNumber(0));
		return Key.GetScale3D().IsNearlyZero(0.01);
	}

	/**
	 * Ground speed the animation was authored for, from its root bone travel (the root is locked for playback
	 * but its track is kept). Fortnite's motion matching loops carry it; in-place loops return Fallback.
	 */
	float MeasureRootSpeed(const UAnimSequence* Sequence, float Fallback)
	{
		const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
		static const FName Root(TEXT("root"));
		if (DataModel == nullptr || !DataModel->IsValidBoneTrackName(Root) || DataModel->GetNumberOfKeys() < 2 || Sequence->GetPlayLength() <= KINDA_SMALL_NUMBER)
		{
			return Fallback;
		}

		const FVector Start = DataModel->GetBoneTrackTransform(Root, FFrameNumber(0)).GetLocation();
		const FVector End = DataModel->GetBoneTrackTransform(Root, FFrameNumber(DataModel->GetNumberOfKeys() - 1)).GetLocation();
		const float Speed = static_cast<float>(FVector2D(End.X - Start.X, End.Y - Start.Y).Size()) / Sequence->GetPlayLength();
		if (Speed < 30.0f || Speed > 2000.0f)
		{
			UE_LOG(LogFortnitePorting, Log, TEXT("%s has no usable root travel (%.0f cm/s), using %.0f cm/s"), *Sequence->GetName(), Speed, Fallback);
			return Fallback;
		}

		UE_LOG(LogFortnitePorting, Log, TEXT("%s authored at %.0f cm/s"), *Sequence->GetName(), Speed);
		return Speed;
	}
}

UBlendSpace1D* FFortnitePortingBuilder::CreateBlendSpace(const FString& Folder, const FString& Name, USkeleton* Skeleton, USkeletalMesh* PreviewMesh, float MaxSpeed, const TArray<FBlendSample>& Samples)
{
	const bool bHasSamples = Samples.ContainsByPredicate([](const FBlendSample& Sample) { return Sample.Sequence != nullptr && !IsAdditiveDelta(Sample.Sequence); });
	UBlendSpace1D* Existing = FindAsset<UBlendSpace1D>(Folder, Name);
	if (Existing && bHasSamples)
	{
		// Re-sending a skin refreshes the samples, so newly picked animations and speeds reach existing characters
		Existing->Modify();
		for (int32 Index = Existing->GetNumberOfBlendSamples() - 1; Index >= 0; --Index)
		{
			Existing->DeleteSample(Index);
		}
		if (FStructProperty* AxisProperty = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters")))
		{
			FBlendParameter* Axis = AxisProperty->ContainerPtrToValuePtr<FBlendParameter>(Existing, 0);
			Axis->Max = MaxSpeed;
		}
		for (const FBlendSample& Sample : Samples)
		{
			if (Sample.Sequence != nullptr && !IsAdditiveDelta(Sample.Sequence) && Existing->AddSample(Sample.Sequence, FVector(Sample.Speed, 0.0f, 0.0f)) == INDEX_NONE)
			{
				UE_LOG(LogFortnitePorting, Warning, TEXT("Blend space %s rejected sample %s at speed %.0f"), *Name, *Sample.Sequence->GetName(), Sample.Speed);
			}
		}
		Existing->ValidateSampleData();
		Existing->ResampleData();
		Existing->PostEditChange();
		Existing->MarkPackageDirty();
		return Existing;
	}

	if (Existing)
	{
		// Repairs blend spaces generated by older versions that never built their runtime grid
		if (Existing->GetNumberOfBlendSamples() > 0)
		{
			Existing->Modify();
			for (int32 Index = Existing->GetNumberOfBlendSamples() - 1; Index >= 0; --Index)
			{
				const UAnimSequence* Sequence = Existing->GetBlendSample(Index).Animation;
				if (IsAdditiveDelta(Sequence) && Existing->GetNumberOfBlendSamples() > 1)
				{
					UE_LOG(LogFortnitePorting, Warning, TEXT("Blend space %s: removed %s (additive pose, would shrink the character)"), *Name, *GetNameSafe(Sequence));
					Existing->DeleteSample(Index);
				}
			}
			Existing->ValidateSampleData();
			Existing->ResampleData();
			Existing->PostEditChange();
			Existing->MarkPackageDirty();
		}
		return Existing;
	}

	if (!Samples.ContainsByPredicate([](const FBlendSample& Sample) { return Sample.Sequence != nullptr && !IsAdditiveDelta(Sample.Sequence); }))
	{
		return nullptr;
	}

	UBlendSpace1D* BlendSpace = CreateAsset<UBlendSpace1D>(Folder, Name);
	BlendSpace->Modify();
	BlendSpace->SetSkeleton(Skeleton);
	BlendSpace->SetPreviewMesh(PreviewMesh);

	// The axis is protected, but it is a reflected property
	if (FStructProperty* AxisProperty = FindFProperty<FStructProperty>(UBlendSpace::StaticClass(), TEXT("BlendParameters")))
	{
		FBlendParameter* Axis = AxisProperty->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 0);
		Axis->DisplayName = TEXT("Speed");
		Axis->Min = 0.0f;
		Axis->Max = MaxSpeed;
		Axis->GridNum = 4;
	}

	for (const FBlendSample& Sample : Samples)
	{
		if (IsAdditiveDelta(Sample.Sequence))
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Blend space %s: skipped %s (additive pose, would shrink the character)"), *Name, *Sample.Sequence->GetName());
		}
		else if (Sample.Sequence != nullptr)
		{
			if (BlendSpace->AddSample(Sample.Sequence, FVector(Sample.Speed, 0.0f, 0.0f)) == INDEX_NONE)
			{
				UE_LOG(LogFortnitePorting, Warning, TEXT("Blend space %s rejected sample %s at speed %.0f"), *Name, *Sample.Sequence->GetName(), Sample.Speed);
			}
		}
	}

	// AddSample only stores samples; without this the runtime grid stays empty and the blend space outputs the reference (T) pose.
	// This is what "Apply Parameter Changes" does in the blend space editor.
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();
	return BlendSpace;
}

namespace
{
	/**
	 * Fortnite mantles carry the climb on the root bone (up the ledge, then forward). The root is locked for
	 * rendering, so the climb is baked into two 0..1 curves the character component follows at runtime,
	 * which keeps hands and feet in sync with the ledge whatever its height.
	 * Returns false (no curves) for animations without a real climb, e.g. hurdles.
	 */
	FFortnitePortingMantlePath SampleMantlePath(UAnimSequence* Sequence)
	{
		FFortnitePortingMantlePath Path;
		const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
		static const FName Root(TEXT("root"));
		if (DataModel == nullptr || !DataModel->IsValidBoneTrackName(Root) || DataModel->GetNumberOfKeys() < 2)
		{
			return Path;
		}

		// Fortnite skeletons face +Y: forward travel is root Y, climb is root Z. Relative to the last key,
		// so the path ends on the ledge whatever height the animation was authored at
		const int32 NumKeys = DataModel->GetNumberOfKeys();
		const FVector End = DataModel->GetBoneTrackTransform(Root, FFrameNumber(NumKeys - 1)).GetLocation();
		for (int32 Key = 0; Key < NumKeys; ++Key)
		{
			const FVector Offset = DataModel->GetBoneTrackTransform(Root, FFrameNumber(Key)).GetLocation() - End;
			Path.Points.Add(FVector(Offset.Y, 0.0f, Offset.Z));
		}
		Path.Length = Sequence->GetPlayLength();

		UE_LOG(LogFortnitePorting, Log, TEXT("Mantle %s: %.2f s, root path starts %.0f cm back and %.0f cm below the ledge"),
			*Sequence->GetName(), Path.Length, -Path.Points[0].X, -Path.Points[0].Z);
		return Path;
	}
}

UAnimMontage* FFortnitePortingBuilder::CreateMontage(const FString& Folder, const FString& Name, USkeleton* Skeleton, const TArray<FMontageSection>& Sections, FName SlotName)
{
	if (UAnimMontage* Existing = FindAsset<UAnimMontage>(Folder, Name))
	{
		return Existing;
	}

	if (Sections.IsEmpty() || Sections[0].Sequence == nullptr)
	{
		return nullptr;
	}

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->SourceAnimation = Sections[0].Sequence;
	Factory->PreviewSkeletalMesh = Skeleton->GetPreviewMesh();

	UPackage* Package = CreatePackage(*(Folder / Name));
	UAnimMontage* Montage = Cast<UAnimMontage>(Factory->FactoryCreateNew(UAnimMontage::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (Montage == nullptr)
	{
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Montage);

	// The factory creates one segment and a "Default" section; append the remaining emote sections after it
	if (Montage->CompositeSections.Num() > 0)
	{
		Montage->CompositeSections[0].SectionName = Sections[0].Name;
	}

	float Position = Sections[0].Sequence->GetPlayLength();
	if (Montage->SlotAnimTracks.Num() > 0)
	{
		for (int32 Index = 1; Index < Sections.Num(); ++Index)
		{
			UAnimSequence* Sequence = Sections[Index].Sequence;

			FAnimSegment Segment;
			Segment.SetAnimReference(Sequence, true);
			Segment.StartPos = Position;
			Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Add(Segment);
			Montage->AddAnimCompositeSection(Sections[Index].Name, Position);

			Position += Sequence->GetPlayLength();
		}
	}

	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const int32 SectionIndex = Montage->GetSectionIndex(Sections[Index].Name);
		if (SectionIndex == INDEX_NONE)
		{
			continue;
		}

		FCompositeSection& Section = Montage->CompositeSections[SectionIndex];
		if (Sections[Index].bLoop)
		{
			Section.NextSectionName = Section.SectionName;
		}
		else
		{
			Section.NextSectionName = Sections.IsValidIndex(Index + 1) ? Sections[Index + 1].Name : NAME_None;
		}
	}

	if (!SlotName.IsNone() && Montage->SlotAnimTracks.Num() > 0)
	{
		if (SlotName == FAnimBlueprintGenerator::UpperBodySlot)
		{
			FAnimBlueprintGenerator::RegisterUpperBodySlot(Skeleton);
		}
		Montage->SlotAnimTracks[0].SlotName = SlotName;

		// Shots are short and frequent, long blends would swallow them
		Montage->BlendIn.SetBlendTime(0.1f);
		Montage->BlendOut.SetBlendTime(0.15f);
	}

	SetPlayLength(Montage, Position);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return Montage;
}

void FFortnitePortingBuilder::BuildStyleOption(const TArray<FImportedMesh>& Meshes)
{
	const FJsonWrapper Style = Setup["Style"];
	const FString StyleName = Style.Get<FString>("Name");
	const int32 ChannelIndex = Style.Get<int32>("ChannelIndex", 0);
	const int32 OptionIndex = Style.Get<int32>("OptionIndex", 0);
	const FString BaseRoot = GetBaseRootPath(Setup);
	const FString BaseName = FPaths::GetCleanFilename(BaseRoot);
	const FString BaseBlueprintName = FString::Printf(TEXT("BP_%s"), *BaseName);
	const FString BaseBlueprintPath = BaseRoot / TEXT("Blueprints") / BaseBlueprintName;

	UBlueprint* BaseBlueprint = LoadObject<UBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *BaseBlueprintPath, *BaseBlueprintName));
	if (BaseBlueprint == nullptr)
	{
		Notify(FString::Printf(TEXT("Estilo '%s' de '%s': importa primero la skin base (falta %s)"), *StyleName, *DisplayName, *BaseBlueprintName), true);
		return;
	}

	// The option's own parts are the override meshes; the outfit's meshes are only needed to know its body
	TArray<FPartInput> OptionParts;
	USkeletalMesh* Body = nullptr;
	USkeletalMesh* OptionBody = nullptr;
	for (const FImportedMesh& Imported : Meshes)
	{
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Imported.Object);
		if (Mesh == nullptr || Imported.bIsMasterSkeleton)
		{
			continue;
		}

		const int32 Type = Imported.Json.Get<int32>("Type", -1);
		if (!Imported.bIsOverride)
		{
			if (Type == BodyPartType || Body == nullptr)
			{
				Body = Mesh;
			}
			continue;
		}

		if (Type == BodyPartType)
		{
			// Another body (jacket on / off...): worn in place of the outfit's own
			OptionBody = Mesh;
			continue;
		}
		if (Type < 0)
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Estilo '%s': la malla '%s' no es una pieza de personaje y no se aplica"), *StyleName, *Mesh->GetName());
			continue;
		}

		// A later part of the same type replaces an earlier one
		OptionParts.RemoveAll([Type](const FPartInput& Part) { return Part.Type == Type; });
		OptionParts.Add({ Mesh, Type, Imported.Json });
	}

	TArray<FCharacterPart> CharacterParts;
	if (OptionParts.Num() > 0 || OptionBody != nullptr)
	{
		if (Body == nullptr)
		{
			Notify(FString::Printf(TEXT("Estilo '%s' de '%s': no se encontró el cuerpo de la skin"), *StyleName, *DisplayName), true);
			return;
		}

		const FName Profile(*Setup.Get<FString>("Profile", FortnitePortingProfiles::DefaultProfile.ToString()));
		USkeleton* Skeleton = EnsureProfileSkeleton(Profile, Body);
		if (Skeleton == nullptr)
		{
			Notify(FString::Printf(TEXT("Estilo '%s' de '%s': no se pudo preparar el esqueleto"), *StyleName, *DisplayName), true);
			return;
		}
		if (OptionBody != nullptr)
		{
			ShareSkeleton(Skeleton, OptionBody);

			AssignBodyFix(OptionBody, BaseRoot / TEXT("Blueprints"));
		}
		PrepareCharacterParts(OptionParts, INDEX_NONE, Skeleton, Body, BaseRoot / TEXT("Blueprints"), BaseName, CharacterParts);
	}

	// The outfit's style data: channels > options, written one option at a time as the exports arrive
	const FString DataFolder = BaseRoot / TEXT("Styles");
	const FString DataName = FString::Printf(TEXT("DA_%s_Styles"), *BaseName);
	UFortnitePortingStyleData* Data = FindAsset<UFortnitePortingStyleData>(DataFolder, DataName);
	if (Data == nullptr)
	{
		Data = CreateAsset<UFortnitePortingStyleData>(DataFolder, DataName);
	}
	if (Data == nullptr)
	{
		Notify(FString::Printf(TEXT("Estilo '%s' de '%s': no se pudo crear %s"), *StyleName, *DisplayName, *DataName), true);
		return;
	}

	Data->Modify();
	if (Data->Channels.Num() <= ChannelIndex)
	{
		Data->Channels.SetNum(ChannelIndex + 1);
	}
	FFortnitePortingStyleChannel& Channel = Data->Channels[ChannelIndex];
	Channel.Name = FText::FromString(Style.Get<FString>("Channel"));
	if (Channel.Options.Num() <= OptionIndex)
	{
		Channel.Options.SetNum(OptionIndex + 1);
	}

	FFortnitePortingStyleOption& Option = Channel.Options[OptionIndex];
	Option = FFortnitePortingStyleOption();
	Option.Name = FText::FromString(StyleName);
	Option.Icon = IconTexture;
	Option.BodyMesh = OptionBody;

	for (const FCharacterPart& Part : CharacterParts)
	{
		FFortnitePortingStylePart StylePart;
		StylePart.ComponentName = Part.ComponentName;
		StylePart.Mesh = Part.Mesh;
		StylePart.Socket = Part.Socket;
		if (Part.PartsBlueprint != nullptr)
		{
			StylePart.AnimClass = Part.PartsBlueprint->GeneratedClass.Get();
		}
		Option.Parts.Add(StylePart);
	}

	// Materials the option puts on the slots of any mesh the character wears (its own meshes or the option's parts)
	for (const FImportedMesh& Imported : Meshes)
	{
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Imported.Object);
		if (Mesh == nullptr)
		{
			continue;
		}

		for (const TPair<int32, UMaterialInterface*>& Slot : Imported.SlotMaterials)
		{
			FFortnitePortingStyleMaterial Entry;
			Entry.Mesh = Mesh;
			Entry.Slot = Slot.Key;
			Entry.Material = Slot.Value;
			Option.Materials.Add(Entry);
		}
	}

	Data->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Data, false);

	// The outfit's Blueprint finds its styles through its character component
	bool bAssigned = false;
	if (BaseBlueprint->SimpleConstructionScript != nullptr)
	{
		for (USCS_Node* Node : BaseBlueprint->SimpleConstructionScript->GetAllNodes())
		{
			UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr;
			if (Component != nullptr && Component->StyleData != Data)
			{
				Component->Modify();
				Component->StyleData = Data;
				bAssigned = true;
			}
		}
	}
	if (bAssigned)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BaseBlueprint);
		FKismetEditorUtilities::CompileBlueprint(BaseBlueprint);
		UEditorAssetLibrary::SaveLoadedAsset(BaseBlueprint, false);
	}

	// The option's materials and textures live in the outfit's folders, not in the style's own
	UEditorAssetLibrary::SaveDirectory(BaseRoot / TEXT("Materials"), true, true);
	UEditorAssetLibrary::SaveDirectory(BaseRoot / TEXT("Textures"), true, true);
	Save();

	Notify(FString::Printf(TEXT("Estilo '%s' > '%s' de '%s' importado (%d pieza(s), %d material(es)%s)"), *Channel.Name.ToString(), *StyleName, *DisplayName, Option.Parts.Num(), Option.Materials.Num(), OptionBody ? TEXT(", cuerpo distinto") : TEXT("")));
}

void FFortnitePortingBuilder::AssignBodyFix(USkeletalMesh* Body, const FString& BlueprintFolder)
{
	if (Body == nullptr)
	{
		return;
	}

	UAnimBlueprint* Fix = FAnimBlueprintGenerator::CreateBodyFixBlueprint(BlueprintFolder, FString::Printf(TEXT("ABP_%s_BodyFix"), *Body->GetName()), Body);
	const TSubclassOf<UAnimInstance> Wanted = Fix != nullptr ? TSubclassOf<UAnimInstance>(Fix->GeneratedClass.Get()) : TSubclassOf<UAnimInstance>();
	if (Body->GetPostProcessAnimBlueprint() != Wanted)
	{
		Body->Modify();
		Body->SetPostProcessAnimBlueprint(Wanted);
		Body->MarkPackageDirty();
	}
	if (Fix != nullptr)
	{
		UEditorAssetLibrary::SaveLoadedAsset(Fix, false);
	}
}

void FFortnitePortingBuilder::PrepareCharacterParts(const TArray<FPartInput>& Parts, int32 SkipIndex, USkeleton* Skeleton, USkeletalMesh* Body, const FString& BlueprintFolder, const FString& NamePrefix, TArray<FCharacterPart>& OutCharacterParts)
{
	TMap<USkeleton*, UAnimBlueprint*> PartsBlueprints;
	TMap<UAnimBlueprint*, TArray<TPair<USkeletalMesh*, int32>>> PartsUsers;
	TSet<FName> UsedNames;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		if (Index == SkipIndex)
		{
			continue;
		}

		FCharacterPart CharacterPart;
		CharacterPart.Mesh = Parts[Index].Mesh;
		CharacterPart.ComponentName = MakeComponentName(Parts[Index].Type, UsedNames);

		const FJsonWrapper Meta = Parts[Index].Json["Meta"];
		const FString Socket = Meta.Get<FString>("Socket");
		if (Meta.Get<bool>("AttachToSocket", false) && !Socket.IsEmpty() && !Socket.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			CharacterPart.Socket = FName(*Socket);

			// Tails attached to a socket have their own skeleton (no body bones): give them a parts ABP for the
			// spring physics, otherwise they stay rigid
			const FReferenceSkeleton& SocketBones = CharacterPart.Mesh->GetRefSkeleton();
			bool bHasTail = false;
			for (int32 Bone = 0; Bone < SocketBones.GetRawBoneNum() && !bHasTail; ++Bone)
			{
				bHasTail = SocketBones.GetBoneName(Bone).ToString().Contains(TEXT("tail"));
			}
			if (bHasTail && SocketBones.GetRawBoneNum() >= 3 && CharacterPart.Mesh->GetSkeleton())
			{
				const FString PartsName = FString::Printf(TEXT("ABP_%s_Parts_%s"), *NamePrefix, *CharacterPart.Mesh->GetName());
				UAnimBlueprint* TailBlueprint = FindAsset<UAnimBlueprint>(BlueprintFolder, PartsName);
				if (TailBlueprint == nullptr)
				{
					TailBlueprint = FAnimBlueprintGenerator::CreatePartsBlueprint(BlueprintFolder, PartsName, CharacterPart.Mesh->GetSkeleton(), CharacterPart.Mesh);
				}
				else if (TailBlueprint->TargetSkeleton != CharacterPart.Mesh->GetSkeleton())
				{
					// The tail was reimported with a new skeleton asset: an ABP targeting the old one can't resolve
					// any bone and leaves the tail frozen in its reference pose
					TailBlueprint->TargetSkeleton = CharacterPart.Mesh->GetSkeleton();
					TailBlueprint->SetPreviewMesh(CharacterPart.Mesh);
				}
				CharacterPart.PartsBlueprint = TailBlueprint;
				PartsUsers.FindOrAdd(TailBlueprint).Add({ CharacterPart.Mesh, 4 });
			}
		}
		else
		{
			ShareSkeleton(Skeleton, CharacterPart.Mesh);

			USkeleton* PartSkeleton = CharacterPart.Mesh->GetSkeleton();
			UAnimBlueprint*& PartsBlueprint = PartsBlueprints.FindOrAdd(PartSkeleton);
			if (PartsBlueprint == nullptr)
			{
				const FString PartsName = PartSkeleton == Skeleton
					? FString::Printf(TEXT("ABP_%s_Parts"), *NamePrefix)
					: FString::Printf(TEXT("ABP_%s_Parts_%s"), *NamePrefix, *CharacterPart.Mesh->GetName());

				PartsBlueprint = FindAsset<UAnimBlueprint>(BlueprintFolder, PartsName);
				if (PartsBlueprint == nullptr)
				{
					PartsBlueprint = FAnimBlueprintGenerator::CreatePartsBlueprint(BlueprintFolder, PartsName, PartSkeleton, CharacterPart.Mesh);
				}
				else if (PartsBlueprint->TargetSkeleton != PartSkeleton)
				{
					PartsBlueprint->TargetSkeleton = PartSkeleton;
					PartsBlueprint->SetPreviewMesh(CharacterPart.Mesh);
				}
			}

			CharacterPart.PartsBlueprint = PartsBlueprint;
			PartsUsers.FindOrAdd(PartsBlueprint).Add({ CharacterPart.Mesh, Parts[Index].Type });
		}

		OutCharacterParts.Add(CharacterPart);
	}

	// Parts Anim Blueprints: eye blink for the head, spring physics for tails (Fortnite animates both in the
	// lobby and in game)
	{
		TArray<FAnimBlueprintGenerator::FPartsBoneOffset> BlinkBones;
		for (const FJsonWrapper& BlinkJson : Setup.GetArray(TEXT("BlinkBones")))
		{
			FAnimBlueprintGenerator::FPartsBoneOffset Offset;
			Offset.Bone = FName(*BlinkJson.Get<FString>(TEXT("Bone")));
			Offset.Location = BlinkJson.Get<FVector>(TEXT("Location"));
			const FJsonWrapper Rotation = BlinkJson[TEXT("Rotation")];
			Offset.Rotation = FQuat(Rotation.Get<float>(TEXT("X")), Rotation.Get<float>(TEXT("Y")), Rotation.Get<float>(TEXT("Z")), Rotation.Get<float>(TEXT("W"))).GetNormalized().Rotator();
			BlinkBones.Add(Offset);
		}

		const FReferenceSkeleton& BodyBones = Body->GetRefSkeleton();
		for (const TPair<UAnimBlueprint*, TArray<TPair<USkeletalMesh*, int32>>>& Users : PartsUsers)
		{
			TArray<FAnimBlueprintGenerator::FPartsChain> Chains;
			bool bHasHead = false;
			for (const TPair<USkeletalMesh*, int32>& User : Users.Value)
			{
				bHasHead |= User.Value == 0;
				const FReferenceSkeleton& PartBones = User.Key->GetRefSkeleton();

				// Extra bones of a tail/misc part (not on the body, not facial): each chain hanging from a body bone
				bool bTailPart = User.Value == 4;
				for (int32 Bone = 0; Bone < PartBones.GetRawBoneNum() && !bTailPart; ++Bone)
				{
					bTailPart = PartBones.GetBoneName(Bone).ToString().Contains(TEXT("tail"));
				}
				if (!bTailPart)
				{
					continue;
				}

				auto IsExtra = [&](int32 Bone)
				{
					const FString Name = PartBones.GetBoneName(Bone).ToString();
					return BodyBones.FindBoneIndex(PartBones.GetBoneName(Bone)) == INDEX_NONE && !Name.StartsWith(TEXT("FACIAL_"), ESearchCase::IgnoreCase)
						&& !Name.StartsWith(TEXT("attach"), ESearchCase::IgnoreCase) && !Name.StartsWith(TEXT("ik_"), ESearchCase::IgnoreCase)
						&& !Name.Contains(TEXT("for_aim")) && !Name.Contains(TEXT("Root"));
				};

				for (int32 Bone = 0; Bone < PartBones.GetRawBoneNum(); ++Bone)
				{
					const int32 Parent = PartBones.GetParentIndex(Bone);
					if (!IsExtra(Bone) || (Parent != INDEX_NONE && IsExtra(Parent)))
					{
						continue;
					}

					// Follow the first child down to the end of the chain
					int32 End = Bone;
					for (bool bFound = true; bFound;)
					{
						bFound = false;
						for (int32 Child = End + 1; Child < PartBones.GetRawBoneNum(); ++Child)
						{
							if (PartBones.GetParentIndex(Child) == End && IsExtra(Child))
							{
								End = Child;
								bFound = true;
								break;
							}
						}
					}

					// base_/Base anchors stay fixed on the head/pelvis: simulate from the first bone below them
					int32 Start = Bone;
					if (PartBones.GetBoneName(Start).ToString().Contains(TEXT("base"), ESearchCase::IgnoreCase) && Start != End)
					{
						for (int32 Child = Start + 1; Child < PartBones.GetRawBoneNum(); ++Child)
						{
							if (PartBones.GetParentIndex(Child) == Start && IsExtra(Child))
							{
								Start = Child;
								break;
							}
						}
					}

					if (Start != Bone || End != Bone)
					{
						Chains.Add({ PartBones.GetBoneName(Start), PartBones.GetBoneName(End) });
					}
				}
			}

			FAnimBlueprintGenerator::BuildPartsGraph(Users.Key, Users.Key->TargetSkeleton ? Users.Key->TargetSkeleton.Get() : Skeleton, Chains, bHasHead ? BlinkBones : TArray<FAnimBlueprintGenerator::FPartsBoneOffset>());
			UEditorAssetLibrary::SaveLoadedAsset(Users.Key, false);
		}
	}

}

void FFortnitePortingBuilder::BuildCharacter(const TArray<FImportedMesh>& Meshes)
{
	const FName Profile(*Setup.Get<FString>("Profile", FortnitePortingProfiles::DefaultProfile.ToString()));
	const FString AnimFolder = RootPath / TEXT("Animations");
	const FString BlueprintFolder = RootPath / TEXT("Blueprints");

	using FPart = FPartInput;

	// Style parts replace the base part of the same type
	TArray<FPart> Parts;
	USkeletalMesh* MasterMesh = nullptr;
	for (const FImportedMesh& Imported : Meshes)
	{
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(Imported.Object);
		if (Mesh == nullptr)
		{
			continue;
		}

		if (Imported.bIsMasterSkeleton)
		{
			MasterMesh = Mesh;
			continue;
		}

		const int32 Type = Imported.Json.Get<int32>("Type", -1);
		if (Imported.bIsOverride && Type >= 0)
		{
			Parts.RemoveAll([Type](const FPart& Part) { return Part.Type == Type; });
		}

		Parts.Add({ Mesh, Type, Imported.Json });
	}

	if (Parts.IsEmpty())
	{
		Notify(FString::Printf(TEXT("'%s': no se importó ninguna malla esquelética"), *DisplayName), true);
		return;
	}

	int32 BodyIndex = Parts.IndexOfByPredicate([](const FPart& Part) { return Part.Type == BodyPartType; });
	if (BodyIndex == INDEX_NONE)
	{
		BodyIndex = 0;
	}
	USkeletalMesh* Body = Parts[BodyIndex].Mesh;

	// 1. Skeleton shared by every character of this body profile
	USkeleton* Skeleton = EnsureProfileSkeleton(Profile, Body);
	if (Skeleton == nullptr)
	{
		Notify(FString::Printf(TEXT("'%s': no se pudo preparar el esqueleto"), *DisplayName), true);
		return;
	}
	ShareSkeleton(Skeleton, Body);
	EnsureHandSockets(Skeleton, Setup);

	// Epic's master skeleton adds the animated weapon_r/weapon_l bones the body mesh lacks, so animations
	// keep their weapon track and the character can hold items exactly like in the game
	if (MasterMesh != nullptr && !ShareSkeleton(Skeleton, MasterMesh))
	{
		Notify(FString::Printf(TEXT("'%s': el esqueleto maestro no es compatible; el pico seguirá la mano sin el ajuste de weapon_r"), *DisplayName), true);
		MasterMesh = nullptr;
	}
	else if (MasterMesh == nullptr)
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("'%s' arrived without its master skeleton; held items will follow hand_r"), *DisplayName);
	}

	// 2. Extra parts follow the body (Copy Pose) or attach to their socket
	TArray<FCharacterPart> CharacterParts;
	PrepareCharacterParts(Parts, BodyIndex, Skeleton, Body, BlueprintFolder, FolderName, CharacterParts);

	ApplyRetargetSettings(Skeleton);

	// 3. Locomotion animations for this profile
	TMap<FString, UAnimSequence*> Animations;
	for (const FJsonWrapper& Animation : FindVariant(Profile).GetArray("Animations"))
	{
		if (UAnimSequence* Sequence = ImportAnimation(Animation.Get<FString>("Path"), Skeleton, AnimFolder))
		{
			Animations.Add(Animation.Get<FString>("Slot"), Sequence);
		}
	}

	auto GetAnimation = [&Animations](const TCHAR* Slot) -> UAnimSequence*
	{
		UAnimSequence** Found = Animations.Find(Slot);
		UAnimSequence* Sequence = Found ? *Found : nullptr;
		if (IsAdditiveDelta(Sequence))
		{
			// Additive deltas (zero scale) collapse the character to a point when played as a full pose
			UE_LOG(LogFortnitePorting, Warning, TEXT("Animation %s for slot %s is an additive pose, not used"), *Sequence->GetName(), Slot);
			return nullptr;
		}
		return Sequence;
	};

	// 4. Blend spaces, placed at the speeds Fortnite authored each loop for (no foot sliding), which also become
	// the character's run/sprint speeds
	const float AnimWalkSpeed = MeasureRootSpeed(GetAnimation(TEXT("Walk")), WalkSpeed);
	const float AnimJogSpeed = FMath::Max(MeasureRootSpeed(GetAnimation(TEXT("Jog")), JogSpeed), AnimWalkSpeed + 100.0f);
	const float AnimSprintSpeed = FMath::Max(MeasureRootSpeed(GetAnimation(TEXT("Sprint")), SprintSpeed), AnimJogSpeed + 80.0f);
	const float AnimCrouchSpeed = MeasureRootSpeed(GetAnimation(TEXT("CrouchWalk")), CrouchSpeed);
	UE_LOG(LogFortnitePorting, Log, TEXT("%s speeds: walk %.0f, run %.0f, sprint %.0f, crouch %.0f cm/s"), *FolderName, AnimWalkSpeed, AnimJogSpeed, AnimSprintSpeed, AnimCrouchSpeed);

	FLocomotionAnimSet AnimSet;
	AnimSet.Idle = GetAnimation(TEXT("Idle"));
	AnimSet.Locomotion = CreateBlendSpace(AnimFolder, FString::Printf(TEXT("BS_%s_Locomotion"), *FolderName), Skeleton, Body, AnimSprintSpeed, {
		FBlendSample{ GetAnimation(TEXT("Idle")), 0.0f },
		FBlendSample{ GetAnimation(TEXT("Walk")), AnimWalkSpeed },
		FBlendSample{ GetAnimation(TEXT("Jog")), AnimJogSpeed },
		FBlendSample{ GetAnimation(TEXT("Sprint")), AnimSprintSpeed }
	});
	AnimSet.Crouch = CreateBlendSpace(AnimFolder, FString::Printf(TEXT("BS_%s_Crouch"), *FolderName), Skeleton, Body, FMath::Max(300.0f, AnimCrouchSpeed + 50.0f), {
		FBlendSample{ GetAnimation(TEXT("CrouchIdle")), 0.0f },
		FBlendSample{ GetAnimation(TEXT("CrouchWalk")), AnimCrouchSpeed }
	});
	AnimSet.Pickaxe = CreateBlendSpace(AnimFolder, FString::Printf(TEXT("BS_%s_Pickaxe"), *FolderName), Skeleton, Body, AnimSprintSpeed, {
		FBlendSample{ GetAnimation(TEXT("PickaxeIdle")), 0.0f },
		FBlendSample{ GetAnimation(TEXT("PickaxeWalk")), AnimWalkSpeed },
		FBlendSample{ GetAnimation(TEXT("PickaxeJog")), AnimJogSpeed },
		FBlendSample{ GetAnimation(TEXT("PickaxeSprint")), AnimSprintSpeed }
	});

	// Fortnite's knee slide: KneeSlideInitiate once, then KneeSliding looping until the slide ends
	UAnimMontage* SlideMontage = nullptr;
	{
		TArray<FMontageSection> SlideSections;
		if (UAnimSequence* SlideStart = GetAnimation(TEXT("SlideStart")))
		{
			SlideSections.Add({ SlideStart, FName(TEXT("Start")), false });
		}
		if (UAnimSequence* SlideLoop = GetAnimation(TEXT("Slide")))
		{
			SlideSections.Add({ SlideLoop, FName(TEXT("Loop")), true });
		}
		const bool bLoops = SlideSections.Num() > 0 && SlideSections.Last().bLoop;
		SlideMontage = bLoops ? CreateMontage(AnimFolder, FString::Printf(TEXT("AM_%s_Slide"), *FolderName), Skeleton, SlideSections) : nullptr;
		if (SlideMontage && !FMath::IsNearlyEqual(SlideMontage->BlendIn.GetBlendTime(), 0.12f))
		{
			SlideMontage->Modify();
			SlideMontage->BlendIn.SetBlendTime(0.12f);
			SlideMontage->BlendOut.SetBlendTime(0.25f);
			SlideMontage->MarkPackageDirty();
		}
	}
	AnimSet.JumpStart = GetAnimation(TEXT("JumpStart"));
	AnimSet.JumpStartRun = GetAnimation(TEXT("JumpStartRun"));
	AnimSet.JumpLoop = GetAnimation(TEXT("JumpLoop"));
	AnimSet.Land = GetAnimation(TEXT("Land"));
	if (AnimSet.Land && AnimSet.Land->GetName().Contains(TEXT("Anticipation")))
	{
		// "Land_Anticipation" is the mid-air reach for the ground; played after touching down it looks like a second jump
		AnimSet.Land = nullptr;
	}

	// Fortnite mantles by ledge height: Hurdle_Mantle_50Up / 100Up / 200Up
	auto MakeMantle = [&](const TCHAR* Slot, const TCHAR* Suffix, FFortnitePortingMantlePath& OutPath) -> UAnimMontage*
	{
		UAnimSequence* Mantle = GetAnimation(Slot);
		if (Mantle == nullptr)
		{
			return nullptr;
		}

		// The hurdle is only the climb (~0.2 s): Fortnite follows it with Hurdle_Mantle_LandToSprint_*, the
		// landing that flows back into running. Both play back to back in one montage.
		TArray<FMontageSection> Sections = { FMontageSection{ Mantle, FName(TEXT("Default")), false } };
		if (UAnimSequence* Land = GetAnimation(*FString::Printf(TEXT("%sLand"), Slot)))
		{
			Sections.Add(FMontageSection{ Land, FName(TEXT("Land")), false });
		}

		OutPath = SampleMantlePath(Mantle);
		UAnimMontage* MantleMontage = CreateMontage(AnimFolder, FString::Printf(TEXT("AM_%s_%s"), *FolderName, Suffix), Skeleton, Sections);

		// A montage from an older export may play other animations or miss the landing: rebuild its segments in place
		if (MantleMontage && MantleMontage->SlotAnimTracks.Num() > 0)
		{
			TArray<FAnimSegment>& Segments = MantleMontage->SlotAnimTracks[0].AnimTrack.AnimSegments;
			bool bMatches = Segments.Num() == Sections.Num();
			for (int32 Index = 0; bMatches && Index < Sections.Num(); ++Index)
			{
				bMatches = Segments[Index].GetAnimReference() == Sections[Index].Sequence;
			}
			if (!bMatches)
			{
				MantleMontage->Modify();
				Segments.Reset();
				MantleMontage->CompositeSections.Reset();
				float Position = 0.0f;
				for (const FMontageSection& Section : Sections)
				{
					FAnimSegment Segment;
					Segment.SetAnimReference(Section.Sequence, true);
					Segment.StartPos = Position;
					Segments.Add(Segment);
					MantleMontage->AddAnimCompositeSection(Section.Name, Position);
					Position += Section.Sequence->GetPlayLength();
				}
				for (int32 Index = 0; Index < Sections.Num(); ++Index)
				{
					const int32 SectionIndex = MantleMontage->GetSectionIndex(Sections[Index].Name);
					if (SectionIndex != INDEX_NONE)
					{
						MantleMontage->CompositeSections[SectionIndex].NextSectionName = Sections.IsValidIndex(Index + 1) ? Sections[Index + 1].Name : NAME_None;
					}
				}
				SetPlayLength(MantleMontage, Position);
				MantleMontage->PostEditChange();
				MantleMontage->MarkPackageDirty();
				UE_LOG(LogFortnitePorting, Log, TEXT("%s now plays %s%s"), *MantleMontage->GetName(), *Mantle->GetName(),
					Sections.Num() > 1 ? *FString::Printf(TEXT(" + %s"), *Sections[1].Sequence->GetName()) : TEXT(""));
			}
		}
		// The climb is short, so it blends in quickly; the landing blends out softly into locomotion
		if (MantleMontage && (!FMath::IsNearlyEqual(MantleMontage->BlendIn.GetBlendTime(), 0.1f) || !FMath::IsNearlyEqual(MantleMontage->BlendOut.GetBlendTime(), 0.3f)))
		{
			MantleMontage->Modify();
			MantleMontage->BlendIn.SetBlendTime(0.1f);
			MantleMontage->BlendOut.SetBlendTime(0.3f);
			MantleMontage->MarkPackageDirty();
		}
		return MantleMontage;
	};
	FFortnitePortingMantlePath MantlePath, MantlePathLow, MantlePathHigh;
	UAnimMontage* MantleMontage = MakeMantle(TEXT("Mantle"), TEXT("Mantle"), MantlePath);
	UAnimMontage* MantleLowMontage = MakeMantle(TEXT("MantleLow"), TEXT("MantleLow"), MantlePathLow);
	UAnimMontage* MantleHighMontage = MakeMantle(TEXT("MantleHigh"), TEXT("MantleHigh"), MantlePathHigh);

	// 5. Anim Blueprint + Character Blueprint
	const FString AnimBlueprintName = FString::Printf(TEXT("ABP_%s"), *FolderName);
	UAnimBlueprint* AnimBlueprint = FindAsset<UAnimBlueprint>(BlueprintFolder, AnimBlueprintName);
	if (AnimBlueprint == nullptr)
	{
		AnimBlueprint = FAnimBlueprintGenerator::CreateLocomotionBlueprint(BlueprintFolder, AnimBlueprintName, Skeleton, Body, AnimSet);
	}
	else
	{
		// Re-sending a skin rebuilds its generated Anim Blueprint graph so layout fixes reach existing characters
		FAnimBlueprintGenerator::RebuildLocomotionBlueprint(AnimBlueprint, AnimSet);
	}

	// Sleeves follow the arms (Fortnite's rig does it with a constraint nothing replaces here)
	AssignBodyFix(Body, BlueprintFolder);

	const FString BlueprintName = FString::Printf(TEXT("BP_%s"), *FolderName);
	UBlueprint* CharacterBlueprint = CreateCharacterBlueprint(BlueprintFolder, BlueprintName, Body, AnimBlueprint, CharacterParts, Profile, MantleMontage);
	AssignWeaponRig(CharacterBlueprint, MasterMesh);
	AssignMantles(CharacterBlueprint, { MantleLowMontage, MantlePathLow }, { MantleMontage, MantlePath }, { MantleHighMontage, MantlePathHigh });
	AssignLocomotion(CharacterBlueprint, SlideMontage, AnimJogSpeed, AnimSprintSpeed, AnimCrouchSpeed);

	// The outfit's own lobby idle, looped by the lobby / locker
	if (UAnimSequence* LobbyIdle = GetAnimation(TEXT("LobbyIdle")))
	{
		UAnimMontage* LobbyMontage = CreateMontage(AnimFolder, FString::Printf(TEXT("AM_%s_Lobby"), *FolderName), Skeleton, { FMontageSection{ LobbyIdle, FName(TEXT("Loop")), true } });
		if (LobbyMontage && CharacterBlueprint && CharacterBlueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : CharacterBlueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr)
				{
					Component->Modify();
					Component->LobbyIdleMontage = LobbyMontage;
				}
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(CharacterBlueprint);
			FKismetEditorUtilities::CompileBlueprint(CharacterBlueprint);
			UEditorAssetLibrary::SaveLoadedAsset(CharacterBlueprint, false);
		}
	}

	// A re-sent outfit starts with no styles: the options that follow in the same batch fill them again
	if (UFortnitePortingStyleData* OldStyles = FindAsset<UFortnitePortingStyleData>(RootPath / TEXT("Styles"), FString::Printf(TEXT("DA_%s_Styles"), *FolderName)))
	{
		OldStyles->Modify();
		OldStyles->Channels.Reset();
		OldStyles->MarkPackageDirty();
	}

	Save();

	TArray<FString> Missing;
	for (const TCHAR* Slot : { TEXT("Idle"), TEXT("Walk"), TEXT("Jog"), TEXT("Sprint"), TEXT("CrouchIdle"), TEXT("CrouchWalk"), TEXT("JumpStart"), TEXT("JumpLoop"), TEXT("Land"), TEXT("Mantle"), TEXT("Slide"), TEXT("PickaxeIdle") })
	{
		if (GetAnimation(Slot) == nullptr)
		{
			Missing.Add(Slot);
		}
	}

	Notify(FString::Printf(TEXT("Personaje '%s' (%s) creado en %s. Usa %s como Default Pawn Class.%s"),
		*DisplayName, *Profile.ToString(), *RootPath, CharacterBlueprint ? *CharacterBlueprint->GetName() : TEXT("el Blueprint"),
		Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Animaciones no encontradas: %s"), *FString::Join(Missing, TEXT(", ")))));
}

UBlueprint* FFortnitePortingBuilder::CreateCharacterBlueprint(const FString& Folder, const FString& Name, USkeletalMesh* Body, UAnimBlueprint* AnimBlueprint, const TArray<FCharacterPart>& Parts, FName Profile, UAnimMontage* MantleMontage)
{
	if (UBlueprint* Existing = FindAsset<UBlueprint>(Folder, Name))
	{
		Notify(FString::Printf(TEXT("%s ya existe y no se ha modificado (bórralo para regenerarlo)"), *Name));
		return Existing;
	}

	UClass* ParentClass = ResolveCharacterParentClass();
	UPackage* Package = CreatePackage(*(Folder / Name));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("FortnitePorting"));
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
	{
		Notify(FString::Printf(TEXT("No se pudo crear %s"), *Name), true);
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Blueprint);

	const ACharacter* ParentDefaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	USimpleConstructionScript* ConstructionScript = Blueprint->SimpleConstructionScript;
	if (ParentDefaults != nullptr && ConstructionScript != nullptr)
	{
		for (const FCharacterPart& Part : Parts)
		{
			USCS_Node* Node = ConstructionScript->CreateNode(USkeletalMeshComponent::StaticClass(), Part.ComponentName);
			if (USkeletalMeshComponent* Template = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
			{
				Template->SetSkeletalMeshAsset(Part.Mesh);
				if (Part.PartsBlueprint && Part.PartsBlueprint->GeneratedClass)
				{
					Template->SetAnimInstanceClass(Part.PartsBlueprint->GeneratedClass.Get());
				}
			}

			Node->SetParent(ParentDefaults->GetMesh());
			if (!Part.Socket.IsNone())
			{
				Node->AttachToName = Part.Socket;
			}
			ConstructionScript->AddNode(Node);
		}

		USCS_Node* ComponentNode = ConstructionScript->CreateNode(UFortnitePortingCharacterComponent::StaticClass(), TEXT("FortnitePorting"));
		if (UFortnitePortingCharacterComponent* Component = Cast<UFortnitePortingCharacterComponent>(ComponentNode->ComponentTemplate))
		{
			Component->SkeletonProfile = Profile;
			Component->MantleMontage = MantleMontage;
			AssignLibraryCosmetics(Component);
		}
		ConstructionScript->AddNode(ComponentNode);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// The body lives on the inherited CharacterMesh0, so it is configured on the compiled defaults
	if (ACharacter* Defaults = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject()))
	{
		USkeletalMeshComponent* Mesh = Defaults->GetMesh();
		Mesh->SetSkeletalMeshAsset(Body);
		if (AnimBlueprint != nullptr && AnimBlueprint->GeneratedClass != nullptr)
		{
			Mesh->SetAnimInstanceClass(AnimBlueprint->GeneratedClass.Get());
		}

		if (ParentClass == ACharacter::StaticClass())
		{
			Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f));
			Mesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
		}

		Defaults->MarkPackageDirty();
	}

	Blueprint->MarkPackageDirty();
	return Blueprint;
}

void FFortnitePortingBuilder::AssignMantles(UBlueprint* Blueprint, const FMantleAsset& Low, const FMantleAsset& Mid, const FMantleAsset& High)
{
	if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr || (Low.Montage == nullptr && Mid.Montage == nullptr && High.Montage == nullptr))
	{
		return;
	}

	bool bChanged = false;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr;
		if (Component == nullptr)
		{
			continue;
		}

		Component->Modify();
		if (Mid.Montage)
		{
			Component->MantleMontage = Mid.Montage;
			Component->MantlePath = Mid.Path;
		}
		Component->MantleMontageLow = Low.Montage;
		Component->MantlePathLow = Low.Path;
		Component->MantleMontageHigh = High.Montage;
		Component->MantlePathHigh = High.Path;
		bChanged = true;
	}

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false);
	}
}

void FFortnitePortingBuilder::AssignLocomotion(UBlueprint* Blueprint, UAnimMontage* SlideMontage, float RunSpeed, float SprintSpeedValue, float CrouchSpeedValue)
{
	if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
	{
		return;
	}

	bool bChanged = false;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr;
		if (Component == nullptr)
		{
			continue;
		}

		Component->Modify();
		if (SlideMontage)
		{
			Component->SlideMontage = SlideMontage;
		}
		Component->JogSpeed = RunSpeed;
		Component->SprintSpeed = SprintSpeedValue;
		Component->CrouchSpeed = CrouchSpeedValue;
		bChanged = true;
	}

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false);
	}
}

void FFortnitePortingBuilder::AssignWeaponRig(UBlueprint* Blueprint, USkeletalMesh* RigMesh)
{
	if (Blueprint == nullptr || RigMesh == nullptr || Blueprint->SimpleConstructionScript == nullptr)
	{
		return;
	}

	bool bChanged = false;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr;
		if (Component == nullptr || Component->WeaponRigMesh == RigMesh)
		{
			continue;
		}

		Component->Modify();
		Component->WeaponRigMesh = RigMesh;
		bChanged = true;
	}

	if (bChanged)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false);
	}
}

void FFortnitePortingBuilder::AssignLibraryCosmetics(UFortnitePortingCharacterComponent* Component)
{
	for (const FAssetData& AssetData : FindAssets(UFortnitePortingEmoteData::StaticClass(), RootFolder / TEXT("Emotes")))
	{
		if (UFortnitePortingEmoteData* Emote = Cast<UFortnitePortingEmoteData>(AssetData.GetAsset()))
		{
			Component->Emotes.AddUnique(Emote);
		}
	}

	if (Component->Pickaxe == nullptr)
	{
		for (const FAssetData& AssetData : FindAssets(UFortnitePortingPickaxeData::StaticClass(), RootFolder / TEXT("Pickaxes")))
		{
			Component->Pickaxe = Cast<UFortnitePortingPickaxeData>(AssetData.GetAsset());
			if (Component->Pickaxe) break;
		}
	}

	if (Component->Glider == nullptr)
	{
		for (const FAssetData& AssetData : FindAssets(UFortnitePortingGliderData::StaticClass(), RootFolder / TEXT("Gliders")))
		{
			Component->Glider = Cast<UFortnitePortingGliderData>(AssetData.GetAsset());
			if (Component->Glider) break;
		}
	}

	for (const FAssetData& AssetData : FindAssets(UFortnitePortingWeaponData::StaticClass(), RootFolder / TEXT("Weapons")))
	{
		if (UFortnitePortingWeaponData* Weapon = Cast<UFortnitePortingWeaponData>(AssetData.GetAsset()))
		{
			Component->Weapons.AddUnique(Weapon);
		}
	}
}

void FFortnitePortingBuilder::RegisterWithCharacters(UFortnitePortingCosmeticData* Data)
{
	if (Data == nullptr)
	{
		return;
	}

	int32 UpdatedCount = 0;
	for (const FAssetData& AssetData : FindAssets(UBlueprint::StaticClass(), RootFolder / TEXT("Characters"), false))
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
		{
			continue;
		}

		bool bChanged = false;
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			UFortnitePortingCharacterComponent* Component = Node ? Cast<UFortnitePortingCharacterComponent>(Node->ComponentTemplate) : nullptr;
			if (Component == nullptr)
			{
				continue;
			}

			Component->Modify();
			if (UFortnitePortingEmoteData* Emote = Cast<UFortnitePortingEmoteData>(Data))
			{
				if (!Component->Emotes.Contains(Emote))
				{
					Component->Emotes.Add(Emote);
					bChanged = true;
				}
			}
			else if (UFortnitePortingPickaxeData* Pickaxe = Cast<UFortnitePortingPickaxeData>(Data))
			{
				bChanged |= Component->Pickaxe != Pickaxe;
				Component->Pickaxe = Pickaxe;
			}
			else if (UFortnitePortingGliderData* Glider = Cast<UFortnitePortingGliderData>(Data))
			{
				bChanged |= Component->Glider != Glider;
				Component->Glider = Glider;
			}
			else if (UFortnitePortingWeaponData* Weapon = Cast<UFortnitePortingWeaponData>(Data))
			{
				if (!Component->Weapons.Contains(Weapon))
				{
					Component->Weapons.Add(Weapon);
					bChanged = true;
				}
			}
		}

		if (bChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			UEditorAssetLibrary::SaveLoadedAsset(Blueprint, false);
			UpdatedCount++;
		}
	}

	if (UpdatedCount > 0)
	{
		Notify(FString::Printf(TEXT("'%s' asignado a %d personaje(s)"), *Data->DisplayName.ToString(), UpdatedCount));
	}
}

void FFortnitePortingBuilder::BuildPickaxe(const TArray<FImportedMesh>& Meshes)
{
	UFortnitePortingPickaxeData* Data = FindOrCreateData<UFortnitePortingPickaxeData>();

	// Styled pickaxes arrive as override meshes only
	const bool bHasBaseMeshes = Meshes.ContainsByPredicate([](const FImportedMesh& Mesh) { return !Mesh.bIsOverride && Mesh.Object != nullptr; });
	Data->Meshes.Reset();
	for (const FImportedMesh& Imported : Meshes)
	{
		UStreamableRenderAsset* Mesh = Cast<UStreamableRenderAsset>(Imported.Object);
		if (Mesh == nullptr || (bHasBaseMeshes && Imported.bIsOverride))
		{
			continue;
		}

		FFortnitePortingAttachedMesh Entry;
		Entry.Mesh = Mesh;
		Entry.Socket = Data->Meshes.Num() == 0 ? FName(TEXT("weapon_r")) : FName(TEXT("weapon_l"));
		Data->Meshes.Add(Entry);
	}

	// Grip detected by the app: two handed (default), one handed or dual wield. Montage names carry it so a pickaxe
	// sent again with the right animations never reuses montages built with the old ones.
	const FString StyleString = Setup.Get<FString>("ItemType");
	Data->Style = FName(StyleString.IsEmpty() ? TEXT("TwoHanded") : *StyleString);
	const FString StyleSuffix = Data->IsTwoHanded() ? FString() : Data->Style.ToString();
	Data->EquipMontages.Reset();
	Data->SwingMontages.Reset();
	Data->HoldMontages.Reset();

	const TMap<FName, USkeleton*> Skeletons = GetProfileSkeletons();
	for (const auto& [Profile, Skeleton] : Skeletons)
	{
		const TMap<FString, TArray<FJsonWrapper>> Slots = GetVariantAnimations(Profile);
		const FString Folder = RootPath / TEXT("Animations") / Profile.ToString();

		// A montage that already exists is reused, so the animation it plays is part of its name: sending the pickaxe again with a
		// better equip animation (its own one instead of the generic) builds a new montage instead of keeping the old one
		const auto AnimTag = [](const TArray<FJsonWrapper>& Entries)
		{
			return Entries.IsEmpty() ? FString() : TEXT("_") + FPaths::GetBaseFilename(Entries[0].Get<FString>("Path"));
		};

		if (const TArray<FJsonWrapper>* Equip = Slots.Find(TEXT("PickaxeEquip")))
		{
			if (UAnimMontage* Montage = ImportMontage(*Equip, Skeleton, Folder, FString::Printf(TEXT("AM_%s_Equip%s%s_%s"), *FolderName, *StyleSuffix, *AnimTag(*Equip), *Profile.ToString()), false, FAnimBlueprintGenerator::UpperBodySlot))
			{
				SetMontageSlot(Montage, FAnimBlueprintGenerator::UpperBodySlot);
				Data->EquipMontages.Add(Profile, Montage);
			}
		}

		// One handed / dual pickaxes hold their own idle pose (two handed ones use the character's pickaxe pose)
		if (const TArray<FJsonWrapper>* Idle = Slots.Find(TEXT("PickaxeIdle")); Idle && !Data->IsTwoHanded())
		{
			if (UAnimMontage* Montage = ImportMontage(*Idle, Skeleton, Folder, FString::Printf(TEXT("AM_%s_Hold%s%s_%s"), *FolderName, *StyleSuffix, *AnimTag(*Idle), *Profile.ToString()), true, FAnimBlueprintGenerator::UpperBodySlot))
			{
				SetMontageSlot(Montage, FAnimBlueprintGenerator::UpperBodySlot);
				Data->HoldMontages.Add(Profile, Montage);
			}
		}

		// Fortnite's harvesting combo: Swing1..Swing4 (two handed: swing + left arm follow-up) as separate sections of
		// one montage that never flow into each other; the character component picks the section per click
		TArray<FJsonWrapper> Entries;
		int32 SwingCount = 0;
		for (const TCHAR* SwingSlot : { TEXT("PickaxeSwing"), TEXT("PickaxeSwing2"), TEXT("PickaxeSwing3"), TEXT("PickaxeSwing4") })
		{
			if (const TArray<FJsonWrapper>* Swing = Slots.Find(SwingSlot))
			{
				Entries.Append(*Swing);
				SwingCount++;
			}
		}

		if (!Entries.IsEmpty())
		{
			const FString MontageName = FString::Printf(TEXT("AM_%s_%s%s_%s"), *FolderName, SwingCount > 1 ? TEXT("SwingCombo") : TEXT("Swing"), *StyleSuffix, *Profile.ToString());

			// Upper body only, so the legs keep walking while swinging (Fortnite layers it the same way)
			if (UAnimMontage* Montage = ImportMontage(Entries, Skeleton, Folder, MontageName, false, FAnimBlueprintGenerator::UpperBodySlot))
			{
				SetMontageSlot(Montage, FAnimBlueprintGenerator::UpperBodySlot);
				bool bChanged = false;
				for (FCompositeSection& Section : Montage->CompositeSections)
				{
					if (!Section.NextSectionName.IsNone())
					{
						Section.NextSectionName = NAME_None;
						bChanged = true;
					}
				}
				if (bChanged)
				{
					Montage->MarkPackageDirty();
				}
				Data->SwingMontages.Add(Profile, Montage);
			}
		}
	}

	Data->MarkPackageDirty();
	RegisterWithCharacters(Data);
	Save();

	Notify(Skeletons.Num() == 0
		? FString::Printf(TEXT("Pico '%s' importado. Importa una skin y vuelve a enviar el pico para generar sus animaciones."), *DisplayName)
		: FString::Printf(TEXT("Pico '%s' creado en %s (%d perfiles de esqueleto)"), *DisplayName, *RootPath, Skeletons.Num()),
		Skeletons.Num() == 0);
}

void FFortnitePortingBuilder::BuildGlider(const TArray<FImportedMesh>& Meshes)
{
	UFortnitePortingGliderData* Data = FindOrCreateData<UFortnitePortingGliderData>();

	for (const FImportedMesh& Imported : Meshes)
	{
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Imported.Object))
		{
			Data->Glider.Mesh = Mesh;
			break;
		}
	}

	const TMap<FName, USkeleton*> Skeletons = GetProfileSkeletons();
	for (const auto& [Profile, Skeleton] : Skeletons)
	{
		const TMap<FString, TArray<FJsonWrapper>> Slots = GetVariantAnimations(Profile);
		const TArray<FJsonWrapper>* Glide = Slots.Find(TEXT("Glide"));
		if (Glide == nullptr)
		{
			Glide = Slots.Find(TEXT("Skydive"));
		}

		if (Glide == nullptr)
		{
			continue;
		}

		const FString Folder = RootPath / TEXT("Animations") / Profile.ToString();
		if (UAnimMontage* Montage = ImportMontage(*Glide, Skeleton, Folder, FString::Printf(TEXT("AM_%s_Glide_%s"), *FolderName, *Profile.ToString()), true))
		{
			Data->GlideMontages.Add(Profile, Montage);
		}
	}

	Data->MarkPackageDirty();
	RegisterWithCharacters(Data);
	Save();

	Notify(Skeletons.Num() == 0
		? FString::Printf(TEXT("Glider '%s' importado. Importa una skin y vuelve a enviar el glider para generar sus animaciones."), *DisplayName)
		: FString::Printf(TEXT("Glider '%s' creado en %s"), *DisplayName, *RootPath),
		Skeletons.Num() == 0);
}

void FFortnitePortingBuilder::BuildEmote()
{
	ImportIcon();

	const TMap<FName, USkeleton*> Skeletons = GetProfileSkeletons();
	if (Skeletons.Num() == 0)
	{
		Notify(FString::Printf(TEXT("Emote '%s': importa primero una skin para que exista un esqueleto al que aplicarlo."), *DisplayName), true);
		return;
	}

	UFortnitePortingEmoteData* Data = FindOrCreateData<UFortnitePortingEmoteData>();
	for (const auto& [Profile, Skeleton] : Skeletons)
	{
		const TMap<FString, TArray<FJsonWrapper>> Slots = GetVariantAnimations(Profile);
		const TArray<FJsonWrapper>* Sections = Slots.Find(TEXT("Emote"));
		if (Sections == nullptr)
		{
			continue;
		}

		const FString Folder = RootPath / TEXT("Animations") / Profile.ToString();
		if (UAnimMontage* Montage = ImportMontage(*Sections, Skeleton, Folder, FString::Printf(TEXT("AM_%s_%s"), *FolderName, *Profile.ToString()), false))
		{
			Data->MontagesByProfile.Add(Profile, Montage);
		}
	}

	// Music and voice lines, started at their montage time
	Data->Sounds.Reset();
	for (const FJsonWrapper& SoundJson : Setup.GetArray("Sounds"))
	{
		const bool bLoop = SoundJson.Get<bool>("Loop");
		if (USoundWave* Wave = ImportSound(SoundJson.Get<FString>("Path"), RootPath / TEXT("Sounds"), bLoop))
		{
			FFortnitePortingEmoteSound Entry;
			Entry.Sound = Wave;
			Entry.Time = SoundJson.Get<float>("Time");
			Entry.bLoop = bLoop;
			Data->Sounds.Add(Entry);
		}
	}
	Data->Sounds.Sort([](const FFortnitePortingEmoteSound& A, const FFortnitePortingEmoteSound& B) { return A.Time < B.Time; });

	// Detect the loop: which section repeats, which track sets the pace and the dance speed that keeps both together.
	// The montages and the master wave are then marked as looping for good, so nothing has to be tuned per song.
	Data->bLoopAnalyzed = false;
	for (const auto& [Profile, Montage] : Data->MontagesByProfile)
	{
		if (Montage != nullptr)
		{
			Data->AnalyzeMusicLoop(Montage);
			break;
		}
	}

	if (Data->bLoopsWithMusic)
	{
		for (const auto& [Profile, Montage] : Data->MontagesByProfile)
		{
			const int32 SectionIndex = Montage ? Montage->GetSectionIndex(Data->LoopSection) : INDEX_NONE;
			if (SectionIndex != INDEX_NONE && Montage->CompositeSections[SectionIndex].NextSectionName != Data->LoopSection)
			{
				Montage->CompositeSections[SectionIndex].NextSectionName = Data->LoopSection;
				Montage->MarkPackageDirty();
			}
		}

		if (Data->Sounds.IsValidIndex(Data->MasterSound))
		{
			if (USoundWave* MasterWave = Cast<USoundWave>(Data->Sounds[Data->MasterSound].Sound); MasterWave != nullptr && !MasterWave->bLooping)
			{
				MasterWave->bLooping = true;
				MasterWave->MarkPackageDirty();
			}
		}

		UE_LOG(LogFortnitePorting, Log, TEXT("Emote %s: loop section '%s', master track %d, dance speed x%.3f"),
			*DisplayName, *Data->LoopSection.ToString(), Data->MasterSound, Data->DancePlayRate);
	}

	Data->MarkPackageDirty();
	RegisterWithCharacters(Data);
	Save();

	Notify(FString::Printf(TEXT("Emote '%s' creado en %s (%d perfiles, %d sonido(s)). Funciona con todas las skins."), *DisplayName, *RootPath, Data->MontagesByProfile.Num(), Data->Sounds.Num()));
}

USoundWave* FFortnitePortingBuilder::ImportSound(const FString& GamePath, const FString& Folder, bool bLoop) const
{
	FString SourcePackage;
	FString ObjectName;
	if (!GamePath.Split(TEXT("."), &SourcePackage, &ObjectName, ESearchCase::IgnoreCase, ESearchDir::FromEnd) || SourcePackage.Contains(TEXT("/")) == false)
	{
		SourcePackage = GamePath;
	}
	ObjectName = FPackageName::GetShortName(SourcePackage);
	if (ObjectName.IsEmpty())
	{
		return nullptr;
	}

	USoundWave* Wave = FindAsset<USoundWave>(Folder, ObjectName);
	if (Wave == nullptr)
	{
		FString FilePath;
		for (const TCHAR* Extension : { TEXT(".wav"), TEXT(".ogg"), TEXT(".flac"), TEXT(".mp3") })
		{
			const FString Candidate = FPaths::Combine(AssetsRoot, SourcePackage + Extension);
			if (FPaths::FileExists(Candidate))
			{
				FilePath = Candidate;
				break;
			}
		}

		if (FilePath.IsEmpty())
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Sound file not found for %s under %s"), *GamePath, *AssetsRoot);
			return nullptr;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = FilePath;
		Task->DestinationPath = Folder;
		Task->DestinationName = ObjectName;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = false;
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks({ Task });

		for (UObject* Imported : Task->GetObjects())
		{
			Wave = Cast<USoundWave>(Imported);
			if (Wave != nullptr)
			{
				break;
			}
		}
	}

	if (Wave != nullptr && Wave->bLooping != bLoop)
	{
		Wave->Modify();
		Wave->bLooping = bLoop;
		Wave->MarkPackageDirty();
	}

	return Wave;
}

UBlueprint* FFortnitePortingBuilder::FindOrCreateActorBlueprint(const FString& Folder, const FString& Name, UClass* ParentClass, bool& bOutCreated)
{
	bOutCreated = false;
	if (UBlueprint* Existing = FindAsset<UBlueprint>(Folder, Name); Existing && Existing->GeneratedClass)
	{
		return Existing;
	}

	UPackage* Package = CreatePackage(*(Folder / Name));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("FortnitePorting"));
	if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr)
	{
		Notify(FString::Printf(TEXT("No se pudo crear %s"), *Name), true);
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	bOutCreated = true;
	return Blueprint;
}

int32 FFortnitePortingBuilder::UpgradeCharacterAnimBlueprints()
{
	for (const TPair<FName, USkeleton*>& Pair : GetProfileSkeletons())
	{
		FAnimBlueprintGenerator::RegisterUpperBodySlot(Pair.Value);
	}

	int32 UpgradedCount = 0;
	for (const FAssetData& AssetData : FindAssets(UAnimBlueprint::StaticClass(), RootFolder / TEXT("Characters")))
	{
		UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
		if (AnimBlueprint == nullptr || AnimBlueprint->ParentClass == nullptr || !AnimBlueprint->ParentClass->IsChildOf(UFortnitePortingAnimInstance::StaticClass()))
		{
			continue;
		}

		const bool bRepaired = FAnimBlueprintGenerator::RepairUpperBodyLayer(AnimBlueprint);
		if (FAnimBlueprintGenerator::EnsureUpperBodyLayer(AnimBlueprint) || bRepaired)
		{
			UEditorAssetLibrary::SaveLoadedAsset(AnimBlueprint, false);
			UpgradedCount++;
		}
	}

	return UpgradedCount;
}

void FFortnitePortingBuilder::BuildWeapon(const TArray<FImportedMesh>& Meshes)
{
	UFortnitePortingWeaponData* Data = FindOrCreateData<UFortnitePortingWeaponData>();
	const FString ItemType = Setup.Get<FString>("ItemType");
	Data->WeaponType = FName(ItemType.IsEmpty() ? TEXT("Rifle") : *ItemType);
	Data->ApplyTypeDefaults();

	// Styled weapons arrive as override meshes only
	const bool bHasBaseMeshes = Meshes.ContainsByPredicate([](const FImportedMesh& Mesh) { return !Mesh.bIsOverride && Mesh.Object != nullptr; });
	Data->Meshes.Reset();
	for (const FImportedMesh& Imported : Meshes)
	{
		UStreamableRenderAsset* Mesh = Cast<UStreamableRenderAsset>(Imported.Object);
		if (Mesh == nullptr || (bHasBaseMeshes && Imported.bIsOverride))
		{
			continue;
		}

		FFortnitePortingAttachedMesh Entry;
		Entry.Mesh = Mesh;
		Entry.Socket = Data->Meshes.Num() == 0 ? FName(TEXT("weapon_r")) : FName(TEXT("weapon_l"));
		Data->Meshes.Add(Entry);
	}

	if (Data->Meshes.IsEmpty())
	{
		Notify(FString::Printf(TEXT("Arma '%s': no se importó ninguna malla"), *DisplayName), true);
		return;
	}

	// Animations of the weapon mesh itself (slide, magazine, bow string...)
	if (USkeletalMesh* WeaponMesh = Cast<USkeletalMesh>(Data->Meshes[0].Mesh))
	{
		const TMap<FString, UAnimSequence*> ItemAnimations = ImportItemAnimations(WeaponMesh->GetSkeleton(), RootPath / TEXT("Animations") / TEXT("Weapon"));
		auto Find = [&ItemAnimations](const TCHAR* Slot) -> UAnimSequence*
		{
			UAnimSequence* const* Found = ItemAnimations.Find(Slot);
			return Found ? *Found : nullptr;
		};

		Data->WeaponIdleAnimation = Find(TEXT("Idle"));
		Data->WeaponFireAnimation = Find(TEXT("Fire"));
		Data->WeaponReloadAnimation = Find(TEXT("Reload"));
		Data->WeaponEquipAnimation = Find(TEXT("Equip"));
	}

	// Player animations for every body profile already in the project, on the UpperBody slot
	const TMap<FName, USkeleton*> Skeletons = GetProfileSkeletons();
	for (const TPair<FName, USkeleton*>& Pair : Skeletons)
	{
		const FName Profile = Pair.Key;
		USkeleton* Skeleton = Pair.Value;
		const TMap<FString, TArray<FJsonWrapper>> Slots = GetVariantAnimations(Profile);
		const FString Folder = RootPath / TEXT("Animations") / Profile.ToString();

		auto ImportSlot = [&](const TCHAR* Slot, const TCHAR* Suffix, bool bLoop, TMap<FName, TObjectPtr<UAnimMontage>>& Target)
		{
			const TArray<FJsonWrapper>* Entries = Slots.Find(Slot);
			if (Entries == nullptr)
			{
				return;
			}

			// Fortnite's recoil and aim are additive layers: they go in the slot of their own (other group), so playing them does not stop the hold pose
			const bool bAdditive = Entries->ContainsByPredicate([](const FJsonWrapper& Entry) { return Entry.Get<bool>("Additive"); });
			if (bAdditive)
			{
				FAnimBlueprintGenerator::RegisterAdditiveSlot(Skeleton);
			}
			// the animation it plays is part of the name: sending the weapon again with better animations makes a new montage instead of keeping the old one
			const FString AnimTag = Entries->IsEmpty() ? FString() : TEXT("_") + FPaths::GetBaseFilename((*Entries)[0].Get<FString>("Path"));
			const FString Name = FString::Printf(TEXT("AM_%s_%s%s%s_%s"), *FolderName, Suffix, bAdditive ? TEXT("Add") : TEXT(""), *AnimTag, *Profile.ToString());
			if (UAnimMontage* Montage = ImportMontage(*Entries, Skeleton, Folder, Name, bLoop, bAdditive ? FAnimBlueprintGenerator::AdditiveSlot : FAnimBlueprintGenerator::UpperBodySlot))
			{
				Target.Add(Profile, Montage);
			}
		};

		ImportSlot(TEXT("WeaponIdle"), TEXT("Hold"), true, Data->HoldMontages);
		ImportSlot(TEXT("WeaponJog"), TEXT("Jog"), true, Data->JogMontages);
		ImportSlot(TEXT("WeaponFire"), TEXT("Fire"), false, Data->FireMontages);
		ImportSlot(TEXT("WeaponAim"), TEXT("Aim"), true, Data->AimMontages);
		ImportSlot(TEXT("WeaponReload"), TEXT("Reload"), false, Data->ReloadMontages);
		ImportSlot(TEXT("WeaponEquip"), TEXT("Equip"), false, Data->EquipMontages);
	}

	// BP_<Weapon>: world pickup and the actor spawned in the hand
	bool bCreated = false;
	if (UBlueprint* Blueprint = FindOrCreateActorBlueprint(RootPath / TEXT("Blueprints"), TEXT("BP_") + FolderName, AFortnitePortingWeapon::StaticClass(), bCreated))
	{
		if (AFortnitePortingWeapon* Defaults = Cast<AFortnitePortingWeapon>(Blueprint->GeneratedClass->GetDefaultObject()))
		{
			Defaults->Modify();
			Defaults->WeaponData = Data;
			Defaults->ApplyWeaponData();
			Defaults->MarkPackageDirty();
		}

		Data->WeaponActorClass = Blueprint->GeneratedClass.Get();
		Blueprint->MarkPackageDirty();
	}

	Data->MarkPackageDirty();
	UpgradeCharacterAnimBlueprints();
	RegisterWithCharacters(Data);
	Save();

	if (Skeletons.Num() == 0)
	{
		Notify(FString::Printf(TEXT("Arma '%s' importada sin animaciones de jugador: envía primero una skin y vuelve a enviar el arma."), *DisplayName), true);
		return;
	}

	TArray<FString> Missing;
	if (Data->HoldMontages.IsEmpty()) Missing.Add(TEXT("sujetar"));
	if (Data->FireMontages.IsEmpty()) Missing.Add(TEXT("disparar"));
	if (Data->ReloadMontages.IsEmpty() && Data->MagazineSize > 0) Missing.Add(TEXT("recargar"));

	Notify(FString::Printf(TEXT("Arma '%s' (%s) creada en %s. Tecla 2 para sacarla, clic para disparar, R para recargar.%s"),
		*DisplayName, *Data->WeaponType.ToString(), *RootPath,
		Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Animaciones no encontradas: %s"), *FString::Join(Missing, TEXT(", ")))));
}

void FFortnitePortingBuilder::BuildVehicle(const TArray<FImportedMesh>& Meshes)
{
	UFortnitePortingVehicleData* Data = FindOrCreateData<UFortnitePortingVehicleData>();
	const bool bFirstBuild = Data->VehicleClass == nullptr;

	// Body: the first skeletal mesh, otherwise the first static mesh; the rest become attached parts
	int32 BodyIndex = Meshes.IndexOfByPredicate([](const FImportedMesh& Mesh) { return Cast<USkeletalMesh>(Mesh.Object) != nullptr; });
	if (BodyIndex == INDEX_NONE)
	{
		BodyIndex = Meshes.IndexOfByPredicate([](const FImportedMesh& Mesh) { return Cast<UStaticMesh>(Mesh.Object) != nullptr; });
	}
	if (BodyIndex == INDEX_NONE)
	{
		Notify(FString::Printf(TEXT("Vehículo '%s': no se importó ninguna malla"), *DisplayName), true);
		return;
	}

	USkeletalMesh* SkeletalBody = Cast<USkeletalMesh>(Meshes[BodyIndex].Object);
	UStaticMesh* StaticBody = Cast<UStaticMesh>(Meshes[BodyIndex].Object);

	// Only two- and four-wheelers can be driven; anything else (boats, tanks, helicopters, carts...) is not built
	int32 WheelCount = 0;
	if (const TSharedPtr<FJsonObject> Object = Setup.GetObject())
	{
		Object->TryGetNumberField(TEXT("WheelCount"), WheelCount);
	}
	if (WheelCount != 2 && WheelCount != 4)
	{
		Notify(FString::Printf(TEXT("Vehículo '%s' no soportado: tiene %d ruedas (solo se admiten vehículos de 2 o 4 ruedas)"), *DisplayName, WheelCount), true);
		return;
	}
	Data->WheelCount = WheelCount;
	Data->MaxLeanAngle = WheelCount == 2 ? 12.0f : 0.0f;
	if (bFirstBuild)
	{
		Data->TurnRate = WheelCount == 2 ? 140.0f : 85.0f;
	}

	// Seats: sockets reported by the app, then anything seat-like on the imported skeleton
	Data->SeatSockets.Reset();
	TArray<FString> SocketNames;
	if (const TSharedPtr<FJsonObject> Object = Setup.GetObject())
	{
		Object->TryGetStringArrayField(TEXT("Sockets"), SocketNames);
	}
	for (const FString& SocketName : SocketNames)
	{
		Data->SeatSockets.AddUnique(FName(*SocketName));
	}

	if (SkeletalBody && SkeletalBody->GetSkeleton())
	{
		auto IsSeat = [](const FString& Name) { return Name.Contains(TEXT("seat")) || Name.Contains(TEXT("driver")); };
		for (const USkeletalMeshSocket* Socket : SkeletalBody->GetSkeleton()->Sockets)
		{
			if (Socket && IsSeat(Socket->SocketName.ToString()))
			{
				Data->SeatSockets.AddUnique(Socket->SocketName);
			}
		}

		const FReferenceSkeleton& ReferenceSkeleton = SkeletalBody->GetRefSkeleton();
		for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
		{
			if (IsSeat(ReferenceSkeleton.GetBoneName(BoneIndex).ToString()))
			{
				Data->SeatSockets.AddUnique(ReferenceSkeleton.GetBoneName(BoneIndex));
			}
		}
	}

	// Without a seat, sit in the middle of the vehicle slightly above its floor
	if (bFirstBuild)
	{
		const FBoxSphereBounds Bounds = SkeletalBody ? SkeletalBody->GetBounds() : StaticBody->GetBounds();
		if (Data->SeatSockets.IsEmpty())
		{
			Data->DriverOffset.SetLocation(FVector(0.0f, 0.0f, Bounds.Origin.Z - Bounds.BoxExtent.Z + 35.0f));
		}

		// Long side along Y means the mesh is authored facing Y instead of X
		if (Bounds.BoxExtent.Y > Bounds.BoxExtent.X * 1.3f)
		{
			Data->MeshYawOffset = -90.0f;
		}
	}

	if (SkeletalBody)
	{
		const TMap<FString, UAnimSequence*> ItemAnimations = ImportItemAnimations(SkeletalBody->GetSkeleton(), RootPath / TEXT("Animations") / TEXT("Vehicle"));
		if (UAnimSequence* const* Idle = ItemAnimations.Find(TEXT("VehicleIdle")))
		{
			Data->VehicleIdleAnimation = *Idle;
		}
	}

	// Driver/passenger animations for every body profile already in the project
	const TMap<FName, USkeleton*> Skeletons = GetProfileSkeletons();
	for (const TPair<FName, USkeleton*>& Pair : Skeletons)
	{
		const FName Profile = Pair.Key;
		USkeleton* Skeleton = Pair.Value;
		const TMap<FString, TArray<FJsonWrapper>> Slots = GetVariantAnimations(Profile);
		const FString Folder = RootPath / TEXT("Animations") / Profile.ToString();

		auto ImportSlot = [&](const TCHAR* Slot, const TCHAR* Suffix, bool bLoop, TMap<FName, TObjectPtr<UAnimMontage>>& Target)
		{
			if (const TArray<FJsonWrapper>* Entries = Slots.Find(Slot))
			{
				if (UAnimMontage* Montage = ImportMontage(*Entries, Skeleton, Folder, FString::Printf(TEXT("AM_%s_%s_%s"), *FolderName, Suffix, *Profile.ToString()), bLoop))
				{
					Target.Add(Profile, Montage);
				}
			}
		};

		ImportSlot(TEXT("VehicleDriverIdle"), TEXT("Driver"), true, Data->DriverMontages);
		ImportSlot(TEXT("VehicleDriverDrive"), TEXT("DriverDrive"), true, Data->DriverDriveMontages);
		ImportSlot(TEXT("VehicleDriverLeft"), TEXT("DriverLeft"), true, Data->DriverLeftMontages);
		ImportSlot(TEXT("VehicleDriverRight"), TEXT("DriverRight"), true, Data->DriverRightMontages);
		ImportSlot(TEXT("VehicleDriverReverse"), TEXT("DriverReverse"), true, Data->DriverReverseMontages);
		ImportSlot(TEXT("VehicleDriverBrake"), TEXT("DriverBrake"), true, Data->DriverBrakeMontages);
		ImportSlot(TEXT("VehiclePassengerDrive"), TEXT("PassengerDrive"), true, Data->PassengerDriveMontages);
		ImportSlot(TEXT("VehiclePassengerIdle"), TEXT("Passenger"), true, Data->PassengerMontages);
		ImportSlot(TEXT("VehicleEnter"), TEXT("Enter"), false, Data->EnterMontages);
		ImportSlot(TEXT("VehicleExit"), TEXT("Exit"), false, Data->ExitMontages);
	}

	bool bCreated = false;
	UBlueprint* Blueprint = FindOrCreateActorBlueprint(RootPath / TEXT("Blueprints"), TEXT("BP_") + FolderName, AFortnitePortingVehicle::StaticClass(), bCreated);
	if (Blueprint != nullptr)
	{
		const AFortnitePortingVehicle* ParentDefaults = Cast<AFortnitePortingVehicle>(Blueprint->GeneratedClass->GetDefaultObject());
		USimpleConstructionScript* ConstructionScript = Blueprint->SimpleConstructionScript;
		if (bCreated && ParentDefaults && ConstructionScript)
		{
			USceneComponent* AttachParent = SkeletalBody ? static_cast<USceneComponent*>(ParentDefaults->VehicleMesh.Get()) : static_cast<USceneComponent*>(ParentDefaults->VehicleStaticMesh.Get());
			int32 PartIndex = 1;
			for (int32 Index = 0; Index < Meshes.Num(); ++Index)
			{
				UObject* PartMesh = Meshes[Index].Object;
				if (Index == BodyIndex || PartMesh == nullptr || Meshes[Index].bIsOverride)
				{
					continue;
				}

				const bool bSkeletal = PartMesh->IsA<USkeletalMesh>();
				USCS_Node* Node = ConstructionScript->CreateNode(bSkeletal ? USkeletalMeshComponent::StaticClass() : UStaticMeshComponent::StaticClass(), FName(*FString::Printf(TEXT("Part%d"), PartIndex++)));
				if (USkeletalMeshComponent* SkeletalTemplate = Cast<USkeletalMeshComponent>(Node->ComponentTemplate))
				{
					SkeletalTemplate->SetSkeletalMeshAsset(Cast<USkeletalMesh>(PartMesh));
				}
				else if (UStaticMeshComponent* StaticTemplate = Cast<UStaticMeshComponent>(Node->ComponentTemplate))
				{
					StaticTemplate->SetStaticMesh(Cast<UStaticMesh>(PartMesh));
				}

				if (UPrimitiveComponent* Template = Cast<UPrimitiveComponent>(Node->ComponentTemplate))
				{
					Template->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Template->SetRelativeLocation(Meshes[Index].Json.Get<FVector>("Location", FVector::ZeroVector));
					Template->SetRelativeRotation(Meshes[Index].Json.Get<FRotator>("Rotation", FRotator::ZeroRotator));
					Template->SetRelativeScale3D(Meshes[Index].Json.Get<FVector>("Scale", FVector::OneVector));
				}

				Node->SetParent(AttachParent);
				ConstructionScript->AddNode(Node);
			}

			FKismetEditorUtilities::CompileBlueprint(Blueprint);
		}

		if (AFortnitePortingVehicle* Defaults = Cast<AFortnitePortingVehicle>(Blueprint->GeneratedClass->GetDefaultObject()))
		{
			Defaults->Modify();
			if (SkeletalBody)
			{
				Defaults->VehicleMesh->SetSkeletalMeshAsset(SkeletalBody);
			}
			else
			{
				Defaults->VehicleStaticMesh->SetStaticMesh(StaticBody);
			}
			Defaults->VehicleData = Data;
			Defaults->ApplyVehicleData();
			Defaults->MarkPackageDirty();
		}

		// Without this compile, actors dragged into a level start without their VehicleData
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		Data->VehicleClass = Blueprint->GeneratedClass.Get();
		Blueprint->MarkPackageDirty();
	}

	Data->MarkPackageDirty();
	Save();

	TArray<FString> Missing;
	if (Data->DriverMontages.IsEmpty()) Missing.Add(TEXT("conducir"));
	if (Data->EnterMontages.IsEmpty()) Missing.Add(TEXT("subir"));
	if (Data->ExitMontages.IsEmpty()) Missing.Add(TEXT("bajar"));

	Notify(FString::Printf(TEXT("Vehículo '%s' creado en %s. Arrastra BP_%s al nivel, acércate y pulsa E para subir (WASD conduce, E baja).%s%s"),
		*DisplayName, *RootPath, *FolderName,
		Skeletons.Num() == 0 ? TEXT(" Envía primero una skin y vuelve a enviar el vehículo para generar las animaciones del conductor.") : TEXT(""),
		Skeletons.Num() > 0 && !Missing.IsEmpty() ? *FString::Printf(TEXT(" Animaciones no encontradas: %s"), *FString::Join(Missing, TEXT(", "))) : TEXT("")),
		Skeletons.Num() == 0);
}

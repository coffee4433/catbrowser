#define LOCTEXT_NAMESPACE "FFortnitePortingModule"
#include "FortnitePorting.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "FortnitePortingCosmeticData.h"
#include "Processing/FortnitePortingBuilder.h"
#include "Rendering/SkeletalMeshModel.h"
#include "StaticMeshResources.h"
#include "Utilities/RagdollUtils.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/UEFAnimFactory.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#include "Classes/BuildingTextureData.h"
#include "Renderers/BuildingTextureDataThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"

DEFINE_LOG_CATEGORY(LogFortnitePorting);

namespace
{
	/**
	 * FP.ImportAnim <file.ueanim> <skeleton asset path> <destination folder> [name]
	 * Imports one animation FortnitePorting exported to a folder (animations that are not emotes are not sent to the editor) onto a skeleton.
	 */
	void ImportRawAnimation(const TArray<FString>& Args)
	{
		if (Args.Num() < 3)
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("FP.ImportAnim <file.ueanim> <skeleton asset path> <destination folder> [name]"));
			return;
		}
		const FString& File = Args[0];
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *Args[1]);
		const FString Folder = Args[2];
		const FString Name = Args.Num() > 3 ? Args[3] : FPaths::GetBaseFilename(File);
		if (!Skeleton || !FPaths::FileExists(File))
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("FP.ImportAnim: skeleton or file not found (%s / %s)"), *Args[1], *File);
			return;
		}

		UEFAnimFactory* Factory = NewObject<UEFAnimFactory>();
		Factory->SettingsImporter->Skeleton = Skeleton;
		Factory->SettingsImporter->bInitialized = true;
		Factory->bImport = true;
		Factory->bImportAll = true;

		const FString PackageName = Folder / Name;
		UPackage* Package = CreatePackage(*PackageName);
		bool bCanceled = false;
		UObject* Imported = Factory->FactoryCreateFile(UAnimSequence::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, File, nullptr, GWarn, bCanceled);
		UAnimSequence* Sequence = Cast<UAnimSequence>(Imported);
		if (!Sequence)
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("FP.ImportAnim: %s did not import"), *File);
			return;
		}
		FAssetRegistryModule::AssetCreated(Sequence);
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Sequence, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
		UE_LOG(LogFortnitePorting, Log, TEXT("FP.ImportAnim: %s imported (%.2f s)"), *PackageName, Sequence->GetPlayLength());
	}

	/** The barrel tip of a gun mesh: the far end of its longest axis, the upper part of that end (the barrel sits above the magazine tube) */
	bool FindMuzzle(UStreamableRenderAsset* Mesh, FVector& OutOffset, FVector& OutDirection)
	{
		TArray<FVector> Points;
		if (USkeletalMesh* Skeletal = Cast<USkeletalMesh>(Mesh))
		{
			if (const FSkeletalMeshModel* Model = Skeletal->GetImportedModel(); Model && Model->LODModels.Num() > 0)
			{
				for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
				{
					for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
					{
						Points.Add(FVector(Vertex.Position));
					}
				}
			}
		}
		else if (UStaticMesh* Static = Cast<UStaticMesh>(Mesh))
		{
			if (const FStaticMeshRenderData* Render = Static->GetRenderData(); Render && Render->LODResources.Num() > 0)
			{
				const FPositionVertexBuffer& Buffer = Render->LODResources[0].VertexBuffers.PositionVertexBuffer;
				for (uint32 Index = 0; Index < Buffer.GetNumVertices(); ++Index)
				{
					Points.Add(FVector(Buffer.VertexPosition(Index)));
				}
			}
		}
		if (Points.Num() < 8)
		{
			return false;
		}
		FBox Box(Points);
		const FVector Size = Box.GetSize();
		const int32 Axis = (Size.X >= Size.Y && Size.X >= Size.Z) ? 0 : (Size.Y >= Size.Z ? 1 : 2);
		const float Sign = Box.Max[Axis] > -Box.Min[Axis] ? 1.0f : -1.0f;       // the barrel is on the side that reaches farther from the grip (the origin)
		const float Tip = Sign > 0.0f ? Box.Max[Axis] : Box.Min[Axis];
		FBox Cluster(ForceInit);
		for (const FVector& P : Points)
		{
			if (Sign * (P[Axis] - Tip) >= -2.5f)
			{
				Cluster += P;
			}
		}
		const float TopZ = Cluster.Max.Z;
		const float Cut = TopZ - Cluster.GetSize().Z * 0.55f;
		FBox Barrel(ForceInit);
		for (const FVector& P : Points)
		{
			if (Sign * (P[Axis] - Tip) >= -2.5f && P.Z >= Cut)
			{
				Barrel += P;
			}
		}
		if (!Barrel.IsValid)
		{
			Barrel = Cluster;
		}
		OutOffset = Barrel.GetCenter();
		OutOffset[Axis] = Tip + Sign * 1.0f;
		OutDirection = FVector::ZeroVector;
		OutDirection[Axis] = Sign;
		return true;
	}

	/** FP.FitMuzzles: finds the barrel tip of every imported weapon and stores it in its data asset */
	void FitMuzzles(const TArray<FString>& Args)
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByPath(FName(Args.Num() > 0 ? *Args[0] : TEXT("/Game/FortnitePorting/Weapons")), Assets, true);
		int32 Fitted = 0;
		for (const FAssetData& Data : Assets)
		{
			UFortnitePortingWeaponData* Weapon = Cast<UFortnitePortingWeaponData>(Data.GetAsset());
			if (!Weapon || Weapon->Meshes.IsEmpty())
			{
				continue;
			}
			FVector Offset, Direction;
			if (FindMuzzle(Weapon->Meshes[0].Mesh, Offset, Direction))
			{
				Weapon->MuzzleOffset = Offset;
				Weapon->MuzzleDirection = Direction;
				Weapon->bMuzzleFitted = true;
				Weapon->MarkPackageDirty();
				UPackage* Package = Weapon->GetOutermost();
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				UPackage::SavePackage(Package, Weapon, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), SaveArgs);
				UE_LOG(LogFortnitePorting, Log, TEXT("FP.FitMuzzles: %s muzzle at %s direction %s"), *Weapon->GetName(), *Offset.ToString(), *Direction.ToString());
				++Fitted;
			}
		}
		UE_LOG(LogFortnitePorting, Log, TEXT("FP.FitMuzzles: %d weapons"), Fitted);
	}

	FAutoConsoleCommand FitMuzzlesCommand(TEXT("FP.FitMuzzles"), TEXT("FP.FitMuzzles [folder]: finds the barrel tip of every imported weapon"), FConsoleCommandWithArgsDelegate::CreateStatic(&FitMuzzles));

	FAutoConsoleCommand MakePhysicsAssetsCommand(TEXT("FP.MakePhysicsAssets"), TEXT("FP.MakePhysicsAssets [folder]: gives every imported skin a physics asset so it can ragdoll"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const int32 Made = FPRagdoll::EnsureForFolder(Args.Num() > 0 ? Args[0] : TEXT("/Game/FortnitePorting/Characters"));
			UE_LOG(LogFortnitePorting, Log, TEXT("FP.MakePhysicsAssets: %d physics assets made"), Made);
		}));

	FAutoConsoleCommand UpgradeAnimBlueprintsCommand(TEXT("FP.UpgradeAnimBlueprints"), TEXT("FP.UpgradeAnimBlueprints: adds the layers the Anim Blueprint generator has gained since a skin was imported (upper body, hand IK, weapon twist)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>&)
		{
			const int32 Upgraded = FFortnitePortingBuilder::UpgradeCharacterAnimBlueprints();
			UE_LOG(LogFortnitePorting, Log, TEXT("FP.UpgradeAnimBlueprints: %d Anim Blueprints upgraded"), Upgraded);
		}));

	FAutoConsoleCommand ImportAnimCommand(TEXT("FP.ImportAnim"), TEXT("FP.ImportAnim <file.ueanim> <skeleton asset path> <destination folder> [name]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&ImportRawAnimation));
}

void FFortnitePortingModule::StartupModule()
{
	ListenServer = new FListenServer();
	
	UThumbnailManager::Get().RegisterCustomRenderer(
		UBuildingTextureData::StaticClass(), 
		UBuildingTextureDataThumbnailRenderer::StaticClass()
	);
}

void FFortnitePortingModule::ShutdownModule()
{
	delete ListenServer;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FFortnitePortingModule, FortnitePorting)
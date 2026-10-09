#include "Utilities/RagdollUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "FortnitePorting.h"
#include "Misc/PackageName.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace FPRagdoll
{
	static void SavePackageOf(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}

	bool EnsurePhysicsAsset(USkeletalMesh* Mesh)
	{
		if (!Mesh || Mesh->GetPhysicsAsset() || !Mesh->GetSkeleton())
		{
			return false;
		}
		const FString Folder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		const FString Name = Mesh->GetName() + TEXT("_PhysicsAsset");
		UPackage* Package = CreatePackage(*(Folder / Name));
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);

		FPhysAssetCreateParams Params;
		Params.bBodyForAll = true;          // heads and accessories carry bones of their own, give them a body too
		Params.MinBoneSize = 6.0f;
		Params.GeomType = EFG_Sphyl;
		Params.bCreateConstraints = true;
		FText Error;
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(Asset, Mesh, Params, Error))
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Physics asset of %s failed: %s"), *Mesh->GetName(), *Error.ToString());
			return false;
		}
		Mesh->SetPhysicsAsset(Asset);
		Mesh->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Asset);
		SavePackageOf(Asset);
		SavePackageOf(Mesh);
		UE_LOG(LogFortnitePorting, Log, TEXT("Physics asset made for %s"), *Mesh->GetName());
		return true;
	}

	int32 EnsureForFolder(const FString& Folder)
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByPath(FName(*Folder), Assets, true);
		int32 Made = 0;
		for (const FAssetData& Data : Assets)
		{
			if (Data.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
			{
				if (EnsurePhysicsAsset(Cast<USkeletalMesh>(Data.GetAsset())))
				{
					++Made;
				}
			}
		}
		return Made;
	}
}

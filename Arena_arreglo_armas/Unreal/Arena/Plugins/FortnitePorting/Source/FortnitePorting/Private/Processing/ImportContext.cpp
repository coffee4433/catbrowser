#include "FortnitePorting/Public/Processing/ImportContext.h"
#include "ImageCore.h"

#include "AutomatedAssetImportData.h"
#include "ComponentReregisterContext.h"
#include "FortnitePorting.h"
#include "Utils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Classes/BuildingTextureData.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMeshActor.h"
#include "Factories/UEFModelFactory.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InterchangeManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Processing/Enums.h"
#include "Processing/FortnitePortingTexturePipeline.h"
#include "Processing/MaterialMappings.h"
#include "Processing/Names.h"
#include "TextureCompiler.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Materials/Material.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonSerializer.h"
#include "Utilities/EditorUtils.h"
#include "Utilities/JsonWrapper.h"
#include "Utilities/TextureUtils.h"
#include "Internationalization/Regex.h"
#include "World/BuildingActor.h"

FImportContext::FImportContext(const FJsonWrapper& InMetaData) : MetaData(InMetaData)
{
	EnsureDependencies();
}

void FImportContext::RunExport(const FJsonWrapper& Json)
{
	const auto PrimitiveType = Json.Get<EPrimitiveExportType>("PrimitiveType");
	const FJsonWrapper Setup = Json["UnrealSetup"];
	const FString AssetsRoot = MetaData.Get<FString>("AssetsRoot");
	
	switch (PrimitiveType)
	{
	case EPrimitiveExportType::Mesh:
	{
		if (!Setup.IsValid())
		{
			ImportMeshData(Json);
			break;
		}

		RoutingRoot = FFortnitePortingBuilder::GetBaseRootPath(Setup);
		bStyleExport = Setup["Style"].IsValid();
		TArray<FImportedMesh> ImportedMeshes;
		ImportMeshData(Json, &ImportedMeshes);
		FFortnitePortingBuilder(Setup, AssetsRoot).BuildFromMeshes(ImportedMeshes);
		break;
	}
	case EPrimitiveExportType::Animation:
		if (Setup.IsValid())
		{
			FFortnitePortingBuilder(Setup, AssetsRoot).BuildEmote();
		}
		else
		{
			UE_LOG(LogFortnitePorting, Warning, TEXT("Animation exports are only supported for emotes"));
		}
		break;
	case EPrimitiveExportType::Texture:
		ImportTextureData(Json);
		break;
	default:
		break;
	}
}

void FImportContext::RunExportJson(const FString& Data)
{
	// Debug aid: -FPDumpExports saves every payload received from FortnitePorting under Saved/FortnitePorting,
	// and -FPDumpOnly additionally skips the import (to inspect what the app sends without touching the project)
	if (FParse::Param(FCommandLine::Get(), TEXT("FPDumpExports")) || FParse::Param(FCommandLine::Get(), TEXT("FPDumpOnly")))
	{
		const FString DumpPath = FPaths::ProjectSavedDir() / TEXT("FortnitePorting") / FString::Printf(TEXT("export_%s.json"), *FDateTime::Now().ToString(TEXT("%H%M%S")));
		FFileHelper::SaveStringToFile(Data, *DumpPath);
		UE_LOG(LogFortnitePorting, Log, TEXT("Export payload saved to %s"), *DumpPath);
		if (FParse::Param(FCommandLine::Get(), TEXT("FPDumpOnly")))
		{
			return;
		}
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
			
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		UE_LOG(LogFortnitePorting, Error, TEXT("Unable to deserialize response from FortnitePorting"))
		return;
	}

	const auto Root = FJsonWrapper(JsonObject);
			
	auto Exports = Root.GetArray("Exports");

	FScopedSlowTask ImportTask(Exports.Num(), FText::FromString("Importing Data..."));
	ImportTask.MakeDialog(true);
		
	FSlateNotificationManager::Get().SetAllowNotifications(false);
	FTextureUtils::ResetStats();
		
	auto ResponseIndex = 0;
	const FJsonWrapper MetaData = Root["MetaData"];
	for (const auto Export : Exports)
	{
		if (ImportTask.ShouldCancel())
			break;
				
		ResponseIndex++;
			
		FString ExportName = Export.Get<FString>("Name");
				
		ImportTask.DefaultMessage = FText::FromString(FString::Printf(TEXT("Importing Data: %s (%d of %d)"), *ExportName, ResponseIndex, Exports.Num()));
		ImportTask.EnterProgressFrame();
				
		auto ImportContext = FImportContext(MetaData);
		ImportContext.RunExport(Export);
	}
	FSlateNotificationManager::Get().SetAllowNotifications(true);
	FTextureUtils::ReportStats();
	FFortnitePortingBuilder::FlushNotifications();
}

FPathData FImportContext::ResolvePath(const FString& SourcePath, const TCHAR* Category) const
{
	if (RoutingRoot.IsEmpty())
	{
		return FEditorUtils::GetPathData(SourcePath);
	}

	FString SourcePackage;
	FString ObjectName;
	if (!SourcePath.Split(TEXT("."), &SourcePackage, &ObjectName))
	{
		SourcePackage = SourcePath;
		ObjectName = FPackageName::GetShortName(SourcePath);
	}

	FString RootName = SourcePath.RightChop(1);
	RootName = RootName.Left(RootName.Find(TEXT("/")));

	const FString Folder = RoutingRoot / Category;
	return FPathData
	{
		Folder / ObjectName,
		ObjectName,
		Folder,
		RootName,
		SourcePackage
	};
}

void FImportContext::EnsureDependencies()
{
	// These are plain static pointers: after a garbage collection (a play session, for instance) they dangle, and a
	// material instance given such a parent is saved without one and renders without textures. Reload and root them.
	auto Ensure = [](UMaterial*& Material, const TCHAR* Path)
	{
		if (!IsValid(Material))
		{
			Material = Cast<UMaterial>(UEditorAssetLibrary::LoadAsset(Path));
		}
		if (Material != nullptr && !Material->IsRooted())
		{
			Material->AddToRoot();
		}
	};

	Ensure(DefaultMaterial, TEXT("/FortnitePorting/Materials/M_FP_Default.M_FP_Default"));
	Ensure(LayerMaterial, TEXT("/FortnitePorting/Materials/M_FP_Layer.M_FP_Layer"));
}

void FImportContext::ImportMeshData(const FJsonWrapper& ExportData, TArray<FImportedMesh>* OutImported)
{
	auto Meshes = ExportData.GetArray("Meshes");
	auto OverrideMeshes = ExportData.GetArray("OverrideMeshes");
	const int32 Count = Meshes.Num() + OverrideMeshes.Num();
	
	FScopedSlowTask ImportTask(Count, FText::FromString("Importing Meshes..."));
	ImportTask.MakeDialog(true);
	
	auto WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
	const auto World = WorldContext->World();
	
	auto ExportType = ExportData.Get<EExportType>("Type");
	FString ExportName = ExportData.Get<FString>("Name");
	bool bCreateActor = ExportType == EExportType::World || ExportType == EExportType::Prefab;
	
	int32 MeshIndex = 0;
	auto ImportMeshes = [&](const TArray<FJsonWrapper>& MeshArray, bool bIsOverride)
	{
		for (const auto& Mesh : MeshArray)
		{
			if (ImportTask.ShouldCancel())
				break;
			
			MeshIndex++;
			FString MeshName = Mesh.Get<FString>("Name");
			
			ImportTask.DefaultMessage = FText::FromString(FString::Printf(TEXT("Importing Mesh %d of %d: %s"), MeshIndex, Count, *MeshName));
			ImportTask.EnterProgressFrame();
			
			UObject* Imported = ImportModel(ExportData, World, nullptr, Mesh, bCreateActor);
			if (OutImported != nullptr)
			{
				OutImported->Add(FImportedMesh { Mesh, Imported, bIsOverride });

				// Epic's master skeleton for this body: it carries the animated weapon_r/weapon_l bones
				// body meshes lack, so the builder merges its bones and uses it as the weapon rig
				const FJsonWrapper Master = Mesh["Meta"]["MasterSkeletalMesh"];
				if (Master.IsValid() && !Master.Get<FString>("Path").IsEmpty())
				{
					UObject* MasterObject = ImportMesh(Master);
					if (MasterObject != nullptr && !OutImported->ContainsByPredicate([MasterObject](const FImportedMesh& Existing) { return Existing.Object == MasterObject; }))
					{
						FImportedMesh MasterEntry { Master, MasterObject, false };
						MasterEntry.bIsMasterSkeleton = true;
						OutImported->Add(MasterEntry);
					}
				}
			}
		}
	};

	ImportMeshes(Meshes, false);
	ImportMeshes(OverrideMeshes, true);

	// Style material swaps target slots by their original material name
	const auto MaterialSwaps = ExportData.GetArray("OverrideMaterials");
	const auto ParameterOverrides = ExportData.GetArray("OverrideParameters");
	if (OutImported == nullptr || (MaterialSwaps.Num() == 0 && (!bStyleExport || ParameterOverrides.Num() == 0)))
	{
		return;
	}

	for (FImportedMesh& Imported : *OutImported)
	{
		for (const auto& Swap : MaterialSwaps)
		{
			const FString NameToSwap = Swap.Get<FString>("MaterialNameToSwap");
			const FJsonWrapper SwapMaterial = Swap["Material"];
			if (NameToSwap.IsEmpty() || !SwapMaterial.IsValid())
			{
				continue;
			}

			TArray<FJsonWrapper> Targets;
			for (const auto& Material : Imported.Json.GetArray("Materials"))
			{
				// A swap names the material the slot has once the outfit's own overrides are applied (e.g. the
				// body's TV20 material), not the one baked into the mesh, so both names count
				const int32 MaterialSlot = Material.Get<int32>("Slot");
				FString CurrentName = Material.Get<FString>("Name");
				for (const auto& Override : Imported.Json.GetArray("OverrideMaterials"))
				{
					if (Override.Get<int32>("Slot") == MaterialSlot)
					{
						CurrentName = Override.Get<FString>("Name");
					}
				}

				if (Material.Get<FString>("Name").Equals(NameToSwap, ESearchCase::IgnoreCase) || CurrentName.Equals(NameToSwap, ESearchCase::IgnoreCase))
				{
					TSharedPtr<FJsonObject> Target = MakeShared<FJsonObject>();
					Target->Values = SwapMaterial.GetObject()->Values;
					Target->SetNumberField(TEXT("Slot"), Material.Get<int32>("Slot"));
					Targets.Add(FJsonWrapper(Target));
				}
			}

			if (bStyleExport)
			{
				// The mesh may be shared with the outfit and its other styles: keep the swap for the style's Blueprint
				for (const FJsonWrapper& Target : Targets)
				{
					if (UMaterialInstanceConstant* Swapped = ImportMaterial(Target))
					{
						Imported.SlotMaterials.Add({ Target.Get<int32>("Slot"), Swapped });
					}
				}
			}
			else
			{
				ApplyMaterials(Imported.Object, Targets);
			}
		}

		// Options that only repaint (other diffuse / mask textures) become copies of the material in place
		if (bStyleExport)
		{
			ApplyParameterOverrides(Imported, ParameterOverrides);
		}
	}
}

void FImportContext::ApplyParameterOverrides(FImportedMesh& Imported, const TArray<FJsonWrapper>& Overrides)
{
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(Imported.Object);
	if (Mesh == nullptr || Overrides.Num() == 0)
	{
		return;
	}

	const TArray<FSkeletalMaterial>& Slots = Mesh->GetMaterials();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		// The material the slot has now: the style's own swap when there is one
		UMaterialInterface* Current = Slots[Slot].MaterialInterface;
		for (const TPair<int32, UMaterialInterface*>& Entry : Imported.SlotMaterials)
		{
			if (Entry.Key == Slot)
			{
				Current = Entry.Value;
			}
		}
		if (Current == nullptr)
		{
			continue;
		}

		FString RawName;
		for (const auto& Material : Imported.Json.GetArray("Materials"))
		{
			if (Material.Get<int32>("Slot") == Slot)
			{
				RawName = Material.Get<FString>("Name");
			}
		}

		for (const FJsonWrapper& Override : Overrides)
		{
			const FString NameToAlter = Override.Get<FString>("MaterialNameToAlter");
			if (NameToAlter.IsEmpty() || (!NameToAlter.Equals(Current->GetName(), ESearchCase::IgnoreCase) && !NameToAlter.Equals(RawName, ESearchCase::IgnoreCase)))
			{
				continue;
			}

			if (UMaterialInstanceConstant* Variant = ImportMaterialVariant(Current, Override))
			{
				Imported.SlotMaterials.RemoveAll([Slot](const TPair<int32, UMaterialInterface*>& Entry) { return Entry.Key == Slot; });
				Imported.SlotMaterials.Add({ Slot, Variant });
				Current = Variant;
			}
		}
	}
}

UMaterialInstanceConstant* FImportContext::ImportMaterialVariant(UMaterialInterface* Current, const FJsonWrapper& Override)
{
	UMaterialInstanceConstant* Source = Cast<UMaterialInstanceConstant>(Current);
	if (Source == nullptr)
	{
		return nullptr;
	}

	const FMappingCollection& Mappings = Source->Parent == LayerMaterial ? FMaterialMappings::Layer : FMaterialMappings::Default;

	// Only the parameters the Fortnite Porting material has can be shown; colours and tint masks are not among them
	FString Signature;
	bool bMapped = false;
	for (const auto& TexParam : Override.GetArray("Textures"))
	{
		const FString ParamName = TexParam.Get<FString>("Name");
		Signature += ParamName + TexParam["Texture"].Get<FString>("Path");
		bMapped |= Mappings.Textures.ContainsByPredicate([&ParamName](const FSlotMapping& Mapping) { return Mapping.Name.Equals(ParamName); });
	}
	for (const auto& Scalar : Override.GetArray("Scalars"))
	{
		const FString ParamName = Scalar.Get<FString>("Name");
		Signature += ParamName + FString::SanitizeFloat(Scalar.Get<float>("Value"));
		bMapped |= Mappings.Scalars.ContainsByPredicate([&ParamName](const FSlotMapping& Mapping) { return Mapping.Name.Equals(ParamName); });
	}
	if (!bMapped)
	{
		return nullptr;
	}

	const FString Name = FString::Printf(TEXT("%s_P%08X"), *Source->GetName(), GetTypeHash(Signature));
	const FString Folder = RoutingRoot.IsEmpty() ? FPackageName::GetLongPackagePath(Source->GetOutermost()->GetName()) : RoutingRoot / TEXT("Materials");
	UPackage* Package = CreatePackage(*(Folder / Name));
	if (UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(Package, *Name))
	{
		return Existing;
	}

	UMaterialInstanceConstant* Variant = DuplicateObject<UMaterialInstanceConstant>(Source, Package, *Name);
	Variant->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Variant);
	Variant->PreEditChange(nullptr);

	for (const auto& TexParam : Override.GetArray("Textures"))
	{
		const FString ParamName = TexParam.Get<FString>("Name");
		for (const FSlotMapping& Mapping : Mappings.Textures)
		{
			if (!Mapping.Name.Equals(ParamName))
			{
				continue;
			}

			if (UTexture* Texture = ImportTexture(TexParam["Texture"]))
			{
				Variant->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(*Mapping.Slot, GlobalParameter), Texture);
				if (!Mapping.SwitchSlot.IsEmpty())
				{
					Variant->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(*Mapping.SwitchSlot, GlobalParameter), true);
				}
			}
			break;
		}
	}

	for (const auto& Scalar : Override.GetArray("Scalars"))
	{
		const FString ParamName = Scalar.Get<FString>("Name");
		for (const FSlotMapping& Mapping : Mappings.Scalars)
		{
			if (Mapping.Name.Equals(ParamName))
			{
				Variant->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(*Mapping.Slot, GlobalParameter), Scalar.Get<float>("Value"));
				break;
			}
		}
	}

	Variant->PostEditChange();
	Variant->MarkPackageDirty();
	return Variant;
}

void FImportContext::ImportTextureData(const FJsonWrapper& ExportData)
{
	const auto Textures = ExportData.GetArray("Textures");

	// Fire all imports concurrently — Interchange pipelines run on worker threads.
	TArray<UE::Interchange::FAssetImportResultRef> PendingResults;
	PendingResults.Reserve(Textures.Num());

	for (const auto& TextureJson : Textures)
	{
		// Resolve path and skip already-imported / engine assets early.
		const auto PathData = FEditorUtils::GetPathData(TextureJson.Get<FString>("Path"));
		const auto Package = CreatePackage(*PathData.Path);

		if (LoadObject<UTexture>(Package, *PathData.ObjectName) || PathData.RootName.Equals("Engine"))
			continue;

		FString AssetsRoot = MetaData.Get<FString>("AssetsRoot");
		FString TexturePath = FPaths::Combine(AssetsRoot, PathData.SourcePath + ".png");
		if (!FPaths::FileExists(TexturePath))
			TexturePath = FPaths::Combine(AssetsRoot, PathData.SourcePath + ".hdr");
		if (!FPaths::FileExists(TexturePath))
			continue;

		UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
		UInterchangeSourceData* SourceData = Manager.CreateSourceData(TexturePath);

		auto* Pipeline = NewObject<UFortnitePortingTexturePipeline>(GetTransientPackage());
		Pipeline->bWantSRGB = TextureJson.Get<bool>("sRGB");
		Pipeline->WantCompression = TextureJson.Get<TextureCompressionSettings>("CompressionSettings");

		FImportAssetParameters Params;
		Params.bIsAutomated = true;
		Params.bReplaceExisting = false;
		Params.OverridePipelines.Add(Pipeline);

		FString ContentFolder = PathData.Path.LeftChop(PathData.ObjectName.Len() + 1);
		PendingResults.Add(Manager.ImportAssetAsync(ContentFolder, SourceData, Params));
	}

	for (const auto& Result : PendingResults)
	{
		Result->WaitUntilDone();
	}

	// Flush the texture build queue so any subsequent save/cook sees fully compiled assets.
	FTextureCompilingManager::Get().FinishAllCompilation();
}

UObject* FImportContext::ImportModel(const FJsonWrapper& ExportData, UWorld* World, ABuildingActor* Parent, const FJsonWrapper& MeshData, bool bCreateActor)
{
    const auto ImportedObject = ImportMesh(MeshData);

    if (const auto StaticMesh = Cast<UStaticMesh>(ImportedObject); bCreateActor)
    {
        FTransform SpawnTransform;
        auto Actor = World->SpawnActorDeferred<ABuildingActor>(ABuildingActor::StaticClass(), SpawnTransform);
        Actor->Modify();

        Actor->SetActorLabel(*MeshData.Get<FString>("Name"));
        if (Parent) Actor->AttachToActor(Parent, FAttachmentTransformRules(EAttachmentRule::KeepRelative, false));

        Actor->SetActorRelativeLocation(MeshData.Get<FVector>("Location", FVector::ZeroVector));
        Actor->SetActorRelativeRotation(MeshData.Get<FRotator>("Rotation", FRotator::ZeroRotator));
        Actor->SetActorRelativeScale3D(MeshData.Get<FVector>("Scale", FVector::OneVector));

    	for (const auto& TexData : MeshData.GetArray("TextureData"))
    	{
    		Actor->TextureData.Add(FTextureDataInstance {
				.LayerIndex = TexData.Get<int>("Index"),
				.TextureData = ImportBuildingTextureData(TexData)
			});
    	}
    	
        Actor->GetStaticMeshComponent()->ForcedLodModel = 1;
        Actor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
        Actor->SetFolderPath(*FString::Printf(TEXT("/%s"), *ExportData.Get<FString>("Name")));
        Actor->FinishSpawning(SpawnTransform);
        Actor->MarkPackageDirty();
    	
    	auto Children = MeshData.GetArray("Children");
    	if (Children.Num() > 0)
    	{
    		FScopedSlowTask ImportTask(Children.Num(), FText::FromString("Importing Children..."));
    		ImportTask.MakeDialog(true);
		
    		int32 ChildIndex = 0;
		
    		for (const auto& Child : Children)
    		{
    			if (ImportTask.ShouldCancel())
    				break;
			
    			ChildIndex++;
    			FString ChildName = Child.Get<FString>("Name");
			
    			ImportTask.DefaultMessage = FText::FromString(FString::Printf(TEXT("Importing Mesh %d of %d: %s"), ChildIndex, Children.Num(), *ChildName));
    			ImportTask.EnterProgressFrame();
    			ImportModel(ExportData, World, Actor, Child, bCreateActor);
    		}
    	}
    }

    return ImportedObject;
}


UObject* FImportContext::ImportMesh(const FJsonWrapper& MeshData)
{
	FString MeshPath = MeshData.Get<FString>("Path");
	auto PathData = ResolvePath(MeshPath, TEXT("Meshes"));
	auto Package = CreatePackage(*PathData.Path);
	
	auto Mesh = LoadObject<UObject>(Package, *PathData.ObjectName);
	if (Mesh != nullptr || PathData.RootName.Equals("Engine")) return Mesh;
	
	FString AssetsRoot = MetaData.Get<FString>("AssetsRoot");
	const auto ModelPath = FPaths::Combine(AssetsRoot, PathData.SourcePath + ".uemodel");
	if (!FPaths::FileExists(ModelPath)) return nullptr;

	auto AutomatedData = NewObject<UAutomatedAssetImportData>();
	AutomatedData->bReplaceExisting = false;

	const auto ModelFactory = NewObject<UEFModelFactory>();
	ModelFactory->AutomatedImportData = AutomatedData;

	bool Canceled;
	Mesh = ModelFactory->FactoryCreateFile(nullptr, Package, FName(*PathData.ObjectName), RF_Public | RF_Standalone, ModelPath, nullptr, nullptr, Canceled);

	if (const auto StaticMesh = Cast<UStaticMesh>(Mesh))
	{
		StaticMesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
		StaticMesh->GetSourceModel(0).BuildSettings.bRecomputeNormals = false;
		StaticMesh->GetSourceModel(0).BuildSettings.bRecomputeTangents = false;
		StaticMesh->Modify();
	}
	
	ApplyMaterials(Mesh, MeshData.GetArray("Materials"));
	// Character parts declare their real skin materials as overrides
	ApplyMaterials(Mesh, MeshData.GetArray("OverrideMaterials"));
	
	return Mesh;
}

void FImportContext::ApplyMaterials(UObject* Mesh, const TArray<FJsonWrapper>& Materials)
{
	if (Mesh == nullptr)
	{
		return;
	}

	for (const auto& Material : Materials)
	{
		const int32 Slot = Material.Get<int32>("Slot");
		UMaterialInstanceConstant* ImportedMaterial = ImportMaterial(Material);
		if (ImportedMaterial == nullptr)
		{
			continue;
		}
		
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			StaticMesh->SetMaterial(Slot, ImportedMaterial);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			if (SkeletalMesh->GetMaterials().IsValidIndex(Slot))
			{
				SkeletalMesh->GetMaterials()[Slot].MaterialInterface = ImportedMaterial;
				SkeletalMesh->MarkPackageDirty();
			}
		}
	}
}

UMaterialInstanceConstant* FImportContext::ImportMaterial(const FJsonWrapper& MaterialData)
{
	FString MaterialPath = MaterialData.Get<FString>("Path");
	const auto PathData = ResolvePath(MaterialPath, TEXT("Materials"));
	const auto Package = CreatePackage(*PathData.Path);
	
	auto MaterialInstance = LoadObject<UMaterialInstanceConstant>(Package, *PathData.ObjectName);
	if (PathData.RootName.Equals("Engine")) return MaterialInstance;

	// Materials imported while textures were failing have no texture values; fill them in instead of skipping
	const bool bNeedsTextures = MaterialData.GetArray("Textures").Num() > 0;
	// (a material saved without its parent renders without textures: rebuild it too)
	if (MaterialInstance != nullptr && MaterialInstance->Parent != nullptr && (!bNeedsTextures || MaterialInstance->TextureParameterValues.Num() > 0)) return MaterialInstance;

	if (MaterialInstance == nullptr)
	{
		MaterialInstance = NewObject<UMaterialInstanceConstant>(Package, *PathData.ObjectName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(MaterialInstance);
	}
	else
	{
		UE_LOG(LogFortnitePorting, Log, TEXT("Adding missing textures to existing material %s"), *PathData.ObjectName);
		MaterialInstance->Modify();
	}
	
	MaterialInstance->PreEditChange(nullptr);

	auto TargetMaterial = DefaultMaterial;
	FMappingCollection TargetMappings = FMaterialMappings::Default;
	
	bool bIsLayerMaterial = false;
	for (const auto& Switch : MaterialData.GetArray("Switches"))
	{
		if (FNames::LayerSwitchNames.Contains(Switch.Get<FString>("Name")))
		{
			bIsLayerMaterial = true;
			break;
		}
	}
	
	if (bIsLayerMaterial)
	{
		for (const auto& Texture : MaterialData.GetArray("Textures"))
		{
			if (FNames::LayerTextureNames.Contains(Texture.Get<FString>("Name")))
			{
				TargetMaterial = LayerMaterial;
				TargetMappings = FMaterialMappings::Layer;
				break;
			}
		}
	}
	
	MaterialInstance->Parent = TargetMaterial;
	MaterialInstance->BlendMode = MaterialData.Get<EBlendMode>("OverrideBlendMode");

	// Vehicle windows are translucent in Fortnite. The default material is opaque, which rendered them solid white:
	// they use a dedicated see-through tinted glass material instead
	const bool bIsGlass = PathData.ObjectName.Contains(TEXT("Glass")) || PathData.ObjectName.Contains(TEXT("Window"));
	if (bIsGlass && MaterialData.Get<EBlendMode>("OverrideBlendMode") == BLEND_Translucent)
	{
		if (UMaterial* GlassMaterial = LoadObject<UMaterial>(nullptr, TEXT("/FortnitePorting/Materials/M_FP_Glass.M_FP_Glass")))
		{
			MaterialInstance->Parent = GlassMaterial;
		}
	}

	TSet<FString> AssignedSlots;
	TArray<TPair<FString, UTexture*>> UnmappedTextures;
	for (const auto& TexParam : MaterialData.GetArray("Textures"))
	{
		FString ParamName = TexParam.Get<FString>("Name");
		
		const auto Texture = ImportTexture(TexParam["Texture"]);
		if (Texture == nullptr) continue;

		bool bMapped = false;
		for (const auto& Mapping : TargetMappings.Textures)
		{
			if (Mapping.Name.Equals(ParamName))
			{
				bMapped = true;
				AssignedSlots.Add(Mapping.Slot);
				MaterialInstance->SetTextureParameterValueEditorOnly(
					FMaterialParameterInfo(*Mapping.Slot, GlobalParameter), 
					Texture
				);

				if (!Mapping.SwitchSlot.IsEmpty())
				{
					MaterialInstance->SetStaticSwitchParameterValueEditorOnly(
						FMaterialParameterInfo(*Mapping.SwitchSlot, GlobalParameter), 
						true
					);
				}
				break;
			}
		}

		if (!bMapped)
		{
			UnmappedTextures.Emplace(ParamName, Texture);
		}
	}

	// Newer Fortnite materials name their parameters differently (toon/lit colour maps, packed masks...). Unmapped
	// they left the default white base colour: faces or whole parts rendered white. Fill the empty slots from the
	// texture names (Fortnite suffixes: _D, _Color, _N, _S/_SRM, _M).
	if (!bIsLayerMaterial && UnmappedTextures.Num() > 0)
	{
		struct FSlotGuess { const TCHAR* Slot; const TCHAR* Pattern; };
		static const FSlotGuess Guesses[] = {
			{ TEXT("Diffuse"), TEXT("(^|_)(D|Diffuse|BaseColor|Base_Color|BC|Color|ColorL|Col|Albedo|Lit|LitColor|Color_Lit)$|Diffuse|BaseColor|Base Color|Albedo") },
			{ TEXT("Normals"), TEXT("(^|_)(N|Normal|Normals|NM)$|Normal") },
			{ TEXT("SpecularMasks"), TEXT("(^|_)(S|SRM|Spec|Specular|SpecularMasks)$|Specular") },
			{ TEXT("M"), TEXT("(^|_)(M|Mask)$") },
		};

		for (const FSlotGuess& Guess : Guesses)
		{
			if (AssignedSlots.Contains(Guess.Slot))
			{
				continue;
			}

			const FRegexPattern Pattern(Guess.Pattern, ERegexPatternFlags::CaseInsensitive);
			for (const TPair<FString, UTexture*>& Unmapped : UnmappedTextures)
			{
				FRegexMatcher NameMatcher(Pattern, Unmapped.Value->GetName());
				FRegexMatcher ParamMatcher(Pattern, Unmapped.Key);
				if (NameMatcher.FindNext() || ParamMatcher.FindNext())
				{
					MaterialInstance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Guess.Slot, GlobalParameter), Unmapped.Value);
					AssignedSlots.Add(Guess.Slot);
					UE_LOG(LogFortnitePorting, Log, TEXT("%s: '%s' (%s) used as %s"), *PathData.ObjectName, *Unmapped.Key, *Unmapped.Value->GetName(), Guess.Slot);
					break;
				}
			}
		}

		for (const TPair<FString, UTexture*>& Unmapped : UnmappedTextures)
		{
			UE_LOG(LogFortnitePorting, Verbose, TEXT("%s: texture parameter '%s' (%s) has no slot in the Fortnite Porting material"), *PathData.ObjectName, *Unmapped.Key, *Unmapped.Value->GetName());
		}
	}
	
	for (const auto& Scalar : MaterialData.GetArray("Scalars"))
	{
		FString ParamName = Scalar.Get<FString>("Name");
		float ParamValue = Scalar.Get<float>("Value");
		
		for (const auto& Mapping : TargetMappings.Scalars)
		{
			if (Mapping.Name.Equals(ParamName))
			{
				MaterialInstance->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(*Mapping.Slot, GlobalParameter), 
					ParamValue
				);
				break;
			}
		}
	}

	for (const auto& Switch : MaterialData.GetArray("Switches"))
	{
		FString ParamName = Switch.Get<FString>("Name");
		bool ParamValue = Switch.Get<bool>("Value");
		
		for (const auto& Mapping : TargetMappings.Switches)
		{
			if (Mapping.Name.Equals(ParamName))
			{
				MaterialInstance->SetStaticSwitchParameterValueEditorOnly(
					FMaterialParameterInfo(*Mapping.Slot, GlobalParameter), 
					ParamValue
				);
				break;
			}
		}
	}

	// Newer Fortnite SpecularMasks pack roughness in G and metallic in B, and their materials keep the
	// SwizzleRoughnessToGreen switch at its parent default, so it is not exported. Read as metallic, skin turned
	// dark and blotchy. When the switch was not sent, detect the layout from the texture: rough surfaces dominate,
	// so the channel carrying roughness has the higher average.
	bool bSwizzleSent = false;
	for (const auto& Switch : MaterialData.GetArray("Switches"))
	{
		bSwizzleSent |= Switch.Get<FString>("Name").Equals(TEXT("SwizzleRoughnessToGreen"));
	}
	if (!bSwizzleSent && !bIsLayerMaterial)
	{
		UTexture* SpecTexture = nullptr;
		if (MaterialInstance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("SpecularMasks"), GlobalParameter), SpecTexture, true) && SpecTexture)
		{
			FImage SpecImage;
			if (SpecTexture->Source.IsValid() && SpecTexture->Source.GetMipImage(SpecImage, 0))
			{
				FImage Bgra;
				SpecImage.CopyTo(Bgra, ERawImageFormat::BGRA8, EGammaSpace::Linear);
				const TArrayView64<FColor> Pixels = Bgra.AsBGRA8();
				const int64 Step = FMath::Max<int64>(1, Pixels.Num() / 65536);
				double SumG = 0.0, SumB = 0.0;
				int64 Count = 0;
				for (int64 Index = 0; Index < Pixels.Num(); Index += Step)
				{
					SumG += Pixels[Index].G;
					SumB += Pixels[Index].B;
					++Count;
				}
				const bool bRoughInGreen = Count > 0 && SumG > SumB * 1.2;
				MaterialInstance->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(TEXT("SwizzleRoughnessToGreen"), GlobalParameter), bRoughInGreen);
				UE_LOG(LogFortnitePorting, Log, TEXT("%s: SpecularMasks G %.0f / B %.0f -> SwizzleRoughnessToGreen %s"), *PathData.ObjectName, Count ? SumG / Count : 0.0, Count ? SumB / Count : 0.0, bRoughInGreen ? TEXT("on") : TEXT("off"));
			}
		}
	}

	MaterialInstance->SetScalarParameterValueEditorOnly(
		FMaterialParameterInfo("Ambient Occlusion", GlobalParameter), 
		MetaData["Settings"].Get<float>("AmbientOcclusion")
	);
	MaterialInstance->SetScalarParameterValueEditorOnly(
		FMaterialParameterInfo("Cavity", GlobalParameter), 
		MetaData["Settings"].Get<float>("Cavity")
	);
	MaterialInstance->SetScalarParameterValueEditorOnly(
		FMaterialParameterInfo("Subsurface", GlobalParameter), 
		MetaData["Settings"].Get<float>("Subsurface")
	);
	
	MaterialInstance->PostEditChange();
	MaterialInstance->MarkPackageDirty();
	Package->FullyLoad();
	
	FGlobalComponentReregisterContext RecreateComponents;
	
	return MaterialInstance;
}

UBuildingTextureData* FImportContext::ImportBuildingTextureData(const FJsonWrapper& TexData)
{
	const auto PathData = FEditorUtils::GetPathData(TexData.Get<FString>("Path"));
	const auto Package = CreatePackage(*PathData.Path);
	
	auto TextureData = LoadObject<UBuildingTextureData>(Package, *PathData.ObjectName);
	if (TextureData != nullptr || PathData.RootName.Equals("Engine")) return TextureData;
	
	TextureData = NewObject<UBuildingTextureData>(
		Package,
		UBuildingTextureData::StaticClass(),
		*PathData.ObjectName,
		RF_Public | RF_Standalone
	);
	
	TextureData->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(TextureData);
	
	TextureData->Diffuse = ImportTexture(TexData["Diffuse"]);
	TextureData->Normal = ImportTexture(TexData["Normal"]);
	TextureData->Specular = ImportTexture(TexData["Specular"]);
	
	if (auto OverrideMat = TexData["OverrideMaterial"]; OverrideMat.IsValid())
	{
		TextureData->OverrideMaterial = ImportMaterial(OverrideMat);
	}
	
	return TextureData;
}

UTexture* FImportContext::ImportTexture(const FJsonWrapper& TextureData)
{
	const FString SourcePath = TextureData.Get<FString>("Path");
	if (SourcePath.IsEmpty())
		return nullptr;

	const auto PathData = ResolvePath(SourcePath, TEXT("Textures"));
	const auto Package = CreatePackage(*PathData.Path);

	auto Texture = LoadObject<UTexture>(Package, *PathData.ObjectName);
	if (PathData.RootName.Equals("Engine")) return Texture;

	const FString AssetsRoot = MetaData.Get<FString>("AssetsRoot");
	const FString TexturePath = FTextureUtils::FindExportedImage(AssetsRoot, PathData.SourcePath);
	if (Texture != nullptr)
	{
		// Earlier exports could only read the 64px mip tail; swap in the full-size image when it is available now
		FTextureUtils::UpgradeTexture(Cast<UTexture2D>(Texture), TexturePath);
		return Texture;
	}
	if (TexturePath.IsEmpty())
	{
		FTextureUtils::RecordMissing(PathData.SourcePath, AssetsRoot);
		return nullptr;
	}

	const bool bSRGB = TextureData.Get<bool>("sRGB");
	const auto Compression = TextureData.Get<TextureCompressionSettings>("CompressionSettings");

	// Direct creation is synchronous and game-thread safe; the async Interchange import used before
	// could finish without producing an asset, leaving every material grey
	if (UTexture2D* Created = FTextureUtils::CreateTexture(TexturePath, PathData.Path, bSRGB, Compression))
	{
		return Created;
	}

	// Formats FImageUtils cannot read still go through Interchange
	UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
	UInterchangeSourceData* SourceData = Manager.CreateSourceData(TexturePath);

	auto* Pipeline = NewObject<UFortnitePortingTexturePipeline>(GetTransientPackage());
	Pipeline->bWantSRGB = bSRGB;
	Pipeline->WantCompression = Compression;

	FImportAssetParameters Params;
	Params.bIsAutomated = true;
	Params.bReplaceExisting = false;
	Params.OverridePipelines.Add(Pipeline);

	FString ContentFolder = PathData.Path.LeftChop(PathData.ObjectName.Len() + 1);
	UE::Interchange::FAssetImportResultRef ImportResult =
		Manager.ImportAssetAsync(ContentFolder, SourceData, Params);

	ImportResult->WaitUntilDone();
	if (!ImportResult->IsValid())
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("Interchange could not import %s"), *TexturePath);
		return nullptr;
	}

	Texture = Cast<UTexture>(ImportResult->GetFirstAssetOfClass(UTexture::StaticClass()));
	if (Texture == nullptr)
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("Interchange produced no texture for %s"), *TexturePath);
		return nullptr;
	}

	FTextureUtils::RecordImported();
	Package->MarkPackageDirty();
	Package->FullyLoad();
	return Texture;
}

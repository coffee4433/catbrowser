#pragma once
#include "EditorAssetLibrary.h"
#include "Factories/TextureFactory.h"
#include "Factories/UEFModelFactory.h"
#include "Processing/FortnitePortingBuilder.h"
#include "Utilities/JsonWrapper.h"
#include "Utilities/EditorUtils.h"
#include "World/BuildingActor.h"

class FImportContext
{
public:
	FImportContext(const FJsonWrapper& MetaData);
	void RunExport(const FJsonWrapper& Json);
	
	static void RunExportJson(const FString& Data);
	static void EnsureDependencies();
	
	inline static UMaterial* DefaultMaterial;
	inline static UMaterial* LayerMaterial;
	
private:
	FJsonWrapper MetaData;

	// When set (character/cosmetic exports), assets go to <RoutingRoot>/<Category>/<Name> instead of mirroring game paths
	FString RoutingRoot;

	// True for one style of an outfit: its shared meshes are never modified, the swapped materials are only recorded
	bool bStyleExport = false;
	FPathData ResolvePath(const FString& SourcePath, const TCHAR* Category) const;
	
	void ImportMeshData(const FJsonWrapper& ExportData, TArray<FImportedMesh>* OutImported = nullptr);
	void ImportTextureData(const FJsonWrapper& ExportData);
	void ApplyMaterials(UObject* Mesh, const TArray<FJsonWrapper>& Materials);
	
	
	UObject* ImportModel(const FJsonWrapper& ExportData, UWorld* World, ABuildingActor* Parent, const FJsonWrapper&
	                     MeshData, bool bCreateActor);
	UObject* ImportMesh(const FJsonWrapper& MeshData);
	
	UMaterialInstanceConstant* ImportMaterial(const FJsonWrapper& MaterialData);

	/** A copy of Current with the texture parameters a style option overrides ("OverrideParameters"); null when none maps to the material */
	UMaterialInstanceConstant* ImportMaterialVariant(UMaterialInterface* Current, const FJsonWrapper& Override);
	void ApplyParameterOverrides(FImportedMesh& Imported, const TArray<FJsonWrapper>& Overrides);

	UBuildingTextureData* ImportBuildingTextureData(const FJsonWrapper& TexData);
	
	UTexture* ImportTexture(const FJsonWrapper& TextureData);
};

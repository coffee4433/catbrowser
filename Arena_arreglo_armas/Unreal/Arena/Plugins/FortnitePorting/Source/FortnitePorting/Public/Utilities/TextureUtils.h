#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"

class UTexture2D;

class FTextureUtils
{
public:
	/** <AssetsRoot>/<GamePath>.png|.tga|.hdr written by Fortnite Porting, empty if none exists */
	static FString FindExportedImage(const FString& AssetsRoot, const FString& GamePath);

	/**
	 * Creates a texture asset straight from an image file, synchronously on the calling thread.
	 * Returns the existing asset if PackagePath is already a texture.
	 */
	static UTexture2D* CreateTexture(const FString& FilePath, const FString& PackagePath, bool bSRGB, TextureCompressionSettings Compression, bool bUserInterface = false);

	/** Replaces the source of an existing texture when the exported image is bigger (e.g. a 64px mip-tail import made before the full mips could be read) */
	static bool UpgradeTexture(UTexture2D* Texture, const FString& FilePath);

	/** Import statistics of the current batch, reported to the user at the end */
	static void ResetStats();
	static void ReportStats();
	static void RecordMissing(const FString& GamePath, const FString& AssetsRoot);
	static void RecordImported() { ++ImportedCount; }

private:
	static inline int32 ImportedCount = 0;
	static inline int32 MissingCount = 0;
	static inline int32 FailedCount = 0;
	static inline FString FirstMissing;
};

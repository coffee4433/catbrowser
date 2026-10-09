#include "Utilities/TextureUtils.h"

#include "FortnitePorting.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Processing/FortnitePortingBuilder.h"

FString FTextureUtils::FindExportedImage(const FString& AssetsRoot, const FString& GamePath)
{
	int32 DotIndex = INDEX_NONE;
	const FString SourcePackage = GamePath.FindLastChar(TEXT('.'), DotIndex) && DotIndex > GamePath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd)
		? GamePath.Left(DotIndex)
		: GamePath;
	if (SourcePackage.IsEmpty())
	{
		return FString();
	}

	// The app's image format setting decides the extension (PNG by default, TGA optional, HDR for cubemaps)
	for (const TCHAR* Extension : { TEXT(".png"), TEXT(".tga"), TEXT(".hdr") })
	{
		const FString Candidate = FPaths::Combine(AssetsRoot, SourcePackage + Extension);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return FString();
}

namespace
{
	/**
	 * Reads an uncompressed 32-bit Targa keeping its alpha. Fortnite icons are cut-outs: their transparent pixels carry
	 * junk colour, and the engine's own TGA reader drops the alpha channel, which showed as smeared streaks in the locker.
	 */
	bool LoadTga32(const FString& FilePath, FImage& OutImage)
	{
		TArray<uint8> Data;
		if (!FPaths::GetExtension(FilePath).Equals(TEXT("tga"), ESearchCase::IgnoreCase) || !FFileHelper::LoadFileToArray(Data, *FilePath) || Data.Num() < 18)
		{
			return false;
		}

		const uint8 IdLength = Data[0];
		const uint8 ColorMapType = Data[1];
		const uint8 ImageType = Data[2];
		const int32 Width = Data[12] | (Data[13] << 8);
		const int32 Height = Data[14] | (Data[15] << 8);
		const uint8 BitsPerPixel = Data[16];
		const uint8 Descriptor = Data[17];
		if (ColorMapType != 0 || ImageType != 2 || BitsPerPixel != 32 || Width <= 0 || Height <= 0)
		{
			return false;
		}

		const int64 PixelBytes = int64(Width) * Height * 4;
		if (int64(Data.Num()) < 18 + IdLength + PixelBytes)
		{
			return false;
		}

		OutImage.Init(Width, Height, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		const bool bTopToBottom = (Descriptor & 0x20) != 0;
		const uint8* Source = Data.GetData() + 18 + IdLength;
		uint8* Dest = OutImage.RawData.GetData();
		for (int32 Row = 0; Row < Height; ++Row)
		{
			const int32 DestRow = bTopToBottom ? Row : Height - 1 - Row;
			FMemory::Memcpy(Dest + int64(DestRow) * Width * 4, Source + int64(Row) * Width * 4, int64(Width) * 4);
		}
		return true;
	}
}

UTexture2D* FTextureUtils::CreateTexture(const FString& FilePath, const FString& PackagePath, bool bSRGB, TextureCompressionSettings Compression, bool bUserInterface)
{
	const FString ObjectName = FPackageName::GetShortName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	if (UTexture2D* Existing = FindObject<UTexture2D>(Package, *ObjectName))
	{
		return Existing;
	}

	FImage Image;
	if (!(bUserInterface && LoadTga32(FilePath, Image)) && !FImageUtils::LoadImage(*FilePath, Image))
	{
		UE_LOG(LogFortnitePorting, Warning, TEXT("Could not read image %s"), *FilePath);
		++FailedCount;
		return nullptr;
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
	Texture->Source.Init(Image);
	Texture->SRGB = bSRGB;
	Texture->CompressionSettings = Compression;
	if (bUserInterface)
	{
		Texture->LODGroup = TEXTUREGROUP_UI;
		Texture->MipGenSettings = TMGS_NoMipmaps;
	}
	else if (Compression == TC_Normalmap)
	{
		Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
	}

	Texture->PostEditChange();
	FAssetRegistryModule::AssetCreated(Texture);
	Package->MarkPackageDirty();

	++ImportedCount;
	return Texture;
}

bool FTextureUtils::UpgradeTexture(UTexture2D* Texture, const FString& FilePath)
{
	if (Texture == nullptr || FilePath.IsEmpty() || Texture->LODGroup == TEXTUREGROUP_UI)
	{
		return false;
	}

	FImage Image;
	if (!FImageUtils::LoadImage(*FilePath, Image))
	{
		return false;
	}

	const int64 OldPixels = int64(Texture->Source.GetSizeX()) * Texture->Source.GetSizeY();
	if (int64(Image.SizeX) * Image.SizeY <= OldPixels)
	{
		return false;
	}

	UE_LOG(LogFortnitePorting, Log, TEXT("Upgrading %s from %dx%d to %dx%d"), *Texture->GetName(), Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), Image.SizeX, Image.SizeY);
	Texture->Modify();
	Texture->Source.Init(Image);
	Texture->PostEditChange();
	Texture->MarkPackageDirty();
	++ImportedCount;
	return true;
}

void FTextureUtils::ResetStats()
{
	ImportedCount = 0;
	MissingCount = 0;
	FailedCount = 0;
	FirstMissing.Reset();
}

void FTextureUtils::RecordMissing(const FString& GamePath, const FString& AssetsRoot)
{
	UE_LOG(LogFortnitePorting, Warning, TEXT("Texture file not found for %s under %s (.png/.tga/.hdr)"), *GamePath, *AssetsRoot);
	if (MissingCount++ == 0)
	{
		FirstMissing = FPaths::Combine(AssetsRoot, GamePath);
	}
}

void FTextureUtils::ReportStats()
{
	UE_LOG(LogFortnitePorting, Log, TEXT("Textures: %d imported, %d missing, %d unreadable"), ImportedCount, MissingCount, FailedCount);

	if (MissingCount > 0 || FailedCount > 0)
	{
		FFortnitePortingBuilder::Notify(FString::Printf(
			TEXT("%d textura(s) no encontradas y %d ilegibles (%d importadas). Primera que falta: %s. Revisa el Registro de salida (LogFortnitePorting)."),
			MissingCount, FailedCount, ImportedCount, FirstMissing.IsEmpty() ? TEXT("-") : *FirstMissing), true);
	}
}

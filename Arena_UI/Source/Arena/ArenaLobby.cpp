#include "ArenaLobby.h"
#include "ArenaPlayerController.h"
#include "FortnitePortingStyleData.h"
#include "ArenaPlayerState.h"
#include "ArenaSessionSubsystem.h"
#include "ArenaFriendsSubsystem.h"
#include "Arena.h"

#include "ArenaLobbyWidget.h"
#include "ArenaLockerSave.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SpotLight.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "ContentStreaming.h"
#include "TextureResource.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * True when the file exists, starts with the PNG signature (older exports were Radiance HDR with a .png name) and is
	 * not a tiny placeholder (a texture that was still streaming in was once written as 32x32)
	 */
	bool IsRealPng(const FString& FilePath)
	{
		TArray<uint8> Header;
		if (!FPaths::FileExists(FilePath) || !FFileHelper::LoadFileToArray(Header, *FilePath, FILEREAD_Silent) || Header.Num() < 24)
		{
			return false;
		}
		static const uint8 Signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
		if (FMemory::Memcmp(Header.GetData(), Signature, 8) != 0)
		{
			return false;
		}
		const uint32 Width = (static_cast<uint32>(Header[16]) << 24) | (static_cast<uint32>(Header[17]) << 16) | (static_cast<uint32>(Header[18]) << 8) | Header[19];
		return Width >= 64;
	}

	/**
	 * Writes a texture as a real PNG (at most MaxSize pixels on its long side). UKismetRenderingLibrary::ExportTexture2D
	 * writes Radiance HDR regardless of the extension, which no browser can show, so draw it into an 8 bit render target
	 * and compress that instead.
	 */
	bool WriteTexturePng(UObject* WorldContext, UTexture2D* Texture, const FString& FilePath, int32 MaxSize = 256)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (World == nullptr || Texture == nullptr)
		{
			return false;
		}

#if WITH_EDITOR
		// In the editor a texture is built asynchronously and reports a 32x32 placeholder until it is done
		Texture->FinishCachePlatformData();
#endif
		if (Texture->GetSurfaceWidth() < 1.0f || Texture->GetSurfaceHeight() < 1.0f)
		{
			return false;
		}

		// The real size, not the size of the mip that happens to be resident
		const float FullWidth = Texture->GetSurfaceWidth();
		const float FullHeight = Texture->GetSurfaceHeight();
		const float Scale = FMath::Min(1.0f, static_cast<float>(MaxSize) / FMath::Max(FullWidth, FullHeight));
		const int32 Width = FMath::Max(1, FMath::RoundToInt(FullWidth * Scale));
		const int32 Height = FMath::Max(1, FMath::RoundToInt(FullHeight * Scale));

		// The icon must be fully loaded or the file would be blank or hold its blurry lowest mip
		Texture->SetForceMipLevelsToBeResident(60.0f);
		if (Texture->GetNumResidentMips() < Texture->GetNumMips())
		{
			IStreamingManager::Get().StreamAllResources(3.0f);
		}
		if (Texture->GetNumResidentMips() < Texture->GetNumMips())
		{
			return false;
		}

		UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(World, Width, Height, RTF_RGBA8_SRGB, FLinearColor::Transparent, false);
		if (Target == nullptr)
		{
			return false;
		}

		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, CanvasSize, Context);
		if (Canvas == nullptr)
		{
			return false;
		}
		Canvas->K2_DrawTexture(Texture, FVector2D::ZeroVector, CanvasSize, FVector2D::ZeroVector, FVector2D::UnitVector, FLinearColor::White, BLEND_Translucent);
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);

		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		TArray<FColor> Pixels;
		if (Resource == nullptr || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
		{
			return false;
		}

		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		return Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *FilePath);
	}
}

AArenaLobby::AArenaLobby()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	LobbyWidgetClass = UArenaLobbyWidget::StaticClass();
	PlayerName = NSLOCTEXT("ArenaLobby", "PlayerName", "JUGADOR");

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BackdropFinder(TEXT("/Game/UI/Fortnite/Materials/M_LobbyBackdrop.M_LobbyBackdrop"));
	BackdropMaterial = BackdropFinder.Object;
}

void AArenaLobby::BeginPlay()
{
	Super::BeginPlay();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			const FString Nickname = Sessions->GetPlayerNickname();
			if (!Nickname.IsEmpty())
			{
				PlayerName = FText::FromString(Nickname);
			}
		}
	}

	// The player may not be possessed yet on the first frame: try until the pawn is there
	GetWorldTimerManager().SetTimer(RetryTimer, this, &AArenaLobby::TryEnterLobby, 0.05f, true, 0.0f);

	// Periodically check for AutoJoin requests from the Launcher
	GetWorldTimerManager().SetTimer(AutoJoinTimer, this, &AArenaLobby::CheckAutoJoinRequest, 1.5f, true, 2.0f);
}

void AArenaLobby::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	if (Backdrop)
	{
		Backdrop->Destroy();
		Backdrop = nullptr;
	}
	if (LobbyWidget)
	{
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AArenaLobby::DiscoverSkins()
{
	Skins.Reset();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*CharactersPath), Assets, true);

	TSet<FString> Seen;
	for (const FAssetData& Asset : Assets)
	{
		// FortnitePorting writes every character as <CharactersPath>/<Name>/Blueprints/BP_<Name>
		const FString AssetName = Asset.AssetName.ToString();
		// Cooked asset registries also expose generated classes (BP_Name_C); they are not separate skins.
		if (!AssetName.StartsWith(TEXT("BP_")) || AssetName.EndsWith(TEXT("_C")))
		{
			continue;
		}
		const FString PackageName = Asset.PackageName.ToString();
		const FString ClassPath = FString::Printf(TEXT("%s.%s_C"), *PackageName, *AssetName);
		if (Seen.Contains(ClassPath))
		{
			continue;
		}
		Seen.Add(ClassPath);

		// Blueprints of an older import (one per style, under Styles/) are not outfits of their own
		if (PackageName.Contains(TEXT("/Styles/"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString SkinName = AssetName.RightChop(3);
		const FString CharacterFolder = FPaths::GetPath(FPaths::GetPath(PackageName));

		FArenaSkinEntry Entry;
		Entry.Name = FText::FromString(SkinName.Replace(TEXT("_"), TEXT(" ")).ToUpper());
		Entry.PawnClass = TSoftClassPtr<APawn>(FSoftObjectPath(ClassPath));

		const FString IconName = FString::Printf(TEXT("T_%s_Icon"), *SkinName);
		const FSoftObjectPath IconPath(FString::Printf(TEXT("%s/Icon/%s.%s"), *CharacterFolder, *IconName, *IconName));
		if (Registry.GetAssetByObjectPath(IconPath).IsValid())
		{
			Entry.Icon = Cast<UTexture2D>(IconPath.TryLoad());
			if (Entry.Icon)
			{
				// Never write files inside Content: the editor imports a new PNG there as an asset and replaces the icon
				// texture of the same name (that once shrank every icon to a 32x32 placeholder)
				const FString LauncherSkinsFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../Launcher/public/skins"));
				const FString LauncherPngName = FString::Printf(TEXT("%s.png"), *SkinName);
				const FString FullLauncherPngPath = LauncherSkinsFolder / LauncherPngName;
				if (!IsRealPng(FullLauncherPngPath))
				{
					WriteTexturePng(this, Entry.Icon, FullLauncherPngPath);
				}
			}
		}

		// The outfit's styles (channels of options such as Style, Glasses, Tail), when it has a choice
		const FString StylesName = FString::Printf(TEXT("DA_%s_Styles"), *SkinName);
		const FSoftObjectPath StylesPath(FString::Printf(TEXT("%s/Styles/%s.%s"), *CharacterFolder, *StylesName, *StylesName));
		if (Registry.GetAssetByObjectPath(StylesPath).IsValid())
		{
			UFortnitePortingStyleData* StyleData = Cast<UFortnitePortingStyleData>(StylesPath.TryLoad());
			if (StyleData != nullptr && StyleData->HasChoices())
			{
				Entry.StyleData = StyleData;
			}
		}

		Skins.Add(Entry);
	}

	Skins.Sort([](const FArenaSkinEntry& A, const FArenaSkinEntry& B) { return A.Name.CompareTo(B.Name) < 0; });
}

int32 AArenaLobby::FindSkinIndex(const UClass* PawnClass) const
{
	return PawnClass != nullptr ? FindSkinForPath(PawnClass->GetPathName()) : INDEX_NONE;
}

int32 AArenaLobby::FindSkinForPath(const FString& ClassPath) const
{
	return Skins.IndexOfByPredicate([&ClassPath](const FArenaSkinEntry& Entry) { return Entry.PawnClass.ToSoftObjectPath().ToString() == ClassPath; });
}

void AArenaLobby::PrepareLobbyPawn(APawn* Pawn) const
{
	if (Pawn == nullptr)
	{
		return;
	}
	if (APlayerController* PC = Controller.Get())
	{
		Pawn->DisableInput(PC);
	}
	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		Character->GetCharacterMovement()->StopMovementImmediately();
	}

	// The outfit's own lobby idle, pickaxe put away (Fortnite lobby)
	if (UFortnitePortingCharacterComponent* Cosmetics = FindCosmeticComponent(Pawn))
	{
		Cosmetics->PlayLobbyIdle();
	}
	if (APlayerController* PC = Controller.Get())
	{
		if (AArenaPlayerState* State = PC->GetPlayerState<AArenaPlayerState>())
		{
			State->Server_SetLobbyIdle(true);
		}
	}
}

void AArenaLobby::ApplyLobbyInput() const
{
	APlayerController* PC = Controller.Get();
	if (PC == nullptr || LobbyWidget == nullptr)
	{
		return;
	}
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(LobbyWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
	PC->SetShowMouseCursor(true);
}

void AArenaLobby::ComputeCameraTarget(bool bLocker, FVector& OutLocation, FRotator& OutRotation, float& OutFOV) const
{
	// Level camera looking straight at the character, framed so FrameHeight cm fit on screen with the feet near the bottom
	const float BaseDistance = FMath::Max(CameraDistance, 50.0f);
	const float EffectiveFrameHeight = bLocker ? FrameHeight : FMath::Max(FrameHeight, LobbyFocusFrameHeight);
	const FVector FocusCenter = bLocker ? StageCenter : LobbyFocusCenter;
	// Mouse wheel zoom in the locker: narrow the frame and slide it up to the face (Fortnite's locker zoom)
	// The FOV stays fixed and the camera dollies in, so the zoom is a straight push-in with no swing
	const float Zoom = bLocker ? LockerZoom : 0.0f;
	const float ZoomScale = FMath::Lerp(1.0f, FMath::Max(LockerZoomFrameHeight, 20.0f) / FMath::Max(EffectiveFrameHeight, 50.0f), Zoom);
	const float HalfFrame = FMath::Max(EffectiveFrameHeight, 50.0f) * 0.5f;
	const float FeetZ = FocusCenter.Z - StageHalfHeight;
	const float CameraZ = FMath::Lerp(FeetZ - FeetMargin * EffectiveFrameHeight + HalfFrame, FeetZ + StageHalfHeight * 2.0f * LockerZoomFocus, Zoom);

	float Aspect = 16.0f / 9.0f;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D ViewportSize;
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		if (ViewportSize.Y > 1.0f && ViewportSize.X > 1.0f)
		{
			Aspect = ViewportSize.X / ViewportSize.Y;
		}
	}

	// The camera FOV is horizontal: derive it from the vertical frame and the current window shape,
	// so the whole body always fits whatever the resolution
	const float TanHalfVertical = HalfFrame / BaseDistance;
	const float Distance = BaseDistance * ZoomScale;
	OutFOV = FMath::RadiansToDegrees(2.0f * FMath::Atan(TanHalfVertical * Aspect));
	OutRotation = FRotator(0.0f, (-StageForward).Rotation().Yaw, 0.0f);
	OutLocation = FocusCenter + StageForward * Distance;
	OutLocation.Z = CameraZ;

	if (bLocker)
	{
		// Slide the camera sideways so the character sits at LockerScreenX of the screen width
		const float HalfWidth = HalfFrame * ZoomScale * Aspect;
		const float ScreenOffset = 1.0f - 2.0f * FMath::Clamp(LockerScreenX, 0.0f, 1.0f);
		OutLocation += FRotationMatrix(OutRotation).GetUnitAxis(EAxis::Y) * (ScreenOffset * HalfWidth);
	}
}

void AArenaLobby::TryEnterLobby()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn == nullptr || !PC->IsLocalController())
	{
		if (++Attempts > 100)
		{
			GetWorldTimerManager().ClearTimer(RetryTimer);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(RetryTimer);
	Controller = PC;

	// Wear the outfit, pickaxe and glider picked last time in the locker
	DiscoverSkins();
	DiscoverItems();
	if (const UArenaLockerSave* Save = Cast<UArenaLockerSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
	{
		ChosenPickaxe = Save->Pickaxe.IsValid() ? Save->Pickaxe.TryLoad() : nullptr;
		ChosenGlider = Save->Glider.IsValid() ? Save->Glider.TryLoad() : nullptr;

		const int32 Saved = FindSkinForPath(Save->Outfit.ToString());
		if (Saved != INDEX_NONE)
		{
			StyleSelection = Save->OutfitStyles;
			if (Pawn->GetClass()->GetPathName() != Save->Outfit.ToString())
			{
				EquipSkinWithStyles(Saved, Save->OutfitStyles);
				Pawn = PC->GetPawn();
			}
		}
	}

	ApplyLockerItems(Pawn);

	// Character stands still, facing the camera (no turning in the lobby, like Fortnite)
	PrepareLobbyPawn(Pawn);
	StageCenter = Pawn->GetActorLocation();
	StageForward = Pawn->GetActorForwardVector().GetSafeNormal2D();
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		StageHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	if (const AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>())
	{
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			const APawn* PlayerPawn = PlayerState ? PlayerState->GetPawn() : nullptr;
			if (IsValid(PlayerPawn))
			{
				StageForward = PlayerPawn->GetActorForwardVector().GetSafeNormal2D();
				break;
			}
		}
	}
	StageYaw = StageForward.Rotation().Yaw;
	Pawn->SetActorRotation(FRotator(0.0f, StageYaw, 0.0f));
	LobbyFocusCenter = StageCenter;
	LobbyFocusFrameHeight = FrameHeight;

	FVector CameraLocation;
	FRotator CameraRotation;
	float FOV;
	ComputeCameraTarget(false, CameraLocation, CameraRotation, FOV);
	CameraTargetLocation = CameraLocation;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	LobbyCamera = GetWorld()->SpawnActor<ACameraActor>(CameraLocation, CameraRotation, Params);
	if (LobbyCamera)
	{
		UCameraComponent* Camera = LobbyCamera->GetCameraComponent();
		Camera->SetFieldOfView(FOV);
		Camera->bConstrainAspectRatio = false;
		// Studio exposure like Fortnite's locker: histogram metering pulled down a bit and slow to react,
		// so zooming onto the face doesn't blow the skin out
		FPostProcessSettings& Post = Camera->PostProcessSettings;
		Post.bOverride_AutoExposureMethod = true;
		Post.AutoExposureMethod = AEM_Histogram;
		Post.bOverride_AutoExposureBias = true;
		Post.AutoExposureBias = LobbyExposureBias;
		Post.bOverride_AutoExposureSpeedUp = true;
		Post.AutoExposureSpeedUp = 1.0f;
		Post.bOverride_AutoExposureSpeedDown = true;
		Post.AutoExposureSpeedDown = 1.0f;
		Camera->PostProcessBlendWeight = 1.0f;
		PC->SetViewTargetWithBlend(LobbyCamera, 0.0f);
	}

	// Fortnite's blue stage behind the character
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (BackdropMaterial && Plane)
	{
		const FVector BackdropLocation = StageCenter - StageForward * BackdropDistance + FVector(0.0f, 0.0f, 150.0f);
		Backdrop = GetWorld()->SpawnActor<AStaticMeshActor>(BackdropLocation, FRotator(90.0f, (-StageForward).Rotation().Yaw, 0.0f), Params);
		if (Backdrop)
		{
			Backdrop->SetMobility(EComponentMobility::Movable);
			UStaticMeshComponent* Mesh = Backdrop->GetStaticMeshComponent();
			Mesh->SetStaticMesh(Plane);
			Mesh->SetMaterial(0, BackdropMaterial);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->bAffectDynamicIndirectLighting = false;
			Mesh->bAffectDistanceFieldLighting = false;
			// Plane is 100x100: wide enough to fill the frame, also with the locker's side framing
			Backdrop->SetActorScale3D(FVector(12.0f, 40.0f, 1.0f));
		}
	}

	// Neutral stage floor under the character: the level's coloured ground bounces its colour onto the
	// skin through Lumen (the green tint in the locker). Fortnite's lobby stands on a neutral pad.
	if (bSpawnStageFloor)
	{
		UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		// Glossy blue pad like Fortnite's locker (reflects the character); engine material as fallback
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Lobby/M_LobbyFloor.M_LobbyFloor"));
		const bool bLobbyFloor = Base != nullptr;
		if (Base == nullptr)
		{
			Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		const FVector FloorLocation = StageCenter - FVector(0.0f, 0.0f, StageHalfHeight + 0.5f);
		AStaticMeshActor* Floor = Disc ? GetWorld()->SpawnActor<AStaticMeshActor>(FloorLocation, FRotator::ZeroRotator, Params) : nullptr;
		if (Floor)
		{
			Floor->SetMobility(EComponentMobility::Movable);
			UStaticMeshComponent* Mesh = Floor->GetStaticMeshComponent();
			Mesh->SetStaticMesh(Disc);
			if (Base)
			{
				UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Floor);
				MID->SetVectorParameterValue(TEXT("Color"), bLobbyFloor ? FLinearColor(0.02f, 0.12f, 0.55f) : FLinearColor(0.42f, 0.5f, 0.62f));
				Mesh->SetMaterial(0, MID);
			}
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			// The coloured pad must not bounce its colour onto the character (tinted skin)
			Mesh->bAffectDynamicIndirectLighting = false;
			Mesh->bAffectDistanceFieldLighting = false;
			// 100 cm cylinder: 20 m wide pad, 1 cm thick, top surface at the feet
			Floor->SetActorScale3D(FVector(20.0f, 20.0f, 0.01f));
			StageLights.Add(Floor);
		}
	}
	SpawnStageLights(Params);

	UClass* WidgetClass = LobbyWidgetClass ? LobbyWidgetClass.Get() : UArenaLobbyWidget::StaticClass();
	LobbyWidget = CreateWidget<UArenaLobbyWidget>(PC, WidgetClass);
	if (LobbyWidget == nullptr)
	{
		StartMatch();
		return;
	}

	LobbyWidget->PlayerName = PlayerName;
	LobbyWidget->SetSkins(Skins, FindSkinIndex(Pawn->GetClass()));
	LobbyWidget->SetStyleSelection(StyleSelection);
	{
		const UFortnitePortingCharacterComponent* Cosmetics = FindCosmeticComponent(Pawn);
		const UObject* CurrentPickaxe = Cosmetics ? Cosmetics->Pickaxe.Get() : nullptr;
		const UObject* CurrentGlider = Cosmetics ? Cosmetics->Glider.Get() : nullptr;
		LobbyWidget->SetLockerItems(ArenaLockerCategory::Pickaxe, Pickaxes, Pickaxes.IndexOfByPredicate([CurrentPickaxe](const FArenaSkinEntry& Entry) { return Entry.Item == CurrentPickaxe; }));
		LobbyWidget->SetLockerItems(ArenaLockerCategory::Glider, Gliders, Gliders.IndexOfByPredicate([CurrentGlider](const FArenaSkinEntry& Entry) { return Entry.Item == CurrentGlider; }));
	}
	LobbyWidget->OnLockerItemSelected.AddDynamic(this, &AArenaLobby::HandleLockerItemSelected);
	LobbyWidget->OnPlayPressed.AddDynamic(this, &AArenaLobby::HandlePlayPressed);
	LobbyWidget->OnSkinSelected.AddDynamic(this, &AArenaLobby::HandleSkinSelected);
	LobbyWidget->OnStyleSelected.AddDynamic(this, &AArenaLobby::HandleStyleSelected);
	LobbyWidget->OnPageChanged.AddDynamic(this, &AArenaLobby::HandlePageChanged);
	LobbyWidget->OnHostPressed.AddDynamic(this, &AArenaLobby::HandleHostPressed);
	LobbyWidget->OnJoinPressed.AddDynamic(this, &AArenaLobby::HandleJoinPressed);
	LobbyWidget->OnJoinServerSelected.AddDynamic(this, &AArenaLobby::HandleJoinServerSelected);
	LobbyWidget->AddToViewport(10);

	bInLobby = true;
	bLockerView = false;
	ApplyLobbyInput();
}

void AArenaLobby::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInLobby || LobbyCamera == nullptr)
	{
		return;
	}

	// Keep the framing right if the window is resized, and glide between lobby and locker views
	FVector Location;
	FRotator Rotation;
	float FOV;
	LockerZoom = FMath::FInterpTo(LockerZoom, LobbyWidget ? LobbyWidget->GetLockerZoom() : 0.0f, DeltaSeconds, 10.0f);
	LockerYaw = FMath::FInterpTo(LockerYaw, LobbyWidget ? LobbyWidget->GetLockerYaw() : 0.0f, DeltaSeconds, 12.0f);
	if (APawn* Pawn = Controller.IsValid() ? Controller->GetPawn() : nullptr)
	{
		Pawn->SetActorRotation(FRotator(0.0f, StageYaw + LockerYaw, 0.0f));
		KeepFeetOnFloor(Pawn, DeltaSeconds);
	}

	LobbyFocusCenter = StageCenter;
	LobbyFocusFrameHeight = FrameHeight;
	if (!bLockerView)
	{
		if (const AGameStateBase* GameState = GetWorld()->GetGameState<AGameStateBase>())
		{
			TArray<FVector> PlayerLocations;
			for (const APlayerState* PlayerState : GameState->PlayerArray)
			{
				const APawn* PlayerPawn = PlayerState ? PlayerState->GetPawn() : nullptr;
				if (IsValid(PlayerPawn))
				{
					PlayerLocations.Add(PlayerPawn->GetActorLocation());
				}
			}

			if (PlayerLocations.Num() > 1)
			{
				FVector BoundsMin = PlayerLocations[0];
				FVector BoundsMax = PlayerLocations[0];
				for (const FVector& PlayerLocation : PlayerLocations)
				{
					BoundsMin = BoundsMin.ComponentMin(PlayerLocation);
					BoundsMax = BoundsMax.ComponentMax(PlayerLocation);
				}

				LobbyFocusCenter = (BoundsMin + BoundsMax) * 0.5f;
				LobbyFocusCenter.Z = StageCenter.Z;
				float Aspect = 16.0f / 9.0f;
				if (GEngine && GEngine->GameViewport)
				{
					FVector2D ViewportSize;
					GEngine->GameViewport->GetViewportSize(ViewportSize);
					if (ViewportSize.X > 1.0f && ViewportSize.Y > 1.0f)
					{
						Aspect = ViewportSize.X / ViewportSize.Y;
					}
				}
				const FVector CameraRight = FRotationMatrix(FRotator(0.0f, (-StageForward).Rotation().Yaw, 0.0f)).GetUnitAxis(EAxis::Y);
				float MinHorizontal = MAX_flt;
				float MaxHorizontal = -MAX_flt;
				for (const FVector& PlayerLocation : PlayerLocations)
				{
					const float Horizontal = FVector::DotProduct(PlayerLocation - LobbyFocusCenter, CameraRight);
					MinHorizontal = FMath::Min(MinHorizontal, Horizontal);
					MaxHorizontal = FMath::Max(MaxHorizontal, Horizontal);
				}
				const float RequiredFrameHeight = (MaxHorizontal - MinHorizontal + StageHalfHeight) / FMath::Max(Aspect, 0.1f);
				LobbyFocusFrameHeight = FMath::Max(FrameHeight, RequiredFrameHeight * 1.15f);
			}
		}
	}

	ComputeCameraTarget(bLockerView, Location, Rotation, FOV);
	CameraTargetLocation = Location;
	LobbyCamera->SetActorLocationAndRotation(FMath::VInterpTo(LobbyCamera->GetActorLocation(), CameraTargetLocation, DeltaSeconds, CameraMoveSpeed), Rotation);
	LobbyCamera->GetCameraComponent()->SetFieldOfView(FOV);
}

void AArenaLobby::EquipSkin(int32 Index)
{
	EquipSkinWithStyles(Index, TArray<int32>());
}

void AArenaLobby::EquipSkinWithStyles(int32 Index, const TArray<int32>& Styles)
{
	APlayerController* PC = Controller.Get();
	APawn* OldPawn = PC ? PC->GetPawn() : nullptr;
	if (OldPawn == nullptr || !Skins.IsValidIndex(Index))
	{
		return;
	}

	StyleSelection = Skins[Index].StyleData ? Skins[Index].StyleData->Sanitize(Styles) : TArray<int32>();

	UClass* NewClass = Skins[Index].PawnClass.LoadSynchronous();
	if (NewClass == nullptr)
	{
		return;
	}

	// The same outfit again: only its style options change
	if (NewClass == OldPawn->GetClass())
	{
		ApplyLockerItems(OldPawn);
		if (AArenaPlayerState* PS = PC->GetPlayerState<AArenaPlayerState>(); PS && GetWorld() && GetWorld()->GetNetMode() != NM_Standalone)
		{
			PS->Server_SetSkinStyles(StyleSelection);
		}
		SaveLocker();
		if (LobbyWidget)
		{
			LobbyWidget->SetSelectedSkin(Index);
			LobbyWidget->SetStyleSelection(StyleSelection);
		}
		return;
	}

	const FString ClassPath = NewClass->GetPathName();
	const bool bNetworked = GetWorld() && GetWorld()->GetNetMode() != NM_Standalone;

	if (bNetworked)
	{
		// ── Networked: ask the server to do the authoritative pawn swap ──
		AArenaPlayerState* PS = PC->GetPlayerState<AArenaPlayerState>();
		if (PS)
		{
			PS->Server_RequestEquipSkin(ClassPath);
			PS->Server_SetSkinIndex(Index);
			PS->Server_SetSkinStyles(StyleSelection);

			// The pawn swap happens on the next frame(s); poll until the new pawn arrives
			// to re-apply lobby framing, camera, and input
			FTimerDelegate PostEquipDelegate;
			PostEquipDelegate.BindWeakLambda(this, [this, Index, ClassPath]()
			{
				APlayerController* LPC = Controller.Get();
				APawn* NewPawn = LPC ? LPC->GetPawn() : nullptr;
				if (NewPawn == nullptr)
				{
					return; // still waiting
				}

				// Check if the pawn class has actually changed
				if (Skins.IsValidIndex(Index) && NewPawn->GetClass()->GetPathName() != ClassPath)
				{
					return; // pawn hasn't swapped yet
				}

				GetWorldTimerManager().ClearTimer(PostEquipTimer);
				PrepareLobbyPawn(NewPawn);
				if (bInLobby && LobbyCamera)
				{
					LPC->SetViewTarget(LobbyCamera);
					ApplyLobbyInput();
				}
			});
			GetWorldTimerManager().SetTimer(PostEquipTimer, PostEquipDelegate, 0.05f, true);
		}
	}
	else
	{
		// ── Standalone: local pawn swap (no server needed) ──
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APawn* NewPawn = GetWorld()->SpawnActor<APawn>(NewClass, OldPawn->GetActorTransform(), Params);
		if (NewPawn)
		{
			PC->Possess(NewPawn);
			OldPawn->Destroy();
			ApplyLockerItems(NewPawn);
			PrepareLobbyPawn(NewPawn);

			if (bInLobby)
			{
				if (LobbyCamera)
				{
					PC->SetViewTarget(LobbyCamera);
				}
				ApplyLobbyInput();
			}
		}
	}

	SaveLocker();
	if (LobbyWidget)
	{
		LobbyWidget->SetSelectedSkin(Index);
		LobbyWidget->SetStyleSelection(StyleSelection);
	}
}

void AArenaLobby::HandleSkinSelected(int32 SkinIndex)
{
	EquipSkin(SkinIndex);
}

void AArenaLobby::HandleStyleSelected(int32 SkinIndex, int32 ChannelIndex, int32 OptionIndex)
{
	if (!Skins.IsValidIndex(SkinIndex) || ChannelIndex < 0)
	{
		return;
	}

	if (StyleSelection.Num() <= ChannelIndex)
	{
		StyleSelection.SetNumZeroed(ChannelIndex + 1);
	}
	StyleSelection[ChannelIndex] = OptionIndex;

	APlayerController* PC = Controller.Get();
	if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		ApplyLockerItems(Pawn);
	}
	if (PC && GetWorld() && GetWorld()->GetNetMode() != NM_Standalone)
	{
		if (AArenaPlayerState* PS = PC->GetPlayerState<AArenaPlayerState>())
		{
			PS->Server_SetSkinStyles(StyleSelection);
		}
	}
	SaveLocker();
}

void AArenaLobby::HandleLockerItemSelected(int32 LockerCategory, int32 ItemIndex)
{
	const TArray<FArenaSkinEntry>& Items = LockerCategory == ArenaLockerCategory::Pickaxe ? Pickaxes : Gliders;
	if (!Items.IsValidIndex(ItemIndex))
	{
		return;
	}

	if (LockerCategory == ArenaLockerCategory::Pickaxe)
	{
		ChosenPickaxe = Items[ItemIndex].Item;
	}
	else if (LockerCategory == ArenaLockerCategory::Glider)
	{
		ChosenGlider = Items[ItemIndex].Item;
	}

	APlayerController* PC = Controller.Get();
	ApplyLockerItems(PC ? PC->GetPawn() : nullptr);

	// Notify the server of the cosmetic change for replication
	if (PC)
	{
		AArenaPlayerState* PS = PC->GetPlayerState<AArenaPlayerState>();
		if (PS)
		{
			if (LockerCategory == ArenaLockerCategory::Pickaxe && ChosenPickaxe)
			{
				PS->Server_SetPickaxe(ChosenPickaxe->GetPathName());
			}
			else if (LockerCategory == ArenaLockerCategory::Glider && ChosenGlider)
			{
				PS->Server_SetGlider(ChosenGlider->GetPathName());
			}
		}
	}

	SaveLocker();
}

UFortnitePortingCharacterComponent* AArenaLobby::FindCosmeticComponent(APawn* Pawn) const
{
	return Pawn ? Pawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;
}

void AArenaLobby::ApplyLockerItems(APawn* Pawn) const
{
	UFortnitePortingCharacterComponent* Cosmetics = FindCosmeticComponent(Pawn);
	if (Cosmetics == nullptr)
	{
		return;
	}

	if (Cosmetics->StyleData != nullptr)
	{
		Cosmetics->SetStyleSelection(StyleSelection);
	}
	if (UFortnitePortingPickaxeData* PickaxeData = Cast<UFortnitePortingPickaxeData>(ChosenPickaxe))
	{
		Cosmetics->SetPickaxe(PickaxeData);
	}
	if (UFortnitePortingGliderData* GliderData = Cast<UFortnitePortingGliderData>(ChosenGlider))
	{
		Cosmetics->SetGlider(GliderData);
	}
}

void AArenaLobby::SaveLocker() const
{
	UArenaLockerSave* Save = Cast<UArenaLockerSave>(UGameplayStatics::CreateSaveGameObject(UArenaLockerSave::StaticClass()));
	if (Save == nullptr)
	{
		return;
	}

	const APlayerController* PC = Controller.Get();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const int32 SkinIndex = FindSkinIndex(Pawn ? Pawn->GetClass() : nullptr);
	if (Skins.IsValidIndex(SkinIndex))
	{
		Save->Outfit = FSoftClassPath(Skins[SkinIndex].PawnClass.ToSoftObjectPath().ToString());
		Save->OutfitStyles = StyleSelection;

		// Save EquippedSkin.json for the launcher: skinId is the name of the skin folder under Characters/, which is also
		// the file name of its icon in the public storage (the display name is upper case and cannot be used as an id)
		const FString PawnPath = Skins[SkinIndex].PawnClass.ToSoftObjectPath().ToString();
		FString SkinFolder;
		{
			const FString Marker = TEXT("/Characters/");
			const int32 MarkerIndex = PawnPath.Find(Marker, ESearchCase::IgnoreCase);
			if (MarkerIndex != INDEX_NONE)
			{
				SkinFolder = PawnPath.Mid(MarkerIndex + Marker.Len());
				SkinFolder = SkinFolder.Left(SkinFolder.Find(TEXT("/")));
			}
		}
		const FString SkinCleanName = Skins[SkinIndex].Name.ToString().Replace(TEXT(" "), TEXT("_"));
		const FString SavedJsonPath = FPaths::ProjectSavedDir() / TEXT("EquippedSkin.json");

		// The launcher shows this icon as the player's avatar: a real PNG next to the json, so it needs no hosted catalog
		const FString EquippedIconPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EquippedSkin.png"));
		static FString LastIconSkin;
		const FString IconKey = SkinFolder.IsEmpty() ? SkinCleanName : SkinFolder;
		bool bHasEquippedIcon = IsRealPng(EquippedIconPath) && LastIconSkin == IconKey;
		if (!bHasEquippedIcon && Skins[SkinIndex].Icon != nullptr && WriteTexturePng(const_cast<AArenaLobby*>(this), Skins[SkinIndex].Icon, EquippedIconPath, 128))
		{
			LastIconSkin = IconKey;
			bHasEquippedIcon = true;
		}
		const FString RelativeIcon = FString::Printf(TEXT("FortnitePorting/Characters/%s/Icon/T_%s_Icon.png"), *SkinCleanName, *SkinCleanName);

		TSharedPtr<FJsonObject> SkinObj = MakeShared<FJsonObject>();
		SkinObj->SetStringField(TEXT("skinId"), SkinFolder.IsEmpty() ? SkinCleanName : SkinFolder);
		SkinObj->SetStringField(TEXT("skinName"), SkinCleanName);
		SkinObj->SetStringField(TEXT("displayName"), Skins[SkinIndex].Name.ToString());
		SkinObj->SetStringField(TEXT("iconFile"), bHasEquippedIcon ? EquippedIconPath : FString());
		SkinObj->SetStringField(TEXT("iconPath"), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / RelativeIcon));
		SkinObj->SetStringField(TEXT("iconUrl"), FString::Printf(TEXT("/content/%s"), *RelativeIcon));
		SkinObj->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());

		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		if (FJsonSerializer::Serialize(SkinObj.ToSharedRef(), Writer))
		{
			FFileHelper::SaveStringToFile(OutputString, *SavedJsonPath);
		}
	}
	Save->Pickaxe = FSoftObjectPath(ChosenPickaxe.Get());
	Save->Glider = FSoftObjectPath(ChosenGlider.Get());
	UGameplayStatics::SaveGameToSlot(Save, SaveSlot, 0);
}

void AArenaLobby::DiscoverItems()
{
	Pickaxes.Reset();
	Gliders.Reset();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	auto Collect = [&Registry, this](UClass* Class, TArray<FArenaSkinEntry>& Out)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(Class->GetClassPathName());
		Filter.PackagePaths.Add(FName(*FPaths::GetPath(CharactersPath)));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		for (const FAssetData& AssetData : Assets)
		{
			const UFortnitePortingCosmeticData* Data = Cast<UFortnitePortingCosmeticData>(AssetData.GetAsset());
			if (Data == nullptr)
			{
				continue;
			}

			FArenaSkinEntry Entry;
			const FString Name = Data->DisplayName.IsEmpty() ? AssetData.AssetName.ToString().RightChop(3).Replace(TEXT("_"), TEXT(" ")) : Data->DisplayName.ToString();
			Entry.Name = FText::FromString(Name.ToUpper());
			Entry.Icon = Data->Icon;
			Entry.Item = const_cast<UFortnitePortingCosmeticData*>(Data);
			Out.Add(Entry);
		}
		Out.Sort([](const FArenaSkinEntry& A, const FArenaSkinEntry& B) { return A.Name.CompareTo(B.Name) < 0; });
	};

	Collect(UFortnitePortingPickaxeData::StaticClass(), Pickaxes);
	Collect(UFortnitePortingGliderData::StaticClass(), Gliders);
}

void AArenaLobby::HandlePageChanged(bool bLocker)
{
	bLockerView = bLocker;
}

void AArenaLobby::HandlePlayPressed()
{
	const FString Chosen = LobbyWidget ? LobbyWidget->GetSelectedMapPath() : FString();
	const FString Current = UWorld::RemovePIEPrefix(GetOutermost()->GetName());
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone)
	{
		if (!Chosen.IsEmpty())
		{
			if (AArenaPlayerController* PC = Cast<AArenaPlayerController>(Controller.Get()))
			{
				PC->ServerTravelToArenaMap(Chosen);
			}
		}
		return;
	}

	if (!Chosen.IsEmpty() && Chosen != Current)
	{
		UGameplayStatics::OpenLevel(this, FName(*Chosen));
		return;
	}

	StartMatch();
}

void AArenaLobby::HandleHostPressed()
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		return;
	}

	// Don't host again if we already have a session running
	if (Sessions->IsInSession())
	{
		if (LobbyWidget)
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "AlreadyHosting", "YA TIENES UN SERVIDOR ACTIVO"));
		}
		return;
	}

	// If using EOS and not yet logged in, prompt EOS login
	if (Sessions->IsUsingEOS() && !Sessions->IsLoggedIn())
	{
		if (LobbyWidget)
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "LoggingInEOS", "CONECTANDO CON EPIC ONLINE SERVICES..."));
		}
		Sessions->OnLoginComplete.RemoveAll(this);
		Sessions->OnLoginComplete.AddDynamic(this, &AArenaLobby::HandleHostLoginComplete);
		Sessions->Login(TEXT("accountportal"));
		return;
	}

	ProceedWithHosting();
}

void AArenaLobby::HandleHostLoginComplete(bool bSuccess, const FString& Error)
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions)
	{
		Sessions->OnLoginComplete.RemoveAll(this);
	}

	if (bSuccess)
	{
		if (LobbyWidget)
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "EOSLoginSuccess", "SESIÓN INICIADA EN EPIC GAMES"));
		}
	}
	else
	{
		UE_LOG(LogArena, Warning, TEXT("EOS login not completed, continuing in LAN mode: %s"), *Error);
	}

	ProceedWithHosting();
}

void AArenaLobby::ProceedWithHosting()
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		return;
	}

	// The session offers the map picked in the selector; the lobby map itself is only the menu
	FString MapPath = LobbyWidget ? LobbyWidget->GetSelectedMapPath() : FString();
	if (MapPath.IsEmpty())
	{
		MapPath = GetWorld()->GetMapName();
	}
	const FString ServerName = Sessions->GetPlayerNickname();

	// Listen for the result
	Sessions->OnHostReady.RemoveAll(this);
	Sessions->OnHostReady.AddDynamic(this, &AArenaLobby::HandleHostResult);

	// Host the session (creates EOS Lobby/Session or LAN fallback and starts listen server)
	Sessions->HostSession(ServerName, MapPath, 8);
}

void AArenaLobby::HandleHostResult(bool bSuccess)
{
	if (LobbyWidget)
	{
		if (bSuccess)
		{
			UGameInstance* GI = GetGameInstance();
			UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
			const FString HostName = Sessions ? Sessions->GetPlayerNickname() : FPlatformProcess::ComputerName();
			const FString ModeText = (Sessions && Sessions->IsUsingEOS() && Sessions->IsLoggedIn()) ? TEXT("EPIC ONLINE SERVICES") : TEXT("LAN");

			LobbyWidget->ShowToast(FText::FromString(FString::Printf(TEXT("SERVIDOR ACTIVO · %s"), *HostName)));
			LobbyWidget->ModeDescription = FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  8 JUGADORES"), *HostName, *ModeText));
		}
		else
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "HostFail", "ERROR AL CREAR SERVIDOR"));
		}
	}
}

void AArenaLobby::HandleJoinPressed()
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		return;
	}

	// If using EOS and not yet logged in, prompt EOS login first
	if (Sessions->IsUsingEOS() && !Sessions->IsLoggedIn())
	{
		if (LobbyWidget)
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "LoggingInEOS", "CONECTANDO CON EPIC ONLINE SERVICES..."));
		}
		Sessions->OnLoginComplete.RemoveAll(this);
		Sessions->OnLoginComplete.AddDynamic(this, &AArenaLobby::HandleJoinLoginComplete);
		Sessions->Login(TEXT("accountportal"));
		return;
	}

	// Subscribe to search results
	Sessions->OnServersFound.RemoveDynamic(this, &AArenaLobby::HandleServersFound);
	Sessions->OnServersFound.AddDynamic(this, &AArenaLobby::HandleServersFound);

	// Start searching for sessions
	Sessions->FindSessions();
}

void AArenaLobby::HandleJoinLoginComplete(bool bSuccess, const FString& Error)
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		return;
	}

	Sessions->OnLoginComplete.RemoveAll(this);

	if (bSuccess)
	{
		if (LobbyWidget)
		{
			LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "EOSLoginSuccess", "SESIÓN INICIADA EN EPIC GAMES"));
		}
	}

	// Proceed with search
	Sessions->OnServersFound.RemoveDynamic(this, &AArenaLobby::HandleServersFound);
	Sessions->OnServersFound.AddDynamic(this, &AArenaLobby::HandleServersFound);
	Sessions->FindSessions();
}

void AArenaLobby::HandleServersFound(const TArray<FArenaServerInfo>& Servers)
{
	if (LobbyWidget)
	{
		LobbyWidget->SetServerList(Servers);
	}

	if (bAutoJoinPending)
	{
		bAutoJoinPending = false;
		if (Servers.Num() > 0)
		{
			int32 MatchIndex = INDEX_NONE;
			for (int32 i = 0; i < Servers.Num(); ++i)
			{
				if (!PendingAutoJoinFriend.IsEmpty() && Servers[i].ServerName.Contains(PendingAutoJoinFriend, ESearchCase::IgnoreCase))
				{
					MatchIndex = i;
					break;
				}
				if (!PendingAutoJoinSession.IsEmpty() && Servers[i].ServerName.Contains(PendingAutoJoinSession, ESearchCase::IgnoreCase))
				{
					MatchIndex = i;
					break;
				}
			}

			if (MatchIndex == INDEX_NONE)
			{
				MatchIndex = 0;
			}

			if (LobbyWidget)
			{
				LobbyWidget->ShowToast(FText::FromString(FString::Printf(TEXT("CONECTANDO AL LOBBY DE %s..."), *Servers[MatchIndex].ServerName)));
			}
			HandleJoinServerSelected(MatchIndex);
		}
		else
		{
			if (LobbyWidget)
			{
				LobbyWidget->ShowToast(NSLOCTEXT("ArenaLobby", "NoAutoJoinServers", "NO SE ENCONTRÓ NINGÚN LOBBY ACTIVO"));
			}
		}
	}
}

void AArenaLobby::HandleJoinServerSelected(int32 ServerIndex)
{
	UGameInstance* GI = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		return;
	}

	if (LobbyWidget)
	{
		LobbyWidget->SetServerBrowserStatus(NSLOCTEXT("ArenaLobby", "Connecting", "CONECTANDO..."));
	}

	// Join the selected server
	Sessions->JoinSession(ServerIndex);
}

void AArenaLobby::StartMatch()
{
	bInLobby = false;
	if (UGameInstance* PresenceGameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = PresenceGameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->SetMyPresence(TEXT("En partida"));
		}
	}
	APlayerController* PC = Controller.Get();
	if (LobbyWidget)
	{
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}
	if (PC == nullptr)
	{
		return;
	}

	if (Backdrop)
	{
		Backdrop->Destroy();
		Backdrop = nullptr;
	}

	for (AActor* Light : StageLights)
	{
		if (Light)
		{
			Light->Destroy();
		}
	}
	StageLights.Reset();

	ResetFeet();
	APawn* Pawn = PC->GetPawn();
	if (Pawn)
	{
		Pawn->SetActorRotation(FRotator(0.0f, StageYaw, 0.0f));
	}
	if (UFortnitePortingCharacterComponent* Cosmetics = FindCosmeticComponent(Pawn))
	{
		Cosmetics->StopLobbyIdle();
	}
	if (Pawn)
	{
		// The game camera goes behind the character
		PC->SetControlRotation(FRotator(-10.0f, Pawn->GetActorRotation().Yaw, 0.0f));
		PC->SetViewTargetWithBlend(Pawn, PlayBlendTime, VTBlend_Cubic);
		Pawn->EnableInput(PC);
	}

	PC->SetInputMode(FInputModeGameOnly());
	PC->SetShowMouseCursor(false);

	if (LobbyCamera)
	{
		GetWorldTimerManager().SetTimer(CleanupTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (LobbyCamera)
			{
				LobbyCamera->Destroy();
				LobbyCamera = nullptr;
			}
		}), PlayBlendTime + 0.1f, false);
	}
}

void AArenaLobby::SpawnStageLights(const FActorSpawnParameters& Params)
{
	// Fortnite's lobby lighting: a soft warm key from the front-left and a cool rim from behind that outlines
	// the character against the backdrop
	const FVector Right = FVector::CrossProduct(FVector::UpVector, StageForward).GetSafeNormal();
	const FVector Chest = StageCenter + FVector(0.0f, 0.0f, StageHalfHeight * 0.5f);
	struct FStageLight { FVector Offset; float Intensity; FLinearColor Color; float Radius; };
	const FStageLight Lights[] = {
		{ StageForward * 320.0f - Right * 180.0f + FVector(0.0f, 0.0f, 160.0f), KeyLightIntensity, FLinearColor(1.0f, 0.96f, 0.9f), 60.0f },
		{ -StageForward * 180.0f + Right * 120.0f + FVector(0.0f, 0.0f, 200.0f), RimLightIntensity, FLinearColor(0.75f, 0.85f, 1.0f), 20.0f },
		{ -StageForward * 180.0f - Right * 140.0f + FVector(0.0f, 0.0f, 180.0f), RimLightIntensity * 0.6f, FLinearColor(0.85f, 0.9f, 1.0f), 20.0f },
	};
	for (const FStageLight& Light : Lights)
	{
		if (Light.Intensity <= 0.0f)
		{
			continue;
		}
		const FVector Location = StageCenter + Light.Offset;
		ASpotLight* Spot = GetWorld()->SpawnActor<ASpotLight>(Location, (Chest - Location).Rotation(), Params);
		if (Spot == nullptr)
		{
			continue;
		}
		Spot->SetMobility(EComponentMobility::Movable);
		USpotLightComponent* Component = Spot->SpotLightComponent;
		// ASpotLight's component carries its own relative rotation: aim the component itself at the chest
		Component->SetWorldRotation((Chest - Location).Rotation());
		Component->SetIntensityUnits(ELightUnits::Candelas);
		Component->SetIntensity(Light.Intensity);
		Component->SetLightColor(Light.Color);
		Component->SetAttenuationRadius(900.0f);
		Component->SetInnerConeAngle(20.0f);
		Component->SetOuterConeAngle(40.0f);
		Component->SetSourceRadius(Light.Radius);
		Component->SetSoftSourceRadius(Light.Radius);
		Component->SetCastShadows(true);
		StageLights.Add(Spot);
	}
}

void AArenaLobby::KeepFeetOnFloor(APawn* Pawn, float DeltaSeconds)
{
	ACharacter* Character = Cast<ACharacter>(Pawn);
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (Mesh == nullptr || Mesh->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}
	if (FootPawn.Get() != Pawn)
	{
		FootPawn = Pawn;
		FootBaseZ = Mesh->GetRelativeLocation().Z;
		FootOffset = 0.0f;
	}

	// Where the soles are: each foot bone's current height minus its height above the mesh origin in the
	// reference pose (Fortnite bodies stand with their soles on the origin)
	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const float Scale = Mesh->GetComponentScale().Z;
	float SoleZ = TNumericLimits<float>::Max();
	for (const TCHAR* BoneName : { TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r") })
	{
		const int32 Bone = RefSkeleton.FindBoneIndex(BoneName);
		if (Bone == INDEX_NONE)
		{
			continue;
		}
		const FTransform Ref = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, Bone);
		SoleZ = FMath::Min(SoleZ, float(Mesh->GetBoneLocation(BoneName).Z - Ref.GetLocation().Z * Scale));
	}
	if (SoleZ == TNumericLimits<float>::Max())
	{
		return;
	}

	const float FloorZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Target = FMath::Clamp(FootOffset + (FloorZ - SoleZ), -30.0f, 30.0f);
	FootOffset = FMath::FInterpTo(FootOffset, Target, DeltaSeconds, 8.0f);
	FVector Relative = Mesh->GetRelativeLocation();
	Relative.Z = FootBaseZ + FootOffset;
	Mesh->SetRelativeLocation(Relative);
}

void AArenaLobby::ResetFeet()
{
	if (ACharacter* Character = Cast<ACharacter>(FootPawn.Get()))
	{
		FVector Relative = Character->GetMesh()->GetRelativeLocation();
		Relative.Z = FootBaseZ;
		Character->GetMesh()->SetRelativeLocation(Relative);
	}
	FootPawn.Reset();
	FootOffset = 0.0f;
}

void AArenaLobby::CheckAutoJoinRequest()
{
	const FString SavedDir = FPaths::ProjectSavedDir();
	const FString SessionPaths[] = {
		FPaths::Combine(SavedDir, TEXT("Config"), TEXT("LauncherSession.json")),
		FPaths::Combine(SavedDir, TEXT("LauncherSession.json"))
	};

	FString JsonContent;
	FString FoundPath;
	for (const FString& Path : SessionPaths)
	{
		if (FFileHelper::LoadFileToString(JsonContent, *Path) && !JsonContent.IsEmpty())
		{
			FoundPath = Path;
			break;
		}
	}

	bool bTriggerAutoJoin = false;
	if (!FoundPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			bool bAutoJoin = false;
			if (JsonObj->TryGetBoolField(TEXT("autoJoin"), bAutoJoin) && bAutoJoin)
			{
				JsonObj->TryGetStringField(TEXT("targetFriend"), PendingAutoJoinFriend);
				JsonObj->TryGetStringField(TEXT("joinSession"), PendingAutoJoinSession);
				bTriggerAutoJoin = true;

				// Clear autoJoin in file so it doesn't repeat
				JsonObj->SetBoolField(TEXT("autoJoin"), false);
				FString OutputString;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
				FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
				FFileHelper::SaveStringToFile(OutputString, *FoundPath);
			}
		}
	}

	// Also check command line if not triggered
	if (!bTriggerAutoJoin && !bAutoJoinPending)
	{
		static bool bCommandLineChecked = false;
		if (!bCommandLineChecked)
		{
			bCommandLineChecked = true;
			if (FParse::Param(FCommandLine::Get(), TEXT("autojoin")))
			{
				FParse::Value(FCommandLine::Get(), TEXT("joinfriend="), PendingAutoJoinFriend);
				FParse::Value(FCommandLine::Get(), TEXT("joinlobby="), PendingAutoJoinSession);
				bTriggerAutoJoin = true;
			}
		}
	}

	if (bTriggerAutoJoin)
	{
		bAutoJoinPending = true;
		UGameInstance* GI = GetGameInstance();
		UArenaSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
		if (Sessions)
		{
			FString TargetName = PendingAutoJoinFriend.IsEmpty() ? PendingAutoJoinSession : PendingAutoJoinFriend;
			if (LobbyWidget)
			{
				FString Msg = TargetName.IsEmpty() ? TEXT("BUSCANDO LOBBY PARA UNIRSE...") : FString::Printf(TEXT("UNIÉNDOSE AL LOBBY DE %s..."), *TargetName);
				LobbyWidget->ShowToast(FText::FromString(Msg));
			}

			if (Sessions->IsUsingEOS() && !Sessions->IsLoggedIn())
			{
				Sessions->OnLoginComplete.RemoveAll(this);
				Sessions->OnLoginComplete.AddDynamic(this, &AArenaLobby::HandleJoinLoginComplete);
				Sessions->Login(TEXT("accountportal"));
				return;
			}

			Sessions->OnServersFound.RemoveDynamic(this, &AArenaLobby::HandleServersFound);
			Sessions->OnServersFound.AddDynamic(this, &AArenaLobby::HandleServersFound);
			Sessions->FindSessions();
		}
	}
}

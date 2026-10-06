// Copyright Epic Games, Inc. All Rights Reserved.


#include "ArenaPlayerController.h"
#include "ArenaLobbyWidget.h"
#include "ArenaControls.h"
#include "ArenaFriendsSubsystem.h"
#include "ArenaPauseMenu.h"
#include "ArenaBuilding.h"
#include "ArenaBuildComponent.h"
#include "ArenaVitalsComponent.h"
#include "GameFramework/DamageType.h"
#include "ArenaGameMode.h"
#include "ArenaLockerSave.h"
#include "ArenaPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "ArenaSettingsWidget.h"
#include "ArenaEmoteWheel.h"
#include "ArenaInventoryWidget.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "Arena.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Containers/Ticker.h"
#include "FortnitePortingCharacterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "ArenaSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"

namespace
{
	void ApplyArena1v1Look(UWorld* World)
	{
		if (World == nullptr)
		{
			return;
		}

		const FName LookTag(TEXT("Arena1v1RuntimeLook"));
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (It->Tags.Contains(LookTag))
			{
				return;
			}
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APostProcessVolume* Look = World->SpawnActor<APostProcessVolume>(
			APostProcessVolume::StaticClass(), FTransform::Identity, SpawnParams);
		if (Look == nullptr)
		{
			UE_LOG(LogArena, Error, TEXT("Could not create the 1v1 post-process volume."));
			return;
		}

		Look->Tags.Add(LookTag);
		Look->SetReplicates(false);
		Look->bUnbound = true;
		Look->Priority = 1000.0f;
		Look->BlendWeight = 1.0f;

		FPostProcessSettings& Settings = Look->Settings;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.35f;
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(1.10f, 1.10f, 1.10f, 1.0f);
		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(1.04f, 1.04f, 1.04f, 1.0f);
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.25f;
		Settings.bOverride_AmbientOcclusionIntensity = true;
		Settings.AmbientOcclusionIntensity = 0.45f;
		Settings.bOverride_AmbientOcclusionRadius = true;
		Settings.AmbientOcclusionRadius = 100.0f;

		int32 DirectionalLightCount = 0;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				Light->SetIntensity(FMath::Max(Light->Intensity * 1.25f, 10.0f));
				Light->SetLightSourceAngle(FMath::Max(Light->LightSourceAngle, 1.5f));
				Light->SetLightSourceSoftAngle(FMath::Max(Light->LightSourceSoftAngle, 0.5f));
				++DirectionalLightCount;
			}
		}

		int32 SkyLightCount = 0;
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			if (USkyLightComponent* Light = It->GetLightComponent())
			{
				Light->SetIntensity(FMath::Max(Light->Intensity * 1.7f, 1.7f));
				Light->SetIndirectLightingIntensity(FMath::Max(Light->IndirectLightingIntensity, 1.25f));
				Light->SetLightColor(FLinearColor(0.82f, 0.90f, 1.0f));
				Light->bLowerHemisphereIsBlack = false;
				Light->LowerHemisphereColor = FLinearColor(0.12f, 0.15f, 0.20f);
				++SkyLightCount;
			}
		}

		UE_LOG(LogArena, Log, TEXT("Applied 1v1 lighting look: %d directional light(s), %d sky light(s)."),
			DirectionalLightCount, SkyLightCount);
	}
}

AArenaPlayerController::AArenaPlayerController()
{
	BuildComponent = CreateDefaultSubobject<UArenaBuildComponent>(TEXT("BuildComponent"));
	VitalsComponent = CreateDefaultSubobject<UArenaVitalsComponent>(TEXT("VitalsComponent"));
}

void AArenaPlayerController::ArenaBuild(int32 On)
{
	if (BuildComponent)
	{
		BuildComponent->SetBuildingForced(On != 0);
	}
}

void AArenaPlayerController::ArenaPlace(int32 Piece, int32 Material, int32 X, int32 Y, int32 Z, int32 Rotation, int32 Shape)
{
	// testing aid: ArenaPlace <piece 0 wall 1 floor 2 stair 3 roof> <material 0 wood 1 brick 2 metal> <cell x y z> <rotation> <shape index>
	if (BuildComponent)
	{
		BuildComponent->ServerPlace(static_cast<EArenaBuildPiece>(Piece), static_cast<EArenaBuildMaterial>(Material), FIntVector(X, Y, Z), 0, Rotation, false, Shape, UArenaSettingsWidget::GetBuildTextureIndex());
	}
}

void AArenaPlayerController::ArenaHurt(float Amount)
{
	if (APawn* Target = GetPawn(); Target && HasAuthority())
	{
		UGameplayStatics::ApplyDamage(Target, Amount, this, nullptr, UDamageType::StaticClass());      // through the pawn, so it dies and respawns like in a fight
	}
}

void AArenaPlayerController::ArenaHeal(float Amount)
{
	if (VitalsComponent && HasAuthority())
	{
		VitalsComponent->Heal(Amount);
	}
}

void AArenaPlayerController::ArenaShield(float Amount)
{
	if (VitalsComponent && HasAuthority())
	{
		VitalsComponent->AddShield(Amount);
	}
}

void AArenaPlayerController::RespawnNow()
{
	UWorld* World = GetWorld();
	if (!GetPawn() && World && !World->bIsTearingDown)
	{
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			GameMode->RestartPlayer(this);
		}
	}
}

void AArenaPlayerController::HandleVitalsDied()
{
	if (AArenaGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AArenaGameMode>() : nullptr)
	{
		GameMode->OnPlayerKilled(this, VitalsComponent ? VitalsComponent->GetLastDamager() : nullptr);     // score and respawn with the skin
	}
	else
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &AArenaPlayerController::RespawnNow, 4.0f, false);
	}
}

void AArenaPlayerController::RestoreLockerOutfit()
{
	if (bOutfitRestored || !IsLocalController() || !GetWorld() || GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		return;
	}
	APawn* Controlled = GetPawn();
	AArenaPlayerState* ArenaState = GetPlayerState<AArenaPlayerState>();
	if (!Controlled || !ArenaState)
	{
		if (++OutfitAttempts < 60)
		{
			GetWorldTimerManager().SetTimer(OutfitTimer, this, &AArenaPlayerController::RestoreLockerOutfit, 0.25f, false);
		}
		return;
	}
	bOutfitRestored = true;

	const UArenaLockerSave* Save = Cast<UArenaLockerSave>(UGameplayStatics::LoadGameFromSlot(TEXT("ArenaLocker"), 0));
	if (!Save || !Save->Outfit.IsValid())
	{
		return;
	}
	const FString OutfitPath = Save->Outfit.ToString();
	if (Controlled->GetClass()->GetPathName() == OutfitPath)
	{
		return;
	}
	// the same calls the lobby makes: pickaxe and glider first, then the outfit, then its styles
	if (Save->Pickaxe.IsValid())
	{
		ArenaState->Server_SetPickaxe(Save->Pickaxe.ToString());
	}
	if (Save->Glider.IsValid())
	{
		ArenaState->Server_SetGlider(Save->Glider.ToString());
	}
	ArenaState->Server_RequestEquipSkin(OutfitPath);
	if (Save->OutfitStyles.Num() > 0)
	{
		ArenaState->Server_SetSkinStyles(Save->OutfitStyles);
	}
}

void AArenaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController() && GetWorld() && GetWorld()->GetMapName().Contains(TEXT("1v1")))
	{
		ApplyArena1v1Look(GetWorld());
	}

	if (HasAuthority() && VitalsComponent)
	{
		VitalsComponent->OnDied.AddUObject(this, &AArenaPlayerController::HandleVitalsDied);
	}
	RestoreLockerOutfit();
	if (HasAuthority() && GetWorld() && !GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		if (AArenaPlayerState* ArenaState = GetPlayerState<AArenaPlayerState>())
		{
			ArenaState->Server_SetLobbyIdle(false);
		}
	}

	// Video settings saved from the settings screen (Fortnite defaults on first launch)
	if (IsLocalPlayerController())
	{
		UArenaSettingsWidget::ApplySavedSettings();
	}

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogArena, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}

	if (IsLocalPlayerController() && GetWorld() && !GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		UArenaLobbyWidget* SocialOverlay = CreateWidget<UArenaLobbyWidget>(this, UArenaLobbyWidget::StaticClass());
		MatchSocialOverlay = SocialOverlay;
		if (SocialOverlay)
		{
			SocialOverlay->ConfigureAsMatchSocialOverlay();
			SocialOverlay->AddToViewport(1000);
		}
		else
		{
			UE_LOG(LogArena, Error, TEXT("Could not create the match social overlay."));
		}
	}
}

void AArenaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AArenaPlayerController::OnEscape);
		// In the editor ESC stops Play In Editor: F10 opens the settings there too
		InputComponent->BindKey(EKeys::F10, IE_Pressed, this, &AArenaPlayerController::OnEscape);
		// B (emotes) and E (doors) are read in PlayerTick from the player's settings
		if (BuildComponent)
		{
			BuildComponent->SetupInput(InputComponent);
		}
	}

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
		ApplyControlSettings();
		if (!ControlsChangedHandle.IsValid())
		{
			ControlsChangedHandle = ArenaControls::OnChanged().AddUObject(this, &AArenaPlayerController::ApplyControlSettings);
		}
	}
}

void AArenaPlayerController::ApplyControlSettings()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	ArenaControls::ApplyToInputContexts(Subsystem, DefaultMappingContexts);
	if (const APawn* Body = GetPawn())
	{
		if (UFortnitePortingCharacterComponent* Cosmetics = Body->FindComponentByClass<UFortnitePortingCharacterComponent>())
		{
			Cosmetics->PickaxeKey = ArenaControls::Get(ArenaControls::EAction::Pickaxe);
			Cosmetics->ReloadKey = ArenaControls::Get(ArenaControls::EAction::Reload);
		}
	}
}

void AArenaPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalPlayerController())
	{
		return;
	}
	if (InventoryWidget)
	{
		return;
	}
	if (BuildComponent && ArenaControls::WasPressed(this, ArenaControls::EAction::Build))
	{
		BuildComponent->ToggleBuildMode();
	}
	if (ArenaControls::WasPressed(this, ArenaControls::EAction::Interact))
	{
		TryInteract();
	}
	if (ArenaControls::WasPressed(this, ArenaControls::EAction::Inventory))
	{
		if (!PauseMenu.IsValid() && (!MatchSocialOverlay.IsValid() || !MatchSocialOverlay->IsSidePanelOpen())
			&& (!EmoteWheel || !EmoteWheel->IsInViewport()))
		{
			OpenInventory();
		}
	}
	if (ArenaControls::WasPressed(this, ArenaControls::EAction::Emotes))
	{
		if (EmoteWheel && EmoteWheel->IsInViewport())
		{
			HideEmoteWheel();
		}
		else if (!MatchSocialOverlay.IsValid() || !MatchSocialOverlay->IsSidePanelOpen())
		{
			ShowEmoteWheel();
		}
	}
}

bool AArenaPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void AArenaPlayerController::TryInteract()
{
	const APawn* Controlled = GetPawn();
	UWorld* World = GetWorld();
	if (!Controlled || !World)
	{
		return;
	}
	constexpr float Reach = 300.0f;
	AArenaBuilding* BestBuilding = nullptr;
	int32 BestDoor = INDEX_NONE;
	float BestDistance = Reach;
	for (TActorIterator<AArenaBuilding> It(World); It; ++It)
	{
		float Distance = 0.0f;
		const int32 Door = It->FindDoorNear(Controlled->GetActorLocation(), BestDistance, Distance);
		if (Door != INDEX_NONE && Distance < BestDistance)
		{
			BestDistance = Distance;
			BestBuilding = *It;
			BestDoor = Door;
		}
	}
	if (BestBuilding)
	{
		ServerToggleDoor(BestBuilding, BestDoor);
	}
}

void AArenaPlayerController::ServerToggleDoor_Implementation(AArenaBuilding* Building, int32 DoorIndex)
{
	const APawn* Controlled = GetPawn();
	float Distance = 0.0f;
	// the server checks the reach again (with some slack for lag)
	if (Building && Controlled && Building->FindDoorNear(Controlled->GetActorLocation(), 450.0f, Distance) == DoorIndex)
	{
		Building->ToggleDoor(DoorIndex, Controlled->GetActorLocation());
	}
}

void AArenaPlayerController::OnEscape()
{
	if (InventoryWidget)
	{
		CloseInventory();
		return;
	}
	if (EmoteWheel && EmoteWheel->IsInViewport())
	{
		HideEmoteWheel();
		return;
	}
	if (UArenaLobbyWidget* SocialOverlay = MatchSocialOverlay.Get();
		SocialOverlay && SocialOverlay->IsSidePanelOpen())
	{
		SocialOverlay->ToggleSidePanel();
		return;
	}
	// the lobby keeps its settings screen, a match gets the pause menu
	if (GetWorld() && GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		OpenSettings();
	}
	else if (UArenaLobbyWidget* SocialOverlay = MatchSocialOverlay.Get())
	{
		SocialOverlay->ToggleSidePanel();
	}
}

void AArenaPlayerController::TogglePauseMenu()
{
	if (PauseMenu.IsValid())
	{
		HidePauseMenu();
	}
	else
	{
		ShowPauseMenu();
	}
}

void AArenaPlayerController::ShowPauseMenu()
{
	if (!IsLocalController() || PauseMenu.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	PauseMenu = SNew(SArenaPauseMenu).Controller(this);
	GEngine->GameViewport->AddViewportWidgetContent(PauseMenu.ToSharedRef(), 100);

	// the game goes on (it is multiplayer): only the input is taken, and the mouse works on the menu
	SetShowMouseCursor(true);
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	SetIgnoreLookInput(true);
	SetIgnoreMoveInput(true);
	if (BuildComponent)
	{
		BuildComponent->SetExternalCombatBlock(true);
	}

	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = Instance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->OnFriendsChanged.AddUniqueDynamic(this, &AArenaPlayerController::HandleFriendsChangedForMenu);
			Friends->RefreshFriends();
		}
	}
}

void AArenaPlayerController::HidePauseMenu()
{
	if (!PauseMenu.IsValid())
	{
		return;
	}
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(PauseMenu.ToSharedRef());
	}
	PauseMenu.Reset();
	SetShowMouseCursor(false);
	SetInputMode(FInputModeGameOnly());
	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);
	if (BuildComponent)
	{
		BuildComponent->SetExternalCombatBlock(false);
	}
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = Instance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->OnFriendsChanged.RemoveDynamic(this, &AArenaPlayerController::HandleFriendsChangedForMenu);
		}
	}
}

void AArenaPlayerController::HandleFriendsChangedForMenu()
{
	if (PauseMenu.IsValid())
	{
		PauseMenu->RebuildFriends();
	}
}

void AArenaPlayerController::LeaveMatch()
{
	HidePauseMenu();
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone)
	{
		ServerTravelToArenaMap(TEXT("/Game/Lobby/Lvl_Lobby"));
		return;
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Lobby/Lvl_Lobby")));
}

void AArenaPlayerController::ServerTravelToArenaMap_Implementation(const FString& MapPath)
{
	UWorld* World = GetWorld();
	const FString NormalizedPath = UWorld::RemovePIEPrefix(MapPath).TrimStartAndEnd();
	if (!HasAuthority() || World == nullptr
		|| !NormalizedPath.StartsWith(TEXT("/Game/"))
		|| NormalizedPath.Contains(TEXT("?"))
		|| !FPackageName::IsValidLongPackageName(NormalizedPath))
	{
		UE_LOG(LogArena, Warning, TEXT("Rejected invalid server travel request: %s"), *MapPath);
		return;
	}

	UArenaSessionSubsystem::WriteRuntimeLog(FString::Printf(TEXT("Party server travel requested to %s."), *NormalizedPath));
	if (!World->ServerTravel(NormalizedPath, true))
	{
		UArenaSessionSubsystem::WriteRuntimeLog(FString::Printf(TEXT("Party server travel failed to start: %s."), *NormalizedPath));
		UE_LOG(LogArena, Error, TEXT("Could not start server travel to %s"), *NormalizedPath);
	}
}

void AArenaPlayerController::OpenSettings()
{
	if (IsLocalPlayerController())
	{
		UArenaSettingsWidget::Open(this, nullptr);
	}
}

void AArenaPlayerController::OpenInventory()
{
	if (!IsLocalPlayerController() || InventoryWidget || GetPawn() == nullptr)
	{
		return;
	}

	UFortnitePortingCharacterComponent* WeaponInventory = GetPawn()->FindComponentByClass<UFortnitePortingCharacterComponent>();
	if (WeaponInventory == nullptr)
	{
		UE_LOG(LogArena, Warning, TEXT("Cannot open inventory: the controlled pawn has no weapon inventory."));
		return;
	}

	InventoryWidget = CreateWidget<UArenaInventoryWidget>(this, UArenaInventoryWidget::StaticClass());
	if (InventoryWidget == nullptr)
	{
		UE_LOG(LogArena, Error, TEXT("Could not create the inventory screen."));
		return;
	}
	InventoryWidget->InitializeInventory(this, WeaponInventory);
	InventoryWidget->AddToViewport(2000);

	if (BuildComponent)
	{
		BuildComponent->SetExternalCombatBlock(true);
	}
	if (UFortnitePortingCharacterComponent* Cosmetics = GetPawn()->FindComponentByClass<UFortnitePortingCharacterComponent>())
	{
		Cosmetics->SetGameplayInputBlocked(true);
		Cosmetics->bCombatBlocked = true;
	}

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(InventoryWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	SetIgnoreLookInput(true);
	SetIgnoreMoveInput(true);
}

void AArenaPlayerController::CloseInventory()
{
	if (InventoryWidget)
	{
		InventoryWidget->RemoveFromParent();
		InventoryWidget = nullptr;
	}
	if (IsLocalPlayerController())
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
		if (BuildComponent)
		{
			BuildComponent->SetExternalCombatBlock(false);
		}
		if (APawn* ControlledPawn = GetPawn())
		{
			if (UFortnitePortingCharacterComponent* Cosmetics = ControlledPawn->FindComponentByClass<UFortnitePortingCharacterComponent>())
			{
				Cosmetics->SetGameplayInputBlocked(false);
				Cosmetics->bCombatBlocked = BuildComponent && BuildComponent->IsCombatBlocked();
			}
		}
	}
}

void AArenaPlayerController::ArenaConnect(const FString& Address)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->ConnectToAddress(Address);
		}
	}
}

void AArenaPlayerController::ArenaLogin()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->Login(TEXT("accountportal"));
		}
	}
}

void AArenaPlayerController::ArenaEmote(int32 Index, float StopAfterSeconds)
{
	RunEmoteTest(Index, StopAfterSeconds);
}

void AArenaPlayerController::RunEmoteTest(int32 Index, float StopAfterSeconds)
{
	// The character appears a moment after the map loads, and the controller changes when connecting to a host:
	// a global ticker keeps retrying until a local character with emotes exists
	struct FState { int32 Attempts = 0; bool bPlaying = false; double PlayedAt = 0.0; TWeakObjectPtr<UFortnitePortingCharacterComponent> Component; };
	TSharedRef<FState> State = MakeShared<FState>();

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, Index, StopAfterSeconds](float) -> bool
	{
		if (State->bPlaying)
		{
			if (StopAfterSeconds > 0.0f && FPlatformTime::Seconds() - State->PlayedAt >= StopAfterSeconds)
			{
				if (UFortnitePortingCharacterComponent* Playing = State->Component.Get())
				{
					UE_LOG(LogArena, Log, TEXT("ArenaEmote: stopping the emote now"));
					Playing->StopEmote();
				}
				return false;
			}
			return StopAfterSeconds > 0.0f;
		}

		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		APlayerController* LocalController = World ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		APawn* ControlledPawn = LocalController ? LocalController->GetPawn() : nullptr;
		UFortnitePortingCharacterComponent* Cosmetics = ControlledPawn ? ControlledPawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;

		int32 WithMusic = 0;
		if (Cosmetics != nullptr)
		{
			for (UFortnitePortingEmoteData* Emote : Cosmetics->Emotes)
			{
				if (Emote != nullptr && !Emote->Sounds.IsEmpty())
				{
					++WithMusic;
					if (WithMusic == Index + 1)
					{
						Cosmetics->StopLobbyIdle();
						Cosmetics->RequestEmote(Emote);
						State->bPlaying = true;
						State->PlayedAt = FPlatformTime::Seconds();
						State->Component = Cosmetics;
						UE_LOG(LogArena, Log, TEXT("ArenaEmote: playing %s on %s"), *Emote->GetName(), *ControlledPawn->GetName());
						return StopAfterSeconds > 0.0f;
					}
				}
			}
		}

		if (++State->Attempts >= 240)
		{
			UE_LOG(LogArena, Warning, TEXT("ArenaEmote: gave up. pawn=%s component=%d emotes=%d with music=%d"),
				ControlledPawn ? *ControlledPawn->GetName() : TEXT("none"), Cosmetics != nullptr, Cosmetics ? Cosmetics->Emotes.Num() : 0, WithMusic);
			return false;
		}
		return true;
	}), 1.0f);
}

void AArenaPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);

	if (HasAuthority())
	{
		if (InPawn)
		{
			// a new body always starts with full health and shield
			if (VitalsComponent && (VitalsComponent->IsDead() || bPawnLost))
			{
				VitalsComponent->ResetVitals();
			}
			bPawnLost = false;
		}
		else if (GetWorld() && !GetWorld()->bIsTearingDown && !GetWorld()->GetMapName().Contains(TEXT("Lobby")))
		{
			bPawnLost = true;
			// the body is gone without dying (it fell out of the world): that counts as a death, back after a moment with the skin
			if (VitalsComponent && !VitalsComponent->IsDead())
			{
				HandleVitalsDied();
			}
		}
	}

	// the stairs of the build arena are steep: a taller step and a steeper walkable slope so the player simply walks up them
	if (InPawn && GetWorld() && GetWorld()->GetMapName().Contains(TEXT("1v1")))
	{
		if (UCharacterMovementComponent* Movement = InPawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			Movement->MaxStepHeight = 70.0f;
			Movement->SetWalkableFloorAngle(52.0f);
		}
	}

	// a body in a match is always driven with the game input mode: the lobby leaves the menu mode (and the cursor) behind on some paths
	if (InPawn && IsLocalController() && GetWorld() && !GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
	}

	// B opens the emote wheel here: the character must not also cycle through its emotes on that key
	if (InPawn)
	{
		if (UFortnitePortingCharacterComponent* Cosmetics = InPawn->FindComponentByClass<UFortnitePortingCharacterComponent>())
		{
			Cosmetics->EmoteKey = EKeys::Invalid;
			Cosmetics->PickaxeKey = ArenaControls::Get(ArenaControls::EAction::Pickaxe);
			Cosmetics->ReloadKey = ArenaControls::Get(ArenaControls::EAction::Reload);
		}
	}
}

void AArenaPlayerController::ShowEmoteWheel()
{
	if (!IsLocalPlayerController() || (EmoteWheel != nullptr && EmoteWheel->IsInViewport()))
	{
		return;
	}
	if (UArenaLobbyWidget* SocialOverlay = MatchSocialOverlay.Get();
		SocialOverlay && SocialOverlay->IsSidePanelOpen())
	{
		return;
	}
	APawn* ControlledPawn = GetPawn();
	UFortnitePortingCharacterComponent* Cosmetics = ControlledPawn ? ControlledPawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;
	if (Cosmetics == nullptr)
	{
		return;
	}
	EmoteWheel = UArenaEmoteWheel::Show(this, Cosmetics);
}

void AArenaPlayerController::HideEmoteWheel()
{
	if (EmoteWheel)
	{
		EmoteWheel->Hide();
		EmoteWheel = nullptr;
	}
}

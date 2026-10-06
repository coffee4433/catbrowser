// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArenaPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UArenaBuildComponent;
class UArenaLobbyWidget;
class UArenaInventoryWidget;
class UArenaVitalsComponent;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AArenaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	AArenaPlayerController();

	/** Console testing aid: ArenaPlace <piece 0 wall 1 floor 2 stair 3 roof> <material 0 wood 1 brick 2 metal> <cell x y z> <rotation> <shape index> */
	UFUNCTION(Exec)
	void ArenaPlace(int32 Piece, int32 Material, int32 X, int32 Y, int32 Z, int32 Rotation, int32 Shape);

	/** Console: "ArenaHurt 30" damages your own pawn (shield first), to see the health bar react */
	UFUNCTION(Exec)
	void ArenaHurt(float Amount = 25.0f);

	/** Console: "ArenaHeal 20" and "ArenaShield 25" give health or shield back */
	UFUNCTION(Exec)
	void ArenaHeal(float Amount = 20.0f);

	UFUNCTION(Exec)
	void ArenaShield(float Amount = 25.0f);

	/** Console: "ArenaBuild 1" turns the Fortnite style building on in any map, 0 turns it off */
	UFUNCTION(Exec)
	void ArenaBuild(int32 On = 1);

	/** Console: "ArenaConnect 192.168.1.20" joins a host directly by IP (LAN, VPN or port forwarded), no Epic login needed */
	UFUNCTION(Exec)
	void ArenaConnect(const FString& Address);

	/** Console: "ArenaLogin" starts the Epic Games login (browser), for testing the online services */
	UFUNCTION(Exec)
	void ArenaLogin();

	/** Console: "ArenaEmote" plays the Nth emote that has music (0 = first); StopAfterSeconds > 0 ends it later on the current pawn (for testing the music loop) */
	UFUNCTION(Exec)
	void ArenaEmote(int32 Index = 0, float StopAfterSeconds = 0.0f);

	/**
	 * Plays the Nth emote with music on the local character as soon as it exists, and optionally stops it later.
	 * Also started by the command line parameter -ArenaEmoteTest=Index[,StopAfterSeconds] (testing aid).
	 */
	static void RunEmoteTest(int32 Index, float StopAfterSeconds);

public:

	/** The menu of a match: continue, settings, leave, and the social panel on the right */
	void ShowPauseMenu();
	void HidePauseMenu();
	void TogglePauseMenu();

	/** Returns the whole network party to the lobby without destroying its LAN session. */
	void LeaveMatch();

	UFUNCTION(Server, Reliable)
	void ServerTravelToArenaMap(const FString& MapPath);

	void OpenSettings();
	void CloseInventory();

	UFUNCTION()
	void HandleFriendsChangedForMenu();

protected:

	TSharedPtr<class SArenaPauseMenu> PauseMenu;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	TWeakObjectPtr<UArenaLobbyWidget> MatchSocialOverlay;
	UPROPERTY(Transient) TObjectPtr<UArenaInventoryWidget> InventoryWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Puts the keys of the settings (movement, pickaxe) into the input contexts and the pawn */
	void ApplyControlSettings();
	FDelegateHandle ControlsChangedHandle;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

	/** ESC opens settings in the lobby and toggles the social side panel during a match */
	void OnEscape();

	/** B held: glass emote selector, released: closes it */
	void ShowEmoteWheel();
	void HideEmoteWheel();

	/** E: opens or closes the closest door in reach */
	void TryInteract();
	void OpenInventory();

	UFUNCTION(Server, Reliable)
	void ServerToggleDoor(class AArenaBuilding* Building, int32 DoorIndex);

	virtual void SetPawn(APawn* InPawn) override;

	UPROPERTY(Transient) TObjectPtr<class UArenaEmoteWheel> EmoteWheel;

	/** Fortnite style building (Q, Z/X/C/V, G...), only active in the build arenas */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArenaBuildComponent> BuildComponent;

	/** Health and shield (100 each) and the bottom left HUD */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArenaVitalsComponent> VitalsComponent;

	FTimerHandle RespawnTimer;
	void RespawnNow();

	/** Server: the health reached zero, the game mode scores it and brings the player back with the same skin */
	void HandleVitalsDied();

	/** Local player in a match map: put on the outfit, styles, pickaxe and glider picked in the lobby locker (the lobby actor only exists in the lobby) */
	void RestoreLockerOutfit();
	FTimerHandle OutfitTimer;
	int32 OutfitAttempts = 0;
	bool bOutfitRestored = false;
	bool bPawnLost = false;

};

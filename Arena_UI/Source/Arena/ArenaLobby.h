// Fortnite style lobby: frames the player's character with a front camera, shows UArenaLobbyWidget (lobby + locker)
// and hands control back to the player when PLAY is pressed

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaLobbyWidget.h"
#include "ArenaSessionSubsystem.h"
#include "ArenaLobby.generated.h"

class ACameraActor;
class APlayerController;
class AStaticMeshActor;
class UMaterialInterface;

UCLASS()
class ARENA_API AArenaLobby : public AActor
{
	GENERATED_BODY()

public:
	AArenaLobby();

	/** Widget shown in the lobby (the C++ layout, or a Widget Blueprint child of UArenaLobbyWidget) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	TSubclassOf<UArenaLobbyWidget> LobbyWidgetClass;

	/** Name shown over the character */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	FText PlayerName;

	/** Distance of the lobby camera in front of the character */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera")
	float CameraDistance = 650.0f;

	/** World height (cm) the screen shows. The vertical FOV is locked, so the whole body fits at any resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera")
	float FrameHeight = 240.0f;

	/** Empty space under the feet, as a fraction of the screen height */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera", meta = (ClampMin = "0", ClampMax = "0.5"))
	float FeetMargin = 0.05f;

	/** Horizontal screen position of the character while the locker is open (0 = left edge, 0.5 = centre) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera", meta = (ClampMin = "0", ClampMax = "1"))
	float LockerScreenX = 0.26f;

	/** Screen height (cm) at full locker zoom, and the height above the feet it centres on (fraction of the body) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera")
	float LockerZoomFrameHeight = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera", meta = (ClampMin = "0", ClampMax = "1"))
	float LockerZoomFocus = 0.8f;

	/** How fast the camera slides between the lobby and the locker framing */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera")
	float CameraMoveSpeed = 7.0f;

	/** Seconds the camera takes to fly back behind the character after PLAY */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Camera")
	float PlayBlendTime = 0.8f;

	/** Blue backdrop placed behind the character while in the lobby, like Fortnite's lobby stage (none = see the level) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	TObjectPtr<UMaterialInterface> BackdropMaterial;

	/** Spawn the neutral pad under the character. Off when the level brings its own stage (the lobby map) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	bool bSpawnStageFloor = true;

	/** Distance of the backdrop behind the character */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	float BackdropDistance = 110.0f;

	/** Studio lights on the character while in the lobby (Fortnite's lobby key + rim lights). 0 = off */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	float KeyLightIntensity = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	float RimLightIntensity = 180.0f;

	/** Exposure compensation of the lobby camera (negative = darker, keeps skin colours from washing out) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Stage")
	float LobbyExposureBias = -0.5f;

	/** Every BP_<Name> under this folder (FortnitePorting's character exports) is listed in the locker */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Locker")
	FString CharactersPath = TEXT("/Game/FortnitePorting/Characters");

	/** Save slot remembering the chosen outfit between sessions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Locker")
	FString SaveSlot = TEXT("ArenaLocker");

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void StartMatch();

	/** Swaps the player's character for the outfit at Index of the locker list */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void EquipSkin(int32 Index);

	/** Wears an outfit with the given option in each of its style channels */
	void EquipSkinWithStyles(int32 Index, const TArray<int32>& Styles);

	UFUNCTION(BlueprintPure, Category = "Lobby|Locker")
	const TArray<FArenaSkinEntry>& GetSkins() const { return Skins; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TryEnterLobby();
	void DiscoverSkins();
	void DiscoverItems();
	void SaveLocker() const;
	/** Gives the player's character the pickaxe / glider chosen in the locker */
	void ApplyLockerItems(APawn* Pawn) const;
	class UFortnitePortingCharacterComponent* FindCosmeticComponent(APawn* Pawn) const;
	int32 FindSkinIndex(const UClass* PawnClass) const;
	/** The outfit that owns a saved class path, or INDEX_NONE */
	int32 FindSkinForPath(const FString& ClassPath) const;
	void PrepareLobbyPawn(APawn* Pawn) const;
	void ApplyLobbyInput() const;
	void ComputeCameraTarget(bool bLocker, FVector& OutLocation, FRotator& OutRotation, float& OutFOV) const;

	UFUNCTION()
	void HandlePlayPressed();

	UFUNCTION()
	void HandleSkinSelected(int32 SkinIndex);

	UFUNCTION()
	void HandleStyleSelected(int32 SkinIndex, int32 ChannelIndex, int32 OptionIndex);

	/** Option picked in each style channel of the worn outfit */
	TArray<int32> StyleSelection;

	UFUNCTION()
	void HandleLockerItemSelected(int32 LockerCategory, int32 ItemIndex);

	UFUNCTION()
	void HandlePageChanged(bool bLocker);

	UFUNCTION()
	void HandleHostPressed();

	UFUNCTION()
	void HandleHostLoginComplete(bool bSuccess, const FString& Error);

	void ProceedWithHosting();

	UFUNCTION()
	void HandleJoinPressed();

	UFUNCTION()
	void HandleJoinLoginComplete(bool bSuccess, const FString& Error);

	UFUNCTION()
	void HandleJoinServerSelected(int32 ServerIndex);

	UFUNCTION()
	void HandleServersFound(const TArray<FArenaServerInfo>& Servers);

	UFUNCTION()
	void HandleHostResult(bool bSuccess);

	UPROPERTY(Transient)
	TObjectPtr<UArenaLobbyWidget> LobbyWidget;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> LobbyCamera;

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> Backdrop;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Skins;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Pickaxes;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Gliders;

	/** Choices kept across outfit changes (a new character gets them too) */
	UPROPERTY(Transient)
	TObjectPtr<UObject> ChosenPickaxe;

	UPROPERTY(Transient)
	TObjectPtr<UObject> ChosenGlider;

	TWeakObjectPtr<APlayerController> Controller;
	FTimerHandle RetryTimer;
	FTimerHandle CleanupTimer;
	FTimerHandle PostEquipTimer;
	int32 Attempts = 0;

	/** Where the character stands in the lobby (capsule centre) and the direction it faces */
	FVector StageCenter = FVector::ZeroVector;
	FVector StageForward = FVector::ForwardVector;
	float StageHalfHeight = 90.0f;
	FVector LobbyFocusCenter = FVector::ZeroVector;
	float LobbyFocusFrameHeight = 0.0f;

	FVector CameraTargetLocation = FVector::ZeroVector;
	bool bInLobby = false;
	bool bLockerView = false;
	/** Smoothed locker zoom (0 = full body, 1 = upper body close-up) */
	float LockerZoom = 0.0f;
	/** Smoothed character turn dragged in the locker */
	float LockerYaw = 0.0f;
	float StageYaw = 0.0f;
	/** Vertical mesh offset that keeps the lowest foot on the stage floor (lobby idles lift some skins) */
	float FootOffset = 0.0f;
	TWeakObjectPtr<APawn> FootPawn;
	float FootBaseZ = 0.0f;
	void KeepFeetOnFloor(APawn* Pawn, float DeltaSeconds);
	void ResetFeet();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> StageLights;

	void SpawnStageLights(const FActorSpawnParameters& Params);

	void CheckAutoJoinRequest();
	FTimerHandle AutoJoinTimer;
	FString PendingAutoJoinFriend;
	FString PendingAutoJoinSession;
	bool bAutoJoinPending = false;
};

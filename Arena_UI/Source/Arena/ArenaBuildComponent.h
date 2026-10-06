// Fortnite style building for a local player: Q toggles build mode, Z/X/C/V pick wall/floor/stair/roof, F5/F6/F7 pick the material,
// R turns the piece, left click places it and G opens the edit grid on the piece you look at. Lives on the player controller.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "ArenaBuildTypes.h"
#include "ArenaBuildComponent.generated.h"

class AArenaBuildPiece;
class AStaticMeshActor;
class UInputAction;
class UInputComponent;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class SBox;
class SWidget;
class UTexture2D;
struct FSlateBrush;

UCLASS(ClassGroup = (Arena), meta = (BlueprintSpawnableComponent))
class ARENA_API UArenaBuildComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UArenaBuildComponent();

	/** Called by the player controller once its input component exists */
	void SetupInput(UInputComponent* Input);

	/** Building is on in the build arenas (maps with "1v1" in the name) or when the console says so */
	bool IsBuildingEnabled() const;
	bool IsCombatBlocked() const { return bBuildMode || bExternalCombatBlock; }
	void SetBuildingForced(bool bOn);
	void ToggleBuildMode();

	/** Menus on screen (the pause menu): the mouse must not shoot or swing while it is open */
	void SetExternalCombatBlock(bool bBlock);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerPlace(EArenaBuildPiece Piece, EArenaBuildMaterial Material, FIntVector Cell, int32 Edge, int32 Rotation, bool bMirror, int32 ShapeIndex, int32 BuildTextureIndex);

	UFUNCTION(Server, Reliable)
	void ServerEdit(AArenaBuildPiece* PieceActor, int32 Mask, bool bSpiral);

private:

	struct FTarget
	{
		bool bValid = false;
		FIntVector Cell = FIntVector::ZeroValue;
		int32 Edge = 0;
		int32 Rotation = 0;
		int32 Shape = INDEX_NONE;
	};

	APlayerController* GetController() const;
	FTarget ComputeTarget() const;
	bool IsSlotTaken(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge) const;

	// input handlers
	void SelectPiece(EArenaBuildPiece Piece);
	void SelectMaterial(EArenaBuildMaterial Material);
	void SelectWall() { SelectPiece(EArenaBuildPiece::Wall); }
	void SelectFloor() { SelectPiece(EArenaBuildPiece::Floor); }
	void SelectStair() { SelectPiece(EArenaBuildPiece::Stair); }
	void SelectRoof() { SelectPiece(EArenaBuildPiece::Roof); }
	void SelectWood() { SelectMaterial(EArenaBuildMaterial::Wood); }
	void SelectBrick() { SelectMaterial(EArenaBuildMaterial::Brick); }
	void SelectMetal() { SelectMaterial(EArenaBuildMaterial::Metal); }
	void RotatePiece();
	void OnPlace();
	void TryPlace();
	void OnCancelPressed();
	void CycleMaterial();
	void ToggleEdit();
	void CancelEdit();

	void SetBuildMode(bool bOn, bool bQuiet = false);
	void EndEdit(bool bPlaySound);
	void BeginEdit();
	void ConfirmEdit();
	AArenaBuildPiece* PieceUnderCrosshair() const;

	// local only: the blue/red preview
	void UpdateGhost();
	void HideGhost();

	// local only: the hot bar and the edit grid
	void RebuildHud();
	void EnsureHud();
	void RemoveHud();
	TSharedRef<SWidget> MakeBar();
	TSharedRef<SWidget> MakeMaterialPanel();
	TSharedRef<SWidget> MakePieceHealth();
	TSharedRef<SWidget> MakeEditPrompt();

	/** Over the piece the crosshair is on: the key that edits it */
	void UpdateEditPrompt(APlayerController* PC);

	/** Finds the piece that was just hit nearby and puts its health card over it */
	void UpdatePieceHealth(APlayerController* PC);

	void EnableBuildContext(bool bOn);

	bool bForced = false;
	bool bBuildMode = false;
	bool bExternalCombatBlock = false;

	/** G was pressed outside build mode: build mode is only borrowed for the edit (no bar, no sounds) and given back when it ends */
	bool bBorrowedBuildMode = false;
	bool bEditing = false;
	bool bInputReady = false;

	EArenaBuildPiece SelectedPiece = EArenaBuildPiece::Wall;
	EArenaBuildMaterial SelectedMaterial = EArenaBuildMaterial::Wood;
	int32 UserRotation = 0;

	// edit state
	TWeakObjectPtr<AArenaBuildPiece> EditPiece;
	uint16 EditMask = 0;
	bool bEditSpiral = false;

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> Ghost;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PlaceAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> BuildContext;

	TSharedPtr<SBox> HudRoot;
	bool bHudAdded = false;

	TSharedPtr<FSlateRoundedBoxBrush> PanelBrush;
	TSharedPtr<FSlateRoundedBoxBrush> SlotBrush;
	TSharedPtr<FSlateRoundedBoxBrush> SlotSelectedBrush;
	TSharedPtr<FSlateRoundedBoxBrush> MaterialSlotBrush;
	TSharedPtr<FSlateRoundedBoxBrush> MaterialSelectedBrush;
	TSharedPtr<FSlateRoundedBoxBrush> BarFillBrush;
	TSharedPtr<FSlateBrush> MaterialIconBrush[3];

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MaterialIcons[3];

	// "G Editar" over the piece you aim at while building
	bool bEditPromptVisible = false;
	FVector2D EditPromptPosition = FVector2D::ZeroVector;
	TSharedPtr<FSlateRoundedBoxBrush> KeyCapBrush;

	// the health card of the piece being broken
	bool bPieceCardVisible = false;
	FVector2D PieceCardPosition = FVector2D::ZeroVector;
	float PieceCardFraction = 1.0f;
	FText PieceCardText;
	FLinearColor PieceCardColor = FLinearColor::White;

	TSharedPtr<FSlateRoundedBoxBrush> TileOnBrush;
	TSharedPtr<FSlateRoundedBoxBrush> TileOffBrush;

	// edit mode: the blue tiles live ON the piece, you aim at them with the crosshair (Fortnite style)
	void BuildEditTiles();
	void DestroyEditTiles();
	void UpdateEditTiles();
	void PaintTile(int32 Bit, bool bOn);
	void OnPlaceReleased();

	bool bPainting = false;
	bool bPaintValue = false;
	bool bPaintedThisPress = false;

	// holding the left button keeps building, one piece every few hundredths of a second
	bool bHoldPlacing = false;
	float HoldTimer = 0.0f;
	int32 HoveredBit = INDEX_NONE;
	int32 LastPaintedBit = INDEX_NONE;
	TArray<int32> EditTileBits;

	UPROPERTY(Transient)
	TObjectPtr<AActor> EditActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> EditTileComps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> EditTileVisualComps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> EditTileMats;
};

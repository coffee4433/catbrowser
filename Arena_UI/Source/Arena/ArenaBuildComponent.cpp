#include "ArenaBuildComponent.h"

#include "Arena.h"
#include "ArenaBuildFeedback.h"
#include "ArenaControls.h"
#include "FortnitePortingCharacterComponent.h"
#include "ArenaBuildPiece.h"
#include "ArenaBuildSubsystem.h"
#include "ArenaSettingsWidget.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FIntPoint Directions[4] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };

	FSlateFontInfo BoldFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	}
}

UArenaBuildComponent::UArenaBuildComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	PanelBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.02f, 0.03f, 0.05f, 0.85f), 18.0f);
	SlotBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), 12.0f);
	SlotSelectedBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.20f, 0.55f, 1.0f, 0.55f), 12.0f);
	MaterialSlotBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1.0f, 1.0f, 1.0f, 0.07f), 14.0f);
	MaterialSelectedBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.20f, 0.55f, 1.0f, 0.40f), 14.0f, FLinearColor(0.75f, 0.9f, 1.0f, 1.0f), 2.5f);
	KeyCapBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.20f, 0.55f, 1.0f, 1.0f), 8.0f);
	BarFillBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, 5.0f);
	TileOnBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.05f, 0.62f, 0.95f, 1.0f), 4.0f);     // the blue of the original grids
	TileOffBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f), 4.0f);    // the grey ones
}

APlayerController* UArenaBuildComponent::GetController() const
{
	return Cast<APlayerController>(GetOwner());
}

bool UArenaBuildComponent::IsBuildingEnabled() const
{
	if (bForced)
	{
		return true;
	}
	const UWorld* World = GetWorld();
	return World && World->GetMapName().Contains(TEXT("1v1"));
}

void UArenaBuildComponent::SetBuildingForced(bool bOn)
{
	bForced = bOn;
	if (!IsBuildingEnabled())
	{
		SetBuildMode(false);
	}
}

void UArenaBuildComponent::SetExternalCombatBlock(bool bBlock)
{
	bExternalCombatBlock = bBlock;
	if (bBlock)
	{
		bHoldPlacing = false;
		bPainting = false;
		bPaintedThisPress = false;
		if (bEditing)
		{
			CancelEdit();
		}
		if (bBuildMode)
		{
			SetBuildMode(false);
		}
	}
	if (APlayerController* PC = GetController())
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UFortnitePortingCharacterComponent* Cosmetics = Pawn->FindComponentByClass<UFortnitePortingCharacterComponent>())
			{
				Cosmetics->bCombatBlocked = bBuildMode || bExternalCombatBlock;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------ input

void UArenaBuildComponent::SetupInput(UInputComponent* Input)
{
	APlayerController* PC = GetController();
	if (!Input || !PC || !PC->IsLocalController())
	{
		return;
	}
	// the keys are read every frame from the player's settings (see ArenaControls), so changing one applies at once

	// the mouse buttons are built as an input context with higher priority that eats them, so the fight keys do not fire while building
	PlaceAction = NewObject<UInputAction>(this, TEXT("BuildPlaceAction"));
	PlaceAction->ValueType = EInputActionValueType::Boolean;
	PlaceAction->bConsumeInput = true;
	CancelAction = NewObject<UInputAction>(this, TEXT("BuildCancelAction"));
	CancelAction->ValueType = EInputActionValueType::Boolean;
	CancelAction->bConsumeInput = true;
	BuildContext = NewObject<UInputMappingContext>(this, TEXT("BuildContext"));
	BuildContext->MapKey(PlaceAction, EKeys::LeftMouseButton);
	BuildContext->MapKey(CancelAction, EKeys::RightMouseButton);

	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input))
	{
		Enhanced->BindAction(PlaceAction, ETriggerEvent::Started, this, &UArenaBuildComponent::OnPlace);
		Enhanced->BindAction(PlaceAction, ETriggerEvent::Completed, this, &UArenaBuildComponent::OnPlaceReleased);
		Enhanced->BindAction(CancelAction, ETriggerEvent::Started, this, &UArenaBuildComponent::OnCancelPressed);
	}
	bInputReady = true;
}

void UArenaBuildComponent::EnableBuildContext(bool bOn)
{
	APlayerController* PC = GetController();
	if (!PC || !BuildContext)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		if (bOn)
		{
			Subsystem->AddMappingContext(BuildContext, 1000);
		}
		else
		{
			Subsystem->RemoveMappingContext(BuildContext);
		}
	}
}

void UArenaBuildComponent::ToggleBuildMode()
{
	if (IsBuildingEnabled())
	{
		SetBuildMode(!bBuildMode);
	}
}

void UArenaBuildComponent::SetBuildMode(bool bOn, bool bQuiet)
{
	if (bOn && !IsBuildingEnabled())
	{
		return;
	}
	if (bBuildMode == bOn)
	{
		return;
	}
	bBuildMode = bOn;
	if (!bBuildMode)
	{
		CancelEdit();
		HideGhost();
	}
	EnableBuildContext(bBuildMode);
	if (!bOn)
	{
		bBorrowedBuildMode = false;
	}
	if (bBuildMode)
	{
		EnsureHud();
		if (bQuiet)
		{
			RebuildHud();
			return;
		}
		ArenaBuildFeedback::Play2D(this, TEXT("Player_Equip_Blueprint_Cue"));
		if (const APlayerController* PC = GetController())
		{
			ArenaBuildFeedback::PlayAnimation(PC->GetPawn(), ArenaBuildFeedback::EAnim::Equip);
		}
	}

	RebuildHud();
}

void UArenaBuildComponent::SelectPiece(EArenaBuildPiece Piece)
{
	if (!IsBuildingEnabled())
	{
		return;
	}
	if (SelectedPiece != Piece || !bBuildMode)
	{
		ArenaBuildFeedback::Play2D(this, TEXT("Fort_Build_BluePrint_Select_Cue"));
	}
	SelectedPiece = Piece;
	UserRotation = 0;
	SetBuildMode(true);
	RebuildHud();
}

void UArenaBuildComponent::SelectMaterial(EArenaBuildMaterial Material)
{
	if (!IsBuildingEnabled())
	{
		return;
	}
	if (SelectedMaterial != Material)
	{
		ArenaBuildFeedback::Play2D(this, TEXT("Fort_Build_BluePrint_Select_02_short"));
	}
	SelectedMaterial = Material;
	RebuildHud();
}

void UArenaBuildComponent::CycleMaterial()
{
	const int32 Next = (static_cast<int32>(SelectedMaterial) + 1) % 3;
	SelectMaterial(static_cast<EArenaBuildMaterial>(Next));
}

void UArenaBuildComponent::OnCancelPressed()
{
	if (bEditing)
	{
		// right click in the edit: reset the piece to what it was when it was built (all tiles back) and apply it
		if (AArenaBuildPiece* Piece = EditPiece.Get())
		{
			EditMask = ArenaBuild::FullMask(Piece->GetPiece());
			bEditSpiral = false;
			bPainting = false;
			ConfirmEdit();
		}
	}
	else if (bBuildMode)
	{
		CycleMaterial();      // right click: wood -> brick -> metal -> wood
	}
}

void UArenaBuildComponent::RotatePiece()
{
	if (bEditing)
	{
		if (EditPiece.IsValid() && EditPiece->GetPiece() == EArenaBuildPiece::Stair)
		{
			bEditSpiral = !bEditSpiral;      // straight ramp <-> U shaped ramp
		}
		return;
	}
	if (bBuildMode && !bEditing)
	{
		UserRotation = (UserRotation + 1) & 3;
	}
}

// ------------------------------------------------------------------------------------------------ target and ghost

UArenaBuildComponent::FTarget UArenaBuildComponent::ComputeTarget() const
{
	FTarget Target;
	const APlayerController* PC = GetController();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return Target;
	}
	const FVector Location = Pawn->GetActorLocation();
	const float Feet = Location.Z - Pawn->GetSimpleCollisionHalfHeight();
	const FRotator View = PC->GetControlRotation();
	const int32 Heading = FMath::RoundToInt(FRotator::NormalizeAxis(View.Yaw) / 90.0f) & 3;   // 0 +X, 1 +Y, 2 -X, 3 -Y
	const bool bLookingUp = FRotator::NormalizeAxis(View.Pitch) > 25.0f;

	const FIntPoint Here(FMath::FloorToInt(Location.X / ArenaBuild::CellSize), FMath::FloorToInt(Location.Y / ArenaBuild::CellSize));
	const int32 Level = FMath::FloorToInt((Feet + 20.0f) / ArenaBuild::Story);
	const FIntPoint Ahead = Here + Directions[Heading];
	const UCharacterMovementComponent* Movement = Pawn->FindComponentByClass<UCharacterMovementComponent>();
	const bool bIsFalling = Movement && Movement->IsFalling();

	// Floors, stairs and roofs go where the crosshair meets the floor of the current storey (up to two cells away).
	FIntPoint AimedCell = Ahead;
	bool bAimedAtCurrentCell = false;
	{
		FVector Eye;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(Eye, ViewRotation);
		const FVector Dir = ViewRotation.Vector();
		const float PlaneZ = Level * ArenaBuild::Story;
		if (Dir.Z < -0.08f && Eye.Z > PlaneZ)
		{
			const FVector Hit = Eye + Dir * ((PlaneZ - Eye.Z) / Dir.Z);
			const FIntPoint Cell(FMath::FloorToInt(Hit.X / ArenaBuild::CellSize), FMath::FloorToInt(Hit.Y / ArenaBuild::CellSize));
			if (FMath::Abs(Cell.X - Here.X) <= 2 && FMath::Abs(Cell.Y - Here.Y) <= 2)
			{
				if (Cell == Here)
				{
					bAimedAtCurrentCell = true;
				}
				else
				{
					AimedCell = Cell;
				}
			}
		}
	}

	int32 StairPlacementLevel = Level;
	FIntPoint StairContinuationCell = AimedCell;
	const bool bPlaceStairInCurrentCell = SelectedPiece == EArenaBuildPiece::Stair && bAimedAtCurrentCell && bIsFalling;
	if (bPlaceStairInCurrentCell)
	{
		StairContinuationCell = Here;
	}
	else if (SelectedPiece == EArenaBuildPiece::Stair && bAimedAtCurrentCell)
	{
		int32 AheadEdge = 1;
		FIntVector AheadWallCell(Here.X, Here.Y, Level);
		switch (Heading)
		{
		case 0: AheadEdge = 2; AheadWallCell.X += 1; break;
		case 1: AheadEdge = 1; AheadWallCell.Y += 1; break;
		case 2: AheadEdge = 2; break;
		default: AheadEdge = 1; break;
		}
		if (IsSlotTaken(EArenaBuildPiece::Wall, AheadWallCell, AheadEdge))
		{
			StairContinuationCell = Here;
		}
	}
	int32 StairPlacementHeading = Heading;
	if (SelectedPiece == EArenaBuildPiece::Stair && !bPlaceStairInCurrentCell)
	{
		constexpr float StairEdgeTolerance = 100.0f;
		constexpr float StairTurnThreshold = 12.0f;
		float BestStairDistanceSquared = TNumericLimits<float>::Max();
		for (TActorIterator<AArenaBuildPiece> It(GetWorld()); It; ++It)
		{
			const AArenaBuildPiece* Stair = *It;
			const FIntVector& StairCell = Stair->GetCell();
			const float HeightAboveStairBase = Feet - StairCell.Z * ArenaBuild::Story;
			const int32 ClimbHeading = (Stair->GetRotation() + 3) & 3;
			const float YawFromClimb = FRotator::NormalizeAxis(View.Yaw - ClimbHeading * 90.0f);
			constexpr float StairHeightTolerance = 20.0f;
			if (Stair->GetPiece() != EArenaBuildPiece::Stair
				|| HeightAboveStairBase < -StairHeightTolerance
				|| HeightAboveStairBase > ArenaBuild::Story + StairHeightTolerance)
			{
				continue;
			}

			const float MinX = StairCell.X * ArenaBuild::CellSize - StairEdgeTolerance;
			const float MinY = StairCell.Y * ArenaBuild::CellSize - StairEdgeTolerance;
			const float MaxX = (StairCell.X + 1) * ArenaBuild::CellSize + StairEdgeTolerance;
			const float MaxY = (StairCell.Y + 1) * ArenaBuild::CellSize + StairEdgeTolerance;
			if (Location.X < MinX || Location.X > MaxX || Location.Y < MinY || Location.Y > MaxY)
			{
				continue;
			}

			const FVector2D StairCenter(
				(StairCell.X + 0.5f) * ArenaBuild::CellSize,
				(StairCell.Y + 0.5f) * ArenaBuild::CellSize);
			const float DistanceSquared = FVector2D::DistSquared(FVector2D(Location.X, Location.Y), StairCenter);
			if (DistanceSquared < BestStairDistanceSquared)
			{
				BestStairDistanceSquared = DistanceSquared;
				StairPlacementHeading = ClimbHeading;
				int32 SideHeading = ClimbHeading;
				if (YawFromClimb > StairTurnThreshold && YawFromClimb < 135.0f)
				{
					SideHeading = (ClimbHeading + 1) & 3;
				}
				else if (YawFromClimb < -StairTurnThreshold && YawFromClimb > -135.0f)
				{
					SideHeading = (ClimbHeading + 3) & 3;
				}
				const bool bPlacingBesideStair = SideHeading != ClimbHeading;
				StairPlacementLevel = StairCell.Z + (bPlacingBesideStair ? 0 : 1);
				StairContinuationCell = FIntPoint(StairCell.X, StairCell.Y) + Directions[SideHeading];
			}
		}
	}

	Target.Shape = ArenaBuild::DefaultShape(SelectedPiece);
	switch (SelectedPiece)
	{
	case EArenaBuildPiece::Wall:
		// the wall goes on the edge of your cell that you are facing
		switch (Heading)
		{
		case 0: Target.Edge = 2; Target.Cell = FIntVector(Here.X + 1, Here.Y, Level); break;
		case 1: Target.Edge = 1; Target.Cell = FIntVector(Here.X, Here.Y + 1, Level); break;
		case 2: Target.Edge = 2; Target.Cell = FIntVector(Here.X, Here.Y, Level); break;
		default: Target.Edge = 1; Target.Cell = FIntVector(Here.X, Here.Y, Level); break;
		}
		break;
	case EArenaBuildPiece::Floor:
	{
		const FIntPoint FloorCell = bAimedAtCurrentCell && bIsFalling ? Here : AimedCell;
		Target.Cell = bLookingUp ? FIntVector(Here.X, Here.Y, Level + 1) : FIntVector(FloorCell.X, FloorCell.Y, Level);
		Target.Rotation = UserRotation;
		break;
	}
	case EArenaBuildPiece::Stair:
		Target.Cell = FIntVector(StairContinuationCell.X, StairContinuationCell.Y, StairPlacementLevel);
		Target.Rotation = ((StairPlacementHeading + 1 + UserRotation) & 3);       // keep the same climb direction when adding a side-by-side stair
		break;
	default:
		Target.Cell = bLookingUp ? FIntVector(Here.X, Here.Y, Level + 1) : FIntVector(AimedCell.X, AimedCell.Y, Level);
		Target.Rotation = UserRotation;
		break;
	}
	Target.bValid = Target.Shape != INDEX_NONE;
	return Target;
}

bool UArenaBuildComponent::IsSlotTaken(EArenaBuildPiece Piece, const FIntVector& Cell, int32 Edge) const
{
	const FString Key = ArenaBuild::MakeKey(Piece, Cell, Edge);
	if (const UWorld* World = GetWorld())
	{
		if (const UArenaBuildSubsystem* Subsystem = World->GetSubsystem<UArenaBuildSubsystem>())
		{
			if (Subsystem->Find(Key))
			{
				return true;
			}
		}
		for (TActorIterator<AArenaBuildPiece> It(World); It; ++It)
		{
			if (It->GetKey() == Key)
			{
				return true;
			}
		}
	}
	return false;
}

void UArenaBuildComponent::UpdateGhost()
{
	UWorld* World = GetWorld();
	const FTarget Target = ComputeTarget();
	UStaticMesh* Mesh = Target.bValid ? ArenaBuild::LoadMesh(SelectedMaterial, Target.Shape) : nullptr;
	if (!World || !Mesh)
	{
		HideGhost();
		return;
	}
	if (!Ghost)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Ghost = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
		if (!Ghost)
		{
			return;
		}
		Ghost->SetActorEnableCollision(false);
		Ghost->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Ghost->GetStaticMeshComponent()->SetCastShadow(false);
		Ghost->SetReplicates(false);
	}
	UStaticMeshComponent* Component = Ghost->GetStaticMeshComponent();
	Component->SetStaticMesh(Mesh);
	Ghost->SetActorHiddenInGame(false);
	Ghost->SetActorTransform(ArenaBuild::PieceTransform(SelectedPiece, Target.Cell, Target.Edge, Target.Rotation));

	const bool bTaken = IsSlotTaken(SelectedPiece, Target.Cell, Target.Edge);
	if (!GhostMaterial)
	{
		if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_BuildGhost.M_BuildGhost")))
		{
			GhostMaterial = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	if (GhostMaterial)
	{
		GhostMaterial->SetVectorParameterValue(TEXT("Color"), bTaken ? FLinearColor(1.0f, 0.12f, 0.1f) : UArenaSettingsWidget::GetBuildPreviewColor());
		for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
		{
			Component->SetMaterial(Slot, GhostMaterial);
		}
	}
}

void UArenaBuildComponent::HideGhost()
{
	if (Ghost)
	{
		Ghost->SetActorHiddenInGame(true);
	}
}

void UArenaBuildComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APlayerController* PC = GetController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (bBuildMode && !IsBuildingEnabled())
	{
		SetBuildMode(false);
	}
	if (IsBuildingEnabled() && bInputReady && !bExternalCombatBlock)
	{
		using ArenaControls::EAction;
		if (ArenaControls::WasPressed(PC, EAction::Wall)) { SelectWall(); }
		if (ArenaControls::WasPressed(PC, EAction::Floor)) { SelectFloor(); }
		if (ArenaControls::WasPressed(PC, EAction::Stair)) { SelectStair(); }
		if (ArenaControls::WasPressed(PC, EAction::Roof)) { SelectRoof(); }
		if (ArenaControls::WasPressed(PC, EAction::Rotate)) { RotatePiece(); }
		if (ArenaControls::WasPressed(PC, EAction::Edit)) { ToggleEdit(); }
		if (PC->WasInputKeyJustPressed(EKeys::F5)) { SelectWood(); }
		if (PC->WasInputKeyJustPressed(EKeys::F6)) { SelectBrick(); }
		if (PC->WasInputKeyJustPressed(EKeys::F7)) { SelectMetal(); }
	}

	// building: the mouse places pieces, so the pickaxe and the guns stay quiet
	if (const APawn* Body = PC->GetPawn())
	{
		if (UFortnitePortingCharacterComponent* Cosmetics = Body->FindComponentByClass<UFortnitePortingCharacterComponent>())
		{
			Cosmetics->bCombatBlocked = bBuildMode || bExternalCombatBlock;
		}
	}
	if (!bHudAdded && IsBuildingEnabled() && PC->GetPawn())
	{
		EnsureHud();
		RebuildHud();
	}
	if (bHudAdded)
	{
		UpdatePieceHealth(PC);
		UpdateEditPrompt(PC);
	}
	if (bEditing)
	{
		const APawn* Pawn = PC->GetPawn();
		const AArenaBuildPiece* Piece = EditPiece.Get();
		if (!Pawn || !Piece
			|| FVector::DistSquared(Pawn->GetActorLocation(), Piece->GetActorLocation()) > FMath::Square(ArenaBuild::CellSize))
		{
			CancelEdit();
		}
	}
	if (bBuildMode && !bEditing)
	{
		UpdateGhost();
	}
	else
	{
		HideGhost();
	}
	if (bEditing)
	{
		UpdateEditTiles();
	}
	if (bHoldPlacing && bBuildMode && !bEditing && !bExternalCombatBlock)
	{
		HoldTimer += DeltaTime;
		if (HoldTimer >= 0.07f)
		{
			HoldTimer = 0.0f;
			TryPlace();
		}
	}
}

void UArenaBuildComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyEditTiles();
	RemoveHud();
	if (Ghost)
	{
		Ghost->Destroy();
		Ghost = nullptr;
	}
	if (bBuildMode)
	{
		EnableBuildContext(false);
	}
	Super::EndPlay(EndPlayReason);
}

// ------------------------------------------------------------------------------------------------ placing and editing

void UArenaBuildComponent::OnPlace()
{
	if (bExternalCombatBlock)
	{
		return;
	}
	if (bEditing)
	{
		// click a blue tile to cut it out (or bring it back); keep the button down and sweep over more tiles to paint them the same way
		bPainting = true;
		bPaintedThisPress = false;
		LastPaintedBit = INDEX_NONE;
		if (HoveredBit != INDEX_NONE)
		{
			bPaintValue = (EditMask & (1 << HoveredBit)) == 0;
			PaintTile(HoveredBit, bPaintValue);
			LastPaintedBit = HoveredBit;
		}
		return;
	}
	if (!bBuildMode)
	{
		return;
	}
	bHoldPlacing = true;
	HoldTimer = 0.0f;
	TryPlace();
}

void UArenaBuildComponent::TryPlace()
{
	if (!bBuildMode || bEditing)
	{
		return;
	}
	const FTarget Target = ComputeTarget();
	if (!Target.bValid || IsSlotTaken(SelectedPiece, Target.Cell, Target.Edge))
	{
		return;
	}
	ServerPlace(SelectedPiece, SelectedMaterial, Target.Cell, Target.Edge, Target.Rotation, false, Target.Shape, UArenaSettingsWidget::GetBuildTextureIndex());
}

AArenaBuildPiece* UArenaBuildComponent::PieceUnderCrosshair() const
{
	APlayerController* PC = GetController();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return nullptr;
	}
	FVector Origin;
	FRotator Direction;
	PC->GetPlayerViewPoint(Origin, Direction);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBuildEdit), false, PC->GetPawn());
	AArenaBuildPiece* HitPiece = nullptr;
	if (World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction.Vector() * 2500.0f, ECC_Visibility, Params))
	{
		HitPiece = Cast<AArenaBuildPiece>(Hit.GetActor());
	}

	const APawn* Pawn = PC->GetPawn();
	if (HitPiece)
	{
		if (HitPiece->GetPiece() != EArenaBuildPiece::Roof || !Pawn)
		{
			return HitPiece;
		}

		// Prefer the roof nearest to the player along the view ray; stacked pieces need not share a cell.
		const FVector PawnLocation = Pawn->GetActorLocation();
		const FVector RayDirection = Direction.Vector().GetSafeNormal();
		constexpr float MaxTraceDistance = 2500.0f;
		AArenaBuildPiece* NearestRoof = nullptr;
		float BestVerticalDistance = TNumericLimits<float>::Max();
		float BestRayDistance = TNumericLimits<float>::Max();
		for (TActorIterator<AArenaBuildPiece> It(World); It; ++It)
		{
			AArenaBuildPiece* Candidate = *It;
			if (Candidate->GetPiece() != EArenaBuildPiece::Roof)
			{
				continue;
			}

			const FBox Bounds = Candidate->GetComponentsBoundingBox();
			float RayEntry = 0.0f;
			float RayExit = MaxTraceDistance;
			bool bIntersectsRay = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float RayOrigin = Origin[Axis];
				const float RayDelta = RayDirection[Axis];
				const float MinBound = Bounds.Min[Axis] - 8.0f;
				const float MaxBound = Bounds.Max[Axis] + 8.0f;
				if (FMath::IsNearlyZero(RayDelta))
				{
					if (RayOrigin < MinBound || RayOrigin > MaxBound)
					{
						bIntersectsRay = false;
						break;
					}
					continue;
				}

				float AxisEntry = (MinBound - RayOrigin) / RayDelta;
				float AxisExit = (MaxBound - RayOrigin) / RayDelta;
				if (AxisEntry > AxisExit)
				{
					Swap(AxisEntry, AxisExit);
				}
				RayEntry = FMath::Max(RayEntry, AxisEntry);
				RayExit = FMath::Min(RayExit, AxisExit);
				if (RayEntry > RayExit)
				{
					bIntersectsRay = false;
					break;
				}
			}
			if (!bIntersectsRay || RayExit < 0.0f || RayEntry > MaxTraceDistance)
			{
				continue;
			}

			const float VerticalDistance = FMath::Abs(Bounds.GetCenter().Z - PawnLocation.Z);
			if (VerticalDistance < BestVerticalDistance
				|| (FMath::IsNearlyEqual(VerticalDistance, BestVerticalDistance) && RayEntry < BestRayDistance))
			{
				BestVerticalDistance = VerticalDistance;
				BestRayDistance = RayEntry;
				NearestRoof = Candidate;
			}
		}
		return NearestRoof ? NearestRoof : HitPiece;
	}

	// When standing on a stair, the crosshair can hit surrounding world geometry instead of the stair mesh.
	if (!Pawn)
	{
		return nullptr;
	}
	const FVector PawnLocation = Pawn->GetActorLocation();
	const float PawnFeet = PawnLocation.Z - Pawn->GetSimpleCollisionHalfHeight();
	AArenaBuildPiece* StairBelow = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<AArenaBuildPiece> It(World); It; ++It)
	{
		AArenaBuildPiece* Candidate = *It;
		if (Candidate->GetPiece() != EArenaBuildPiece::Stair)
		{
			continue;
		}

		FVector BoundsOrigin;
		FVector BoundsExtent;
		Candidate->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		if (FMath::Abs(PawnLocation.X - BoundsOrigin.X) > BoundsExtent.X + 48.0f
			|| FMath::Abs(PawnLocation.Y - BoundsOrigin.Y) > BoundsExtent.Y + 48.0f
			|| PawnFeet < BoundsOrigin.Z - BoundsExtent.Z - 48.0f
			|| PawnFeet > BoundsOrigin.Z + BoundsExtent.Z + 48.0f)
		{
			continue;
		}

		const float DistanceSquared = FVector2D::DistSquared(
			FVector2D(PawnLocation.X, PawnLocation.Y),
			FVector2D(BoundsOrigin.X, BoundsOrigin.Y));
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			StairBelow = Candidate;
		}
	}
	return StairBelow;
}

void UArenaBuildComponent::OnPlaceReleased()
{
	bHoldPlacing = false;
	const bool bEditNow = bEditing && bPainting && bPaintedThisPress;
	bPainting = false;
	bPaintedThisPress = false;
	LastPaintedBit = INDEX_NONE;
	if (bEditNow)
	{
		ConfirmEdit();      // letting go of the mouse makes the edit (a shape the game does not have stays open, in red)
	}
}

void UArenaBuildComponent::ToggleEdit()
{
	if (!IsBuildingEnabled())
	{
		return;
	}
	if (bEditing)
	{
		CancelEdit();
	}
	else
	{
		BeginEdit();
	}
}

void UArenaBuildComponent::BeginEdit()
{
	APlayerController* PC = GetController();
	AArenaBuildPiece* Piece = PieceUnderCrosshair();
	if (!PC || !Piece)
	{
		return;
	}
	if (!bBuildMode)
	{
		SetBuildMode(true, true);
		bBorrowedBuildMode = true;
	}
	EditPiece = Piece;
	EditMask = Piece->GetCurrentMask();
	bEditSpiral = Piece->IsSpiral();
	bEditing = true;
	HideGhost();
	ArenaBuildFeedback::Play2D(this, TEXT("Fort_Build_BluePrint_G_To_Edit_Cue"));
	ArenaBuildFeedback::PlayAnimation(PC->GetPawn(), ArenaBuildFeedback::EAnim::Equip);      // the arms take the blueprint pose while the grid is open
	BuildEditTiles();
	EnsureHud();
	RebuildHud();
}

void UArenaBuildComponent::CancelEdit()
{
	EndEdit(true);
}

void UArenaBuildComponent::EndEdit(bool bPlaySound)
{
	if (!bEditing)
	{
		return;
	}
	if (bPlaySound)
	{
		ArenaBuildFeedback::Play2D(this, TEXT("Fort_Build_BluePrint_CancelEdit_Cue"));
	}
	bEditing = false;
	bPainting = false;
	DestroyEditTiles();
	EditPiece.Reset();
	if (bBorrowedBuildMode)
	{
		SetBuildMode(false);      // the player never asked for build mode: the piece selector does not open
	}
	RebuildHud();
}

void UArenaBuildComponent::ConfirmEdit()
{
	AArenaBuildPiece* Piece = EditPiece.Get();
	if (Piece)
	{
		const ArenaBuild::FResolved Resolved = ArenaBuild::Resolve(Piece->GetPiece(), EditMask, bEditSpiral);
		const bool bChanged = EditMask != Piece->GetCurrentMask() || bEditSpiral != Piece->IsSpiral();
		if (!Resolved.IsValid())
		{
			return;      // red tiles: stay in the grid until the shape is one the game knows
		}
		if (bChanged)
		{
			ServerEdit(Piece, EditMask, bEditSpiral);
		}
	}
	EndEdit(false);
}

void UArenaBuildComponent::ServerPlace_Implementation(EArenaBuildPiece Piece, EArenaBuildMaterial Material, FIntVector Cell, int32 Edge, int32 Rotation, bool bMirror, int32 ShapeIndex, int32 BuildTextureIndex)
{
	APlayerController* PC = GetController();
	UWorld* World = GetWorld();
	UArenaBuildSubsystem* Subsystem = World ? World->GetSubsystem<UArenaBuildSubsystem>() : nullptr;
	if (!PC || !PC->GetPawn() || !Subsystem || !IsBuildingEnabled())
	{
		return;
	}
	if (BuildTextureIndex < 1 || BuildTextureIndex > 4)
	{
		return;
	}
	const TArray<ArenaBuild::FShape>& Shapes = ArenaBuild::Shapes();
	if (!Shapes.IsValidIndex(ShapeIndex) || Shapes[ShapeIndex].Piece != Piece)
	{
		return;
	}
	if (Piece == EArenaBuildPiece::Wall ? (Edge != 1 && Edge != 2) : Edge != 0)
	{
		return;
	}
	if (Cell.Z < -2 || Cell.Z > 40)
	{
		return;
	}
	const FTransform Transform = ArenaBuild::PieceTransform(Piece, Cell, Edge, Rotation);
	if (FVector::Dist(Transform.GetLocation(), PC->GetPawn()->GetActorLocation()) > 2800.0f)
	{
		return;
	}
	if (Subsystem->Find(ArenaBuild::MakeKey(Piece, Cell, Edge)))
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = PC;
	if (AArenaBuildPiece* Built = World->SpawnActor<AArenaBuildPiece>(AArenaBuildPiece::StaticClass(), Transform, Params))
	{
		Built->Init(Piece, Material, ShapeIndex, bMirror, Rotation & 3, Cell, Edge, PC->GetPawn(), BuildTextureIndex);
	}
}

void UArenaBuildComponent::ServerEdit_Implementation(AArenaBuildPiece* PieceActor, int32 Mask, bool bSpiral)
{
	APlayerController* PC = GetController();
	if (!PC || !PC->GetPawn() || !PieceActor || !IsBuildingEnabled())
	{
		return;
	}
	if (FVector::Dist(PieceActor->GetActorLocation(), PC->GetPawn()->GetActorLocation()) > 3300.0f)
	{
		return;
	}
	const EArenaBuildPiece Piece = PieceActor->GetPiece();
	const uint16 SafeMask = static_cast<uint16>(Mask) & ArenaBuild::FullMask(Piece);
	ArenaBuild::FResolved Resolved = ArenaBuild::Resolve(Piece, SafeMask, bSpiral);
	if (!Resolved.IsValid())
	{
		return;
	}
	if (Piece == EArenaBuildPiece::Stair && SafeMask == ArenaBuild::FullMask(Piece))
	{
		// a full stair has no turn of its own: it keeps the way it climbs
		Resolved.Rotation = PieceActor->GetRotation();
		Resolved.bMirror = false;
	}
	PieceActor->SetShape(Resolved.ShapeIndex, Resolved.bMirror, Resolved.Rotation);
	PieceActor->MulticastEdited(PC->GetPawn());
	if (Piece != EArenaBuildPiece::Wall)
	{
		PieceActor->SetActorTransform(ArenaBuild::PieceTransform(Piece, PieceActor->GetCell(), PieceActor->GetEdge(), Resolved.Rotation));
	}
}

// ------------------------------------------------------------------------------------------------ HUD

void UArenaBuildComponent::EnsureHud()
{
	if (bHudAdded || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	HudRoot = SNew(SBox).Visibility(EVisibility::SelfHitTestInvisible);
	GEngine->GameViewport->AddViewportWidgetContent(HudRoot.ToSharedRef(), 45);
	bHudAdded = true;
}

void UArenaBuildComponent::RemoveHud()
{
	if (bHudAdded && GEngine && GEngine->GameViewport && HudRoot.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudRoot.ToSharedRef());
	}
	HudRoot.Reset();
	bHudAdded = false;
}

void UArenaBuildComponent::RebuildHud()
{
	if (!HudRoot.IsValid())
	{
		return;
	}
	TSharedRef<SOverlay> Layout = SNew(SOverlay);
	if (bBuildMode && !bBorrowedBuildMode)
	{
		Layout->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 36)
		[
			MakeBar()
		];
	}
	// the material in use, centred on the right edge
	Layout->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0, 0, 22, 0)
	[
		MakeMaterialPanel()
	];
	// the health card of the piece being broken follows the piece on screen
	Layout->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(PieceCardPosition.X, PieceCardPosition.Y, 0.0f, 0.0f); }))
	[
		MakePieceHealth()
	];
	// the edit key over the piece under the crosshair
	Layout->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(EditPromptPosition.X, EditPromptPosition.Y, 0.0f, 0.0f); }))
	[
		MakeEditPrompt()
	];
	HudRoot->SetContent(Layout);
}

TSharedRef<SWidget> UArenaBuildComponent::MakeEditPrompt()
{
	return SNew(SBox).Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return bEditPromptVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
	[
		SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(10, 7))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(KeyCapBrush.Get()).Padding(FMargin(9, 3))
				[
					SNew(STextBlock).Text(TAttribute<FText>::CreateLambda([]() { return ArenaControls::KeyName(ArenaControls::Get(ArenaControls::EAction::Edit)); })).Font(BoldFont(14)).ColorAndOpacity(FLinearColor::White).MinDesiredWidth(14.0f).Justification(ETextJustify::Center)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(9, 0, 2, 0)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Editar"))).Font(BoldFont(14)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.92f))
			]
		]
	];
}

void UArenaBuildComponent::UpdateEditPrompt(APlayerController* PC)
{
	bEditPromptVisible = false;
	if (bEditing || !PC || !PC->GetPawn() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	const AArenaBuildPiece* Piece = PieceUnderCrosshair();
	if (!Piece || FVector::DistSquared(PC->GetPawn()->GetActorLocation(), Piece->GetActorLocation()) > FMath::Square(600.0f))
	{
		return;
	}
	FVector2D Screen;
	if (!PC->ProjectWorldLocationToScreen(Piece->GetComponentsBoundingBox().GetCenter(), Screen, false))
	{
		return;
	}
	FIntPoint ViewSize;
	PC->GetViewportSize(ViewSize.X, ViewSize.Y);
	const float Scale = FMath::Max(GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(ViewSize), 0.1f);
	EditPromptPosition = FVector2D(Screen.X / Scale - 60.0f, Screen.Y / Scale - 18.0f);
	bEditPromptVisible = true;
}

TSharedRef<SWidget> UArenaBuildComponent::MakeMaterialPanel()
{
	const TCHAR* IconNames[3] = { TEXT("T_Mat_Wood"), TEXT("T_Mat_Stone"), TEXT("T_Mat_Metal") };
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	const EArenaBuildMaterial Materials[3] = { EArenaBuildMaterial::Wood, EArenaBuildMaterial::Brick, EArenaBuildMaterial::Metal };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!MaterialIcons[Index])
		{
			MaterialIcons[Index] = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/Arena/UI/%s.%s"), IconNames[Index], IconNames[Index]));
		}
		if (!MaterialIconBrush[Index].IsValid())
		{
			MaterialIconBrush[Index] = MakeShared<FSlateBrush>();
			MaterialIconBrush[Index]->ImageSize = FVector2D(64.0f, 64.0f);
		}
		MaterialIconBrush[Index]->SetResourceObject(MaterialIcons[Index]);

		const bool bSelected = SelectedMaterial == Materials[Index];
		const float Size = bSelected ? 72.0f : 58.0f;
		Column->AddSlot().AutoHeight().Padding(0, 5).HAlign(HAlign_Center)
		[
			SNew(SBorder).BorderImage(bSelected ? MaterialSelectedBrush.Get() : MaterialSlotBrush.Get()).Padding(FMargin(7, 6, 7, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(Size).HeightOverride(Size)
					[
						SNew(SImage).Image(MaterialIconBrush[Index].Get()).ColorAndOpacity(bSelected ? FLinearColor::White : FLinearColor(1, 1, 1, 0.6f))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::AsNumber(FMath::RoundToInt(ArenaBuild::MaxHealth(Materials[Index])))).Font(BoldFont(bSelected ? 14 : 12)).ColorAndOpacity(bSelected ? FLinearColor::White : FLinearColor(1, 1, 1, 0.55f))
				]
			]
		];
	}
	return SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(8, 6)).Visibility(EVisibility::HitTestInvisible)[Column];
}

TSharedRef<SWidget> UArenaBuildComponent::MakePieceHealth()
{
	const float BarWidth = 150.0f;
	return SNew(SBox).Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return bPieceCardVisible ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }))
	[
		SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(12, 8))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(TAttribute<FText>::CreateLambda([this]() { return PieceCardText; })).Font(BoldFont(13))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
			[
				SNew(SBorder).BorderImage(SlotBrush.Get()).Padding(0)
				[
					SNew(SBox).WidthOverride(BarWidth).HeightOverride(10.0f).HAlign(HAlign_Left)
					[
						SNew(SBox).WidthOverride(TAttribute<FOptionalSize>::CreateLambda([this, BarWidth]() { return FOptionalSize(FMath::Max(PieceCardFraction, 0.02f) * BarWidth); }))
						[
							SNew(SBorder).BorderImage(BarFillBrush.Get()).BorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(PieceCardColor); }))
						]
					]
				]
			]
		]
	];
}

void UArenaBuildComponent::UpdatePieceHealth(APlayerController* PC)
{
	bPieceCardVisible = false;
	UWorld* World = GetWorld();
	if (!World || !PC || !PC->GetPawn() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const FVector Eye = PC->GetPawn()->GetActorLocation();
	AArenaBuildPiece* Best = nullptr;
	float BestAge = 3.0f;
	for (TActorIterator<AArenaBuildPiece> It(World); It; ++It)
	{
		const float Age = Now - It->GetLastHitTime();
		if (Age < BestAge && FVector::DistSquared(It->GetActorLocation(), Eye) < FMath::Square(2500.0f))
		{
			BestAge = Age;
			Best = *It;
		}
	}
	if (!Best)
	{
		return;
	}
	FVector2D Screen;
	const FVector Center = Best->GetComponentsBoundingBox().GetCenter();
	if (!PC->ProjectWorldLocationToScreen(Center, Screen, false))
	{
		return;
	}
	FIntPoint ViewSize;
	PC->GetViewportSize(ViewSize.X, ViewSize.Y);
	const float Scale = FMath::Max(GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(ViewSize), 0.1f);
	PieceCardPosition = FVector2D(Screen.X / Scale - 82.0f, Screen.Y / Scale - 30.0f);
	const float Max = Best->GetMaxHealth();
	PieceCardFraction = FMath::Clamp(Best->GetHealth() / FMath::Max(Max, 1.0f), 0.0f, 1.0f);
	PieceCardText = FText::FromString(FString::Printf(TEXT("%s  %d / %d"), ArenaBuild::MaterialName(Best->GetMaterial()), FMath::CeilToInt(FMath::Max(Best->GetHealth(), 0.0f)), FMath::RoundToInt(Max)));
	PieceCardColor = FMath::Lerp(FLinearColor(0.95f, 0.22f, 0.15f), FLinearColor(0.35f, 0.9f, 0.4f), PieceCardFraction);
	bPieceCardVisible = true;
}

TSharedRef<SWidget> UArenaBuildComponent::MakeBar()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const EArenaBuildPiece Pieces[4] = { EArenaBuildPiece::Wall, EArenaBuildPiece::Floor, EArenaBuildPiece::Stair, EArenaBuildPiece::Roof };
	const TCHAR* PieceKeys[4] = { TEXT("Z"), TEXT("X"), TEXT("C"), TEXT("V") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bSelected = SelectedPiece == Pieces[Index];
		Row->AddSlot().AutoWidth().Padding(4, 0)
		[
			SNew(SBorder).BorderImage(bSelected ? SlotSelectedBrush.Get() : SlotBrush.Get()).Padding(FMargin(18, 9))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(ArenaBuild::PieceName(Pieces[Index]))).Font(BoldFont(15))]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(PieceKeys[Index])).Font(BoldFont(11)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.55f))]
			]
		];
	}
	Row->AddSlot().AutoWidth().Padding(10, 0)[SNew(SBox).WidthOverride(2).HeightOverride(34)[SNew(SBorder).BorderImage(SlotBrush.Get())]];
	const EArenaBuildMaterial Materials[3] = { EArenaBuildMaterial::Wood, EArenaBuildMaterial::Brick, EArenaBuildMaterial::Metal };
	const TCHAR* MaterialKeys[3] = { TEXT("F5"), TEXT("F6"), TEXT("F7") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const bool bSelected = SelectedMaterial == Materials[Index];
		Row->AddSlot().AutoWidth().Padding(4, 0)
		[
			SNew(SBorder).BorderImage(bSelected ? SlotSelectedBrush.Get() : SlotBrush.Get()).Padding(FMargin(18, 9))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(ArenaBuild::MaterialName(Materials[Index]))).Font(BoldFont(15))]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(STextBlock).Text(FText::FromString(MaterialKeys[Index])).Font(BoldFont(11)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.55f))]
			]
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Text(FText::FromString(bEditing ? TEXT("Clic o arrastra sobre los bloques azules: al soltar se edita   Clic derecho: restablecer   G: salir   R: rampa en U (escaleras)") : TEXT("Clic (mantén): construir   Clic derecho: material   R: girar   G: editar   Q: salir"))).Font(BoldFont(12)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.7f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBorder).BorderImage(PanelBrush.Get()).Padding(FMargin(12, 10)).Visibility(EVisibility::HitTestInvisible)[Row]
		];
}

void UArenaBuildComponent::PaintTile(int32 Bit, bool bOn)
{
	const uint16 Before = EditMask;
	if (bOn)
	{
		EditMask |= static_cast<uint16>(1 << Bit);
	}
	else
	{
		EditMask &= static_cast<uint16>(~(1 << Bit));
	}
	if (EditMask != Before)
	{
		bPaintedThisPress = true;
		ArenaBuildFeedback::Play2D(this, TEXT("Fort_Build_BluePrint_Edit_TurnOnTile_Cue"), 0.8f);
	}
}

void UArenaBuildComponent::DestroyEditTiles()
{
	if (EditActor)
	{
		EditActor->Destroy();
		EditActor = nullptr;
	}
	EditTileComps.Reset();
	EditTileVisualComps.Reset();
	EditTileMats.Reset();
	EditTileBits.Reset();
	HoveredBit = INDEX_NONE;
}

void UArenaBuildComponent::BuildEditTiles()
{
	DestroyEditTiles();
	APlayerController* PC = GetController();
	AArenaBuildPiece* Piece = EditPiece.Get();
	UWorld* World = GetWorld();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!PC || !Piece || !World || !Cube || !Sphere)
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Arena/Materials/M_BuildGhost.M_BuildGhost"));

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	EditActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!EditActor)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(EditActor, TEXT("EditRoot"));
	EditActor->SetRootComponent(Root);
	Root->RegisterComponent();

	const EArenaBuildPiece Type = Piece->GetPiece();
	const bool bWall = Type == EArenaBuildPiece::Wall;
	const int32 Size = bWall ? 3 : 2;
	const FTransform PieceTransform = Piece->GetActorTransform();
	const FIntVector Cell = Piece->GetCell();
	const FVector Centre(Cell.X * ArenaBuild::CellSize + ArenaBuild::CellSize * 0.5f, Cell.Y * ArenaBuild::CellSize + ArenaBuild::CellSize * 0.5f, Cell.Z * ArenaBuild::Story);
	FVector ViewOrigin;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewOrigin, ViewRotation);
	const float WallFaceOffset = PieceTransform.InverseTransformPosition(ViewOrigin).Y >= 0.0f ? 35.0f : -35.0f;
	// where the tiles hover: on the face of a wall, on top of a floor, along the stair and on the roof face nearest the player
	float RaiseZ = 34.0f;
	if (Type == EArenaBuildPiece::Roof)
	{
		const FBox RoofBounds = Piece->GetComponentsBoundingBox();
		const bool bPlayerBelowRoof = PC->GetPawn()->GetActorLocation().Z < RoofBounds.GetCenter().Z;
		RaiseZ = (bPlayerBelowRoof ? RoofBounds.Min.Z : RoofBounds.Max.Z) - Centre.Z + (bPlayerBelowRoof ? -8.0f : 8.0f);
	}
	const bool bStair = Type == EArenaBuildPiece::Stair;
	FVector StairClimbDirection = FVector::ZeroVector;
	FQuat StairTileRotation = FQuat::Identity;
	float StairTileLength = ArenaBuild::CellSize * 0.5f;
	if (bStair)
	{
		const int32 ClimbHeading = (Piece->GetRotation() + 3) & 3;
		StairClimbDirection = FVector(Directions[ClimbHeading].X, Directions[ClimbHeading].Y, 0.0f);
		const FVector WidthDirection(StairClimbDirection.Y, -StairClimbDirection.X, 0.0f);
		const FVector RampDirection = (StairClimbDirection + FVector(0.0f, 0.0f, ArenaBuild::Story / ArenaBuild::CellSize)).GetSafeNormal();
		StairTileRotation = FRotationMatrix::MakeFromXY(WidthDirection, RampDirection).ToQuat();
		StairTileLength = FMath::Sqrt(FMath::Square(ArenaBuild::CellSize * 0.5f) + FMath::Square(ArenaBuild::Story * 0.5f));
	}

	for (int32 Row = 0; Row < Size; ++Row)
	{
		for (int32 Col = 0; Col < Size; ++Col)
		{
			FVector Location;
			FQuat Rotation = FQuat::Identity;
			FVector Scale;
			if (bWall)
			{
				const float TileW = ArenaBuild::CellSize / 3.0f;
				const float TileH = ArenaBuild::Story / 3.0f;
				// the first row is the top one; the columns grow along the wall's own X
				Location = PieceTransform.TransformPosition(FVector((Col - 1) * TileW, WallFaceOffset, ArenaBuild::Story - (Row + 0.5f) * TileH));
				Rotation = PieceTransform.GetRotation();
				Scale = FVector((TileW - 12.0f) / 100.0f, 4.0f / 100.0f, (TileH - 12.0f) / 100.0f);
			}
			else
			{
				// the tile mask is in the world frame: columns grow toward +X, rows toward +Y
				const float Half = ArenaBuild::CellSize * 0.25f;
				Location = Centre + FVector((Col * 2 - 1) * Half, (Row * 2 - 1) * Half, RaiseZ);
				if (bStair)
				{
					const FVector Offset = Location - Centre;
					Location.Z = Centre.Z + ArenaBuild::Story * 0.5f
						+ FVector::DotProduct(Offset, StairClimbDirection) * (ArenaBuild::Story / ArenaBuild::CellSize);
					Location += FVector::UpVector * 8.0f;
					Rotation = StairTileRotation;
					Scale = FVector(
						(ArenaBuild::CellSize * 0.5f - 14.0f) / 100.0f,
						(StairTileLength - 14.0f) / 100.0f,
						6.0f / 100.0f);
				}
				else
				{
					Scale = FVector((ArenaBuild::CellSize * 0.5f - 14.0f) / 100.0f, (ArenaBuild::CellSize * 0.5f - 14.0f) / 100.0f, 60.0f / 100.0f);
				}
			}
			UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(EditActor);
			Tile->SetStaticMesh(Cube);
			Tile->SetupAttachment(Root);
			Tile->SetMobility(EComponentMobility::Movable);
			Tile->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Tile->SetCollisionResponseToAllChannels(ECR_Ignore);
			Tile->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Tile->SetCastShadow(false);
			Tile->RegisterComponent();
			Tile->SetWorldTransform(FTransform(Rotation, Location, Scale));
			Tile->SetVisibility(false);
			UMaterialInstanceDynamic* Mat = Base ? UMaterialInstanceDynamic::Create(Base, EditActor) : nullptr;
			if (!Mat)
			{
				UE_LOG(LogArena, Error, TEXT("Could not create edit-grid material for %s."), *GetNameSafe(Piece));
				EditActor->Destroy();
				EditActor = nullptr;
				EditTileComps.Reset();
				EditTileVisualComps.Reset();
				EditTileMats.Reset();
				EditTileBits.Reset();
				return;
			}
			Tile->SetMaterial(0, Mat);

			const float TileWidth = FMath::Abs(Scale.X) * 100.0f;
			const float TileHeight = FMath::Abs(bWall ? Scale.Z : Scale.Y) * 100.0f;
			const float Thickness = bWall ? FMath::Abs(Scale.Y) * 100.0f : 4.0f;
			const float CornerRadius = FMath::Min(16.0f, FMath::Min(TileWidth, TileHeight) * 0.14f);
			auto AddVisual = [&](UStaticMesh* VisualMesh, const FVector& Dimensions, const FVector& LocalOffset)
			{
				UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(EditActor);
				Visual->SetStaticMesh(VisualMesh);
				Visual->SetupAttachment(Root);
				Visual->SetMobility(EComponentMobility::Movable);
				Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Visual->SetCanEverAffectNavigation(false);
				Visual->SetCastShadow(false);
				Visual->RegisterComponent();
				Visual->SetWorldTransform(FTransform(Rotation, Location + Rotation.RotateVector(LocalOffset), Dimensions / 100.0f));
				Visual->SetMaterial(0, Mat);
				EditTileVisualComps.Add(Visual);
			};

			const FVector CenterDimensions(
				FMath::Max(TileWidth - 2.0f * CornerRadius, 1.0f),
				bWall ? Thickness : FMath::Max(TileHeight - 2.0f * CornerRadius, 1.0f),
				bWall ? FMath::Max(TileHeight - 2.0f * CornerRadius, 1.0f) : Thickness);
			AddVisual(Cube, CenterDimensions, FVector::ZeroVector);

			const float LongSpan = FMath::Max(TileWidth - 2.0f * CornerRadius, 1.0f);
			const float ShortSpan = FMath::Max(TileHeight - 2.0f * CornerRadius, 1.0f);
			const FVector HorizontalEdgeDimensions(LongSpan, bWall ? Thickness : 2.0f * CornerRadius, bWall ? 2.0f * CornerRadius : Thickness);
			const FVector VerticalEdgeDimensions(2.0f * CornerRadius, bWall ? Thickness : ShortSpan, bWall ? ShortSpan : Thickness);
			for (const float Sign : { -1.0f, 1.0f })
			{
				const float HeightOffset = Sign * (TileHeight * 0.5f - CornerRadius);
				AddVisual(Cube, HorizontalEdgeDimensions, bWall ? FVector(0.0f, 0.0f, HeightOffset) : FVector(0.0f, HeightOffset, 0.0f));
				AddVisual(Cube, VerticalEdgeDimensions, FVector(Sign * (TileWidth * 0.5f - CornerRadius), 0.0f, 0.0f));

				for (const float WidthSign : { -1.0f, 1.0f })
				{
					const FVector CornerDimensions(2.0f * CornerRadius, bWall ? Thickness : 2.0f * CornerRadius, bWall ? 2.0f * CornerRadius : Thickness);
					const FVector CornerOffset(
						WidthSign * (TileWidth * 0.5f - CornerRadius),
						bWall ? 0.0f : HeightOffset,
						bWall ? HeightOffset : 0.0f);
					AddVisual(Sphere, CornerDimensions, CornerOffset);
				}
			}
			EditTileComps.Add(Tile);
			EditTileMats.Add(Mat);
			EditTileBits.Add(Row * Size + Col);
		}
	}
	UpdateEditTiles();
}

void UArenaBuildComponent::UpdateEditTiles()
{
	APlayerController* PC = GetController();
	UWorld* World = GetWorld();
	AArenaBuildPiece* Piece = EditPiece.Get();
	if (!PC || !World || !Piece || !EditActor)
	{
		return;
	}
	// the tile under the crosshair
	HoveredBit = INDEX_NONE;
	FVector Origin;
	FRotator Direction;
	PC->GetPlayerViewPoint(Origin, Direction);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArenaBuildTile), false, PC->GetPawn());
	if (World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction.Vector() * 2600.0f, ECC_Visibility, Params))
	{
		const int32 Index = EditTileComps.IndexOfByKey(Hit.GetComponent());
		if (Index != INDEX_NONE)
		{
			HoveredBit = EditTileBits[Index];
		}
	}
	if (bPainting && HoveredBit != INDEX_NONE && HoveredBit != LastPaintedBit)
	{
		PaintTile(HoveredBit, bPaintValue);
		LastPaintedBit = HoveredBit;
	}
	// blue = stays, grey = cut out, red = a shape the game does not have (nothing is applied until it is valid)
	const bool bValid = ArenaBuild::Resolve(Piece->GetPiece(), EditMask, bEditSpiral).IsValid();
	const FLinearColor EditGridColor = UArenaSettingsWidget::GetEditGridColor();
	for (int32 Index = 0; Index < EditTileMats.Num(); ++Index)
	{
		if (!EditTileMats[Index])
		{
			continue;
		}
		const bool bOn = (EditMask & (1 << EditTileBits[Index])) != 0;
		FLinearColor Color = bOn ? (bValid ? EditGridColor : FLinearColor(1.0f, 0.18f, 0.12f)) : FLinearColor(0.28f, 0.28f, 0.30f);
		if (EditTileBits[Index] == HoveredBit)
		{
			Color += FLinearColor(0.45f, 0.45f, 0.45f);
		}
		EditTileMats[Index]->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

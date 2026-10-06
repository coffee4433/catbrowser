#include "ArenaInventoryWidget.h"

#include "Arena.h"
#include "ArenaControls.h"
#include "ArenaPlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Input/DragAndDrop.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor InventoryAccent(0.20f, 0.64f, 1.0f);

	FSlateFontInfo InventoryFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
	}

	FButtonStyle MakeInventoryButtonStyle(const FLinearColor& Color)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(Color.R, Color.G, Color.B, 0.28f), 10.0f, FLinearColor(Color.R, Color.G, Color.B, 0.45f), 1.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(Color.R, Color.G, Color.B, 0.52f), 10.0f, FLinearColor::White, 1.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(Color.R, Color.G, Color.B, 0.7f), 10.0f, FLinearColor::White, 1.0f));
		Style.SetNormalPadding(FMargin(0));
		Style.SetPressedPadding(FMargin(0));
		return Style;
	}

	class FArenaInventoryWeaponDragDropOp : public FGameDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FArenaInventoryWeaponDragDropOp, FGameDragDropOperation)

		FString WeaponId;
		FString WeaponName;
		FString WeaponType;
		float CardWidth = 0.0f;
		FVector2D GrabOffsetPixels = FVector2D::ZeroVector;
		bool bWasSelected = false;
		bool bWasEquipped = false;
		FString LastHoveredWeaponId;
		FSlateBrush WeaponIconBrush;
		TSharedPtr<FSlateRoundedBoxBrush> NormalBrush;
		TSharedPtr<FSlateRoundedBoxBrush> HoverBrush;
		TSharedPtr<FSlateRoundedBoxBrush> SelectedBrush;
		FButtonStyle CardStyle;
		TWeakPtr<SWidget> SourceWidget;

		void Begin()
		{
			NormalBrush = MakeShared<FSlateRoundedBoxBrush>(
				FLinearColor(0.07f, 0.09f, 0.14f, 0.95f),
				11.0f,
				FLinearColor(0.55f, 0.64f, 0.78f, 0.18f),
				1.0f);
			HoverBrush = MakeShared<FSlateRoundedBoxBrush>(
				FLinearColor(0.1f, 0.14f, 0.22f, 1.0f),
				11.0f,
				FLinearColor(0.45f, 0.63f, 0.85f, 0.5f),
				1.0f);
			SelectedBrush = MakeShared<FSlateRoundedBoxBrush>(
				FLinearColor(0.08f, 0.24f, 0.38f, 0.96f),
				11.0f,
				FLinearColor(0.25f, 0.72f, 1.0f, 0.95f),
				1.5f);
			CardStyle.SetNormal(bWasSelected ? *SelectedBrush : *NormalBrush);
			CardStyle.SetHovered(bWasSelected ? *SelectedBrush : *HoverBrush);
			CardStyle.SetPressed(bWasSelected ? *SelectedBrush : *NormalBrush);
			CardStyle.SetNormalPadding(FMargin(0));
			CardStyle.SetPressedPadding(FMargin(0));
			Construct();
		}

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			DecoratorPosition = DragDropEvent.GetScreenSpacePosition() - GrabOffsetPixels;
			if (TSharedPtr<SWidget> Source = SourceWidget.Pin())
			{
				Source->SetVisibility(EVisibility::Hidden);
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			if (TSharedPtr<SWidget> Source = SourceWidget.Pin())
			{
				Source->SetVisibility(EVisibility::Visible);
			}
			FGameDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SButton)
				.ButtonStyle(&CardStyle)
				.ContentPadding(FMargin(8.0f))
				.IsFocusable(false)
				[
					SNew(SBox).WidthOverride(CardWidth).HeightOverride(92.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(5.0f, 4.0f, 5.0f, 17.0f)
						[
							WeaponIconBrush.GetResourceObject()
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(&WeaponIconBrush).ColorAndOpacity(FLinearColor::White))
								: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(WeaponType)).Font(InventoryFont(11, true)).ColorAndOpacity(InventoryAccent).Justification(ETextJustify::Center))
						]
						+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(1.0f, 0.0f, 1.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(WeaponName)).Font(InventoryFont(8, true)).ColorAndOpacity(FLinearColor(0.9f, 0.93f, 0.98f)).Justification(ETextJustify::Center).AutoWrapText(false)
						]
						+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(bWasEquipped ? TEXT("EQUIPADA") : TEXT(""))).Font(InventoryFont(7, true)).ColorAndOpacity(FLinearColor(0.55f, 0.86f, 1.0f))
						]
					]
				];
		}
	};

	class SArenaInventoryGroundDropTarget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SArenaInventoryGroundDropTarget) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UArenaInventoryWidget>, Owner)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Owner = InArgs._Owner;
			ChildSlot
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.68f))
			];
		}

		virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
		{
			return Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>().IsValid()
				? FReply::Handled()
				: FReply::Unhandled();
		}

		virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
		{
			const TSharedPtr<FArenaInventoryWeaponDragDropOp> Operation = Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>();
			if (!Operation.IsValid())
			{
				return FReply::Unhandled();
			}
			if (UArenaInventoryWidget* InventoryWidget = Owner.Get())
			{
				InventoryWidget->DropWeaponById(Operation->WeaponId);
			}
			return FReply::Handled();
		}

	private:
		TWeakObjectPtr<UArenaInventoryWidget> Owner;
	};
}

class SArenaInventorySlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaInventorySlate) {}
		SLATE_ARGUMENT(UArenaInventoryWidget*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Owner = InArgs._Owner;
		PanelBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.035f, 0.047f, 0.075f, 0.97f), 20.0f, FLinearColor(0.55f, 0.68f, 0.88f, 0.25f), 1.0f);
		SectionBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(1.0f, 1.0f, 1.0f, 0.035f), 13.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f), 1.0f);
		SlotBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.07f, 0.09f, 0.14f, 0.95f), 11.0f, FLinearColor(0.55f, 0.64f, 0.78f, 0.18f), 1.0f);
		SelectedSlotBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.08f, 0.24f, 0.38f, 0.96f), 11.0f, FLinearColor(0.25f, 0.72f, 1.0f, 0.95f), 1.5f);
		PrimaryButtonStyle = MakeInventoryButtonStyle(InventoryAccent);
		SecondaryButtonStyle = MakeInventoryButtonStyle(FLinearColor(0.5f, 0.6f, 0.75f));
		SelectedSlotStyle.SetNormal(*SelectedSlotBrush);
		SelectedSlotStyle.SetHovered(*SelectedSlotBrush);
		SelectedSlotStyle.SetPressed(*SelectedSlotBrush);
		EmptyButtonStyle.SetNormal(*SlotBrush);
		EmptyButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.1f, 0.14f, 0.22f, 1.0f), 11.0f, FLinearColor(0.45f, 0.63f, 0.85f, 0.5f), 1.0f));
		EmptyButtonStyle.SetPressed(*SlotBrush);
		EmptyButtonStyle.SetNormalPadding(FMargin(0));
		EmptyButtonStyle.SetPressedPadding(FMargin(0));
		MaterialNames = { TEXT("Madera"), TEXT("Piedra"), TEXT("Metal") };

		if (UArenaInventoryWidget* InventoryWidget = Owner.Get())
		{
			InventoryWidget->MaterialIcons.SetNum(3);
			const TCHAR* MaterialPaths[] =
			{
				TEXT("/Game/Arena/UI/T_Mat_Wood.T_Mat_Wood"),
				TEXT("/Game/Arena/UI/T_Mat_Stone.T_Mat_Stone"),
				TEXT("/Game/Arena/UI/T_Mat_Metal.T_Mat_Metal")
			};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(MaterialPaths); ++Index)
			{
				InventoryWidget->MaterialIcons[Index] = LoadObject<UTexture2D>(nullptr, MaterialPaths[Index]);
			}
		}

		TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SArenaInventoryGroundDropTarget).Owner(Owner)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(20.0f)
			[
				SNew(SBox).WidthOverride(680.0f).MaxDesiredHeight(790.0f)
				[
					SNew(SBorder)
					.BorderImage(PanelBrush.Get())
					.Padding(FMargin(24.0f, 20.0f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 14.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("INVENTARIO"))).Font(InventoryFont(19, true)).ColorAndOpacity(FLinearColor::White)
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("Recursos y equipo"))).Font(InventoryFont(11)).ColorAndOpacity(FLinearColor(0.62f, 0.69f, 0.79f))
								]
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(&SecondaryButtonStyle)
								.OnClicked_Lambda([WeakThis]()
								{
									if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
									{
										if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
										{
											InventoryWidget->Close();
										}
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("CERRAR  [Esc / Tab]"))).Font(InventoryFont(10, true)).ColorAndOpacity(FLinearColor::White)
								]
							]
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
							+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								MakeMaterialsSection()
							]
							+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								MakeAmmoSection()
							]
							+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 12.0f)
							[
								MakeWeaponsSection()
							]
							+ SScrollBox::Slot()
							[
								MakeDetailsSection()
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 13.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&PrimaryButtonStyle)
								.IsEnabled_Lambda([WeakThis]()
								{
									if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
									{
										if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
										{
											const UFortnitePortingCharacterComponent* Component = InventoryWidget->Inventory.Get();
											return Component
												&& IsValid(InventoryWidget->SelectedWeapon)
												&& Component->Weapons.Contains(InventoryWidget->SelectedWeapon)
												&& Component->GetCurrentWeapon() != InventoryWidget->SelectedWeapon;
										}
									}
									return false;
								})
								.OnClicked_Lambda([WeakThis]()
								{
									if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
									{
										if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
										{
											InventoryWidget->EquipSelectedWeapon();
										}
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("EQUIPAR"))).Font(InventoryFont(11, true)).ColorAndOpacity(FLinearColor::White).Justification(ETextJustify::Center)
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(6.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SButton)
								.ButtonStyle(&SecondaryButtonStyle)
								.IsEnabled_Lambda([WeakThis]()
								{
									if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
									{
										if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
										{
											const UFortnitePortingCharacterComponent* Component = InventoryWidget->Inventory.Get();
											return Component
												&& IsValid(InventoryWidget->SelectedWeapon)
												&& Component->Weapons.Contains(InventoryWidget->SelectedWeapon);
										}
									}
									return false;
								})
								.OnClicked_Lambda([WeakThis]()
								{
									if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
									{
										if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
										{
											InventoryWidget->DropWeapon(InventoryWidget->SelectedWeapon);
										}
									}
									return FReply::Handled();
								})
								[
									SNew(STextBlock).Text(FText::FromString(TEXT("SOLTAR AL SUELO"))).Font(InventoryFont(11, true)).ColorAndOpacity(FLinearColor::White).Justification(ETextJustify::Center)
								]
							]
						]
					]
				]
			]
		];
		Refresh();
	}

	void Refresh(bool bRefreshWeapons = true)
	{
		if (bRefreshWeapons)
		{
			RefreshMaterials();
			RefreshWeapons();
		}
		RefreshAmmo();
		RefreshDetails();
	}

	void RefreshWeaponOrder()
	{
		if (!WeaponsGrid.IsValid() || !Owner.IsValid())
		{
			return;
		}

		TMap<FString, TSharedPtr<SWidget>> ExistingWidgets;
		TMap<FString, FVector2D> PreviousPositions;
		for (const TPair<FString, TWeakPtr<SWidget>>& Slot : WeaponSlotWidgets)
		{
			TSharedPtr<SWidget> Widget = Slot.Value.Pin();
			if (!Widget.IsValid())
			{
				continue;
			}
			FVector2D Position = Widget->GetCachedGeometry().GetAbsolutePosition();
			if (const FSlotAnimation* Animation = SlotAnimations.Find(Slot.Key))
			{
				Position += Animation->CurrentOffset;
			}
			ExistingWidgets.Add(Slot.Key, Widget);
			PreviousPositions.Add(Slot.Key, Position);
		}

		WeaponsGrid->ClearChildren();
		const TArray<TObjectPtr<UFortnitePortingWeaponData>>& Weapons = Owner->DisplayWeapons;
		const int32 SlotCount = FMath::Max(10, Weapons.Num());
		constexpr int32 Columns = 5;
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			UFortnitePortingWeaponData* Weapon = Weapons.IsValidIndex(Index) ? Weapons[Index] : nullptr;
			const int32 Row = Index / Columns;
			const int32 Column = Index % Columns;
			if (IsValid(Weapon))
			{
				const FString WeaponId = Owner->GetWeaponId(Weapon);
				if (TSharedPtr<SWidget>* ExistingWidget = ExistingWidgets.Find(WeaponId))
				{
					WeaponsGrid->AddSlot(Column, Row)[ExistingWidget->ToSharedRef()];
					continue;
				}
			}

			const TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
			WeaponsGrid->AddSlot(Column, Row)
			[
				SNew(SButton)
				.ButtonStyle(&EmptyButtonStyle)
				.ContentPadding(FMargin(4.0f))
				.OnSlateButtonDrop_Lambda([WeakThis, Index](const FGeometry&, const FDragDropEvent& Event)
				{
					const TSharedPtr<FArenaInventoryWeaponDragDropOp> Operation = Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>();
					if (Operation.IsValid())
					{
						if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
						{
							if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
							{
								InventoryWidget->MoveWeaponToIndex(Operation->WeaponId, Index);
							}
						}
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				[
					SNew(SBox).HeightOverride(90.0f)
				]
			];
		}

		SlotAnimations.Reset();
		for (const TPair<FString, FVector2D>& Previous : PreviousPositions)
		{
			if (WeaponSlotWidgets.Contains(Previous.Key))
			{
				FSlotAnimation& Animation = SlotAnimations.Add(Previous.Key);
				Animation.PreviousPosition = Previous.Value;
			}
		}
		RefreshDetails();
	}

private:
	struct FSlotAnimation
	{
		FVector2D PreviousPosition = FVector2D::ZeroVector;
		FVector2D StartOffset = FVector2D::ZeroVector;
		FVector2D CurrentOffset = FVector2D::ZeroVector;
		float Elapsed = 0.0f;
		bool bStarted = false;
	};

	virtual void Tick(const FGeometry& AllottedGeometry, const double CurrentTime, const float DeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, CurrentTime, DeltaTime);
		constexpr float AnimationDuration = 0.18f;
		for (auto It = SlotAnimations.CreateIterator(); It; ++It)
		{
			TWeakPtr<SWidget>* WeakWidget = WeaponSlotWidgets.Find(It.Key());
			TSharedPtr<SWidget> Widget = WeakWidget ? WeakWidget->Pin() : nullptr;
			if (!Widget.IsValid())
			{
				It.RemoveCurrent();
				continue;
			}

			FSlotAnimation& Animation = It.Value();
			const FVector2D CurrentPosition = Widget->GetCachedGeometry().GetAbsolutePosition();
			if (!Animation.bStarted)
			{
				const FVector2D StartOffset = Animation.PreviousPosition - CurrentPosition;
				if (StartOffset.IsNearlyZero(0.5f))
				{
					It.RemoveCurrent();
					continue;
				}
				Animation.bStarted = true;
				Animation.StartOffset = StartOffset;
				Animation.CurrentOffset = StartOffset;
			}

			Animation.Elapsed += DeltaTime;
			const float Alpha = FMath::Clamp(Animation.Elapsed / AnimationDuration, 0.0f, 1.0f);
			const float Remaining = 1.0f - Alpha;
			const float EasedAlpha = 1.0f - Remaining * Remaining * Remaining;
			Animation.CurrentOffset = FMath::Lerp(Animation.StartOffset, FVector2D::ZeroVector, EasedAlpha);
			if (Alpha >= 1.0f)
			{
				Widget->SetRenderTransform(TOptional<FSlateRenderTransform>());
				It.RemoveCurrent();
			}
			else
			{
				const float LayoutScale = FMath::Max(Widget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale(), 0.001f);
				Widget->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(Animation.CurrentOffset / LayoutScale)));
			}
		}
	}

	TSharedRef<SWidget> MakeSectionHeading(const TCHAR* Title, const TCHAR* Subtitle)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Title)).Font(InventoryFont(12, true)).ColorAndOpacity(FLinearColor(0.86f, 0.91f, 0.98f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Subtitle)).Font(InventoryFont(9)).ColorAndOpacity(FLinearColor(0.53f, 0.61f, 0.72f))
			];
	}

	TSharedRef<SWidget> MakeMaterialsSection()
	{
		return SNew(SBorder).BorderImage(SectionBrush.Get()).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("MATERIALES"), TEXT("El modo de construcción actual no consume recursos."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(MaterialsRow, SHorizontalBox)
			]
		];
	}

	TSharedRef<SWidget> MakeAmmoSection()
	{
		return SNew(SBorder).BorderImage(SectionBrush.Get()).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("BALAS"), TEXT("Munición real del cargador del arma equipada."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SBorder).BorderImage(SlotBrush.Get()).Padding(FMargin(12.0f, 9.0f))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("●"))).Font(InventoryFont(17, true)).ColorAndOpacity(InventoryAccent)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(AmmoDescriptionText, STextBlock).Text(FText::FromString(TEXT("Sin arma equipada"))).Font(InventoryFont(10, true)).ColorAndOpacity(FLinearColor(0.77f, 0.83f, 0.91f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SAssignNew(AmmoCountText, STextBlock).Text(FText::FromString(TEXT("—"))).Font(InventoryFont(18, true)).ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		];
	}

	TSharedRef<SWidget> MakeWeaponsSection()
	{
		return SNew(SBorder).BorderImage(SectionBrush.Get()).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("ARMAS"), TEXT("Selecciona un arma para consultar sus estadísticas."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(WeaponsGrid, SUniformGridPanel).SlotPadding(FMargin(6.0f))
			]
		];
	}

	TSharedRef<SWidget> MakeDetailsSection()
	{
		return SNew(SBorder).BorderImage(SectionBrush.Get()).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("DETALLES DEL ARMA"))).Font(InventoryFont(10, true)).ColorAndOpacity(FLinearColor(0.58f, 0.68f, 0.8f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SAssignNew(DetailsBox, SVerticalBox)
			]
		];
	}

	void RefreshMaterials()
	{
		if (!MaterialsRow.IsValid())
		{
			return;
		}

		MaterialsRow->ClearChildren();
		UArenaInventoryWidget* InventoryWidget = Owner.Get();
		for (int32 Index = 0; Index < MaterialNames.Num(); ++Index)
		{
			UTexture2D* Texture = InventoryWidget && InventoryWidget->MaterialIcons.IsValidIndex(Index)
				? InventoryWidget->MaterialIcons[Index]
				: nullptr;
			FSlateBrush* Brush = GetIconBrush(Texture);
			MaterialsRow->AddSlot().FillWidth(1.0f).Padding(3.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(SlotBrush.Get()).Padding(FMargin(9.0f, 7.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 9.0f, 0.0f)
					[
						SNew(SBox).WidthOverride(32.0f).HeightOverride(32.0f)
						[
							Brush
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush))
								: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(TEXT("◆"))).Font(InventoryFont(16, true)).ColorAndOpacity(FLinearColor(0.6f, 0.75f, 0.65f)))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(MaterialNames[Index])).Font(InventoryFont(10, true)).ColorAndOpacity(FLinearColor::White)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("ILIMITADO"))).Font(InventoryFont(8, true)).ColorAndOpacity(FLinearColor(0.58f, 0.79f, 0.66f))
						]
					]
				]
			];
		}
	}

	void RefreshAmmo()
	{
		if (!AmmoCountText.IsValid() || !AmmoDescriptionText.IsValid())
		{
			return;
		}

		const UFortnitePortingCharacterComponent* Component = Owner.IsValid() ? Owner->Inventory.Get() : nullptr;
		const UFortnitePortingWeaponData* Weapon = Component ? Component->GetCurrentWeapon() : nullptr;
		if (!Weapon || Weapon->MagazineSize <= 0)
		{
			AmmoDescriptionText->SetText(FText::FromString(Weapon ? TEXT("Este objeto no usa balas") : TEXT("Sin arma equipada")));
			AmmoCountText->SetText(FText::FromString(TEXT("—")));
			return;
		}

		AmmoDescriptionText->SetText(FText::FromString(FString::Printf(TEXT("%s · cargador"), *Weapon->WeaponType.ToString())));
		AmmoCountText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Component->GetAmmoInMagazine(), Weapon->MagazineSize)));
	}

	void RefreshWeapons()
	{
		if (!WeaponsGrid.IsValid())
		{
			return;
		}

		WeaponsGrid->ClearChildren();
		WeaponSlotWidgets.Reset();
		const UFortnitePortingCharacterComponent* Component = Owner.IsValid() ? Owner->Inventory.Get() : nullptr;
		const TArray<TObjectPtr<UFortnitePortingWeaponData>>* Weapons = Owner.IsValid() ? &Owner->DisplayWeapons : nullptr;
		const int32 WeaponCount = Weapons ? Weapons->Num() : 0;
		const int32 SlotCount = FMath::Max(10, WeaponCount);
		const int32 Columns = 5;

		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			UFortnitePortingWeaponData* Weapon = Weapons && Weapons->IsValidIndex(Index) ? (*Weapons)[Index] : nullptr;
			const int32 Row = Index / Columns;
			const int32 Column = Index % Columns;
			if (!IsValid(Weapon))
			{
				TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
				WeaponsGrid->AddSlot(Column, Row)
				[
					SNew(SButton)
					.ButtonStyle(&EmptyButtonStyle)
					.ContentPadding(FMargin(4.0f))
					.OnSlateButtonDrop_Lambda([WeakThis, Index](const FGeometry&, const FDragDropEvent& Event)
					{
						const TSharedPtr<FArenaInventoryWeaponDragDropOp> Operation = Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>();
						if (Operation.IsValid())
						{
							if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
							{
								if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
								{
									InventoryWidget->MoveWeaponToIndex(Operation->WeaponId, Index);
								}
							}
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[
						SNew(SBox).HeightOverride(90.0f)
					]
				];
				continue;
			}

			const bool bSelected = Owner->SelectedWeapon == Weapon;
			const bool bEquipped = Component->GetCurrentWeapon() == Weapon;
			const FString WeaponId = Owner->GetWeaponId(Weapon);
			const FString Name = Weapon->DisplayName.IsEmpty() ? Weapon->GetName() : Weapon->DisplayName.ToString();
			const FString WeaponType = Weapon->WeaponType.ToString();
			TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
			TSharedRef<SButton> WeaponButton =
				SNew(SButton)
				.ButtonStyle(bSelected ? &SelectedSlotStyle : &EmptyButtonStyle)
				.AllowDragDrop(true)
				.ContentPadding(FMargin(8.0f))
				.OnClicked_Lambda([WeakThis, WeaponId]()
				{
					if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
					{
						if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
						{
							InventoryWidget->SelectWeapon(WeaponId);
						}
					}
					return FReply::Handled();
				})
				.OnSlateButtonDragDetected_Lambda([WeakThis, WeaponId, Name, WeaponType, bSelected, bEquipped, WeaponIcon = Weapon->Icon](const FGeometry& Geometry, const FPointerEvent& PointerEvent)
				{
					TSharedRef<FArenaInventoryWeaponDragDropOp> Operation = MakeShared<FArenaInventoryWeaponDragDropOp>();
					Operation->WeaponId = WeaponId;
					Operation->WeaponName = Name;
					Operation->WeaponType = WeaponType;
					Operation->CardWidth = Geometry.GetLocalSize().X;
					Operation->GrabOffsetPixels = PointerEvent.GetScreenSpacePosition() - Geometry.GetAbsolutePosition();
					Operation->bWasSelected = bSelected;
					Operation->bWasEquipped = bEquipped;
					if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
					{
						Operation->SourceWidget = Pinned->WeaponSlotWidgets.FindRef(WeaponId);
					}
					if (IsValid(WeaponIcon))
					{
						Operation->WeaponIconBrush.SetResourceObject(WeaponIcon);
						Operation->WeaponIconBrush.ImageSize = FVector2D(64.0f, 64.0f);
						Operation->WeaponIconBrush.DrawAs = ESlateBrushDrawType::Image;
					}
					Operation->Begin();
					return FReply::Handled().BeginDragDrop(Operation);
				})
				.OnSlateButtonDragOver_Lambda([WeakThis, TargetWeaponId = WeaponId](const FGeometry&, const FDragDropEvent& Event)
				{
					const TSharedPtr<FArenaInventoryWeaponDragDropOp> Operation = Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>();
					if (!Operation.IsValid())
					{
						return FReply::Unhandled();
					}
					if (Operation->WeaponId == TargetWeaponId)
					{
						return FReply::Handled();
					}
					if (Operation->LastHoveredWeaponId != TargetWeaponId)
					{
						Operation->LastHoveredWeaponId = TargetWeaponId;
						if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
						{
							if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
							{
								InventoryWidget->SwapWeaponsById(Operation->WeaponId, TargetWeaponId);
							}
						}
					}
					return FReply::Handled();
				})
				.OnSlateButtonDrop_Lambda([WeakThis, TargetWeaponId = WeaponId](const FGeometry&, const FDragDropEvent& Event)
				{
					const TSharedPtr<FArenaInventoryWeaponDragDropOp> Operation = Event.GetOperationAs<FArenaInventoryWeaponDragDropOp>();
					if (!Operation.IsValid())
					{
						return FReply::Unhandled();
					}
					if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
					{
						if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
						{
							if (Operation->LastHoveredWeaponId != TargetWeaponId && Operation->WeaponId != TargetWeaponId)
							{
								InventoryWidget->SwapWeaponsById(Operation->WeaponId, TargetWeaponId);
							}
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SBox).HeightOverride(92.0f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(5.0f, 4.0f, 5.0f, 17.0f)
						[
							Weapon->Icon
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(GetIconBrush(Weapon->Icon)).ColorAndOpacity(FLinearColor::White))
								: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Weapon->WeaponType.ToString())).Font(InventoryFont(11, true)).ColorAndOpacity(InventoryAccent).Justification(ETextJustify::Center))
						]
						+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(1.0f, 0.0f, 1.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(Name)).Font(InventoryFont(8, true)).ColorAndOpacity(FLinearColor(0.9f, 0.93f, 0.98f)).Justification(ETextJustify::Center).AutoWrapText(false)
						]
						+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(bEquipped ? TEXT("EQUIPADA") : TEXT(""))).Font(InventoryFont(7, true)).ColorAndOpacity(FLinearColor(0.55f, 0.86f, 1.0f))
						]
					]
				];
			WeaponSlotWidgets.Add(WeaponId, WeaponButton);
			WeaponsGrid->AddSlot(Column, Row)[WeaponButton];
		}
	}

	void RefreshDetails()
	{
		if (!DetailsBox.IsValid())
		{
			return;
		}

		DetailsBox->ClearChildren();
		const UArenaInventoryWidget* InventoryWidget = Owner.Get();
		const UFortnitePortingWeaponData* Weapon = InventoryWidget ? InventoryWidget->SelectedWeapon : nullptr;
		const UFortnitePortingCharacterComponent* Component = InventoryWidget ? InventoryWidget->Inventory.Get() : nullptr;
		if (!IsValid(Weapon) || !Component || !Component->Weapons.Contains(Weapon))
		{
			DetailsBox->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Selecciona un arma para ver sus estadísticas."))).Font(InventoryFont(10)).ColorAndOpacity(FLinearColor(0.58f, 0.65f, 0.75f))
			];
			return;
		}

		const bool bEquipped = Component->GetCurrentWeapon() == Weapon;
		const FString Ammo = bEquipped && Weapon->MagazineSize > 0
			? FString::Printf(TEXT("%d / %d"), Component->GetAmmoInMagazine(), Weapon->MagazineSize)
			: (Weapon->MagazineSize > 0 ? FString::Printf(TEXT("%d"), Weapon->MagazineSize) : TEXT("—"));
		const FString Stats = FString::Printf(
			TEXT("%s     DAÑO  %.0f     CADENCIA  %.1f/s     CARGADOR  %s     RECARGA  %.1fs"),
			*Weapon->WeaponType.ToString(),
			FMath::Max(Weapon->Damage, 0.0f),
			Weapon->FireInterval > 0.0f ? 1.0f / Weapon->FireInterval : 0.0f,
			*Ammo,
			FMath::Max(Weapon->ReloadTime, 0.0f));

		DetailsBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Text(FText::FromString(Weapon->DisplayName.IsEmpty() ? Weapon->GetName() : Weapon->DisplayName.ToString())).Font(InventoryFont(13, true)).ColorAndOpacity(FLinearColor::White)
		];
		DetailsBox->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock).Text(FText::FromString(Stats)).Font(InventoryFont(9)).ColorAndOpacity(FLinearColor(0.72f, 0.79f, 0.88f))
		];
	}

	FSlateBrush* GetIconBrush(UTexture2D* Texture)
	{
		if (!IsValid(Texture))
		{
			return nullptr;
		}

		TSharedPtr<FSlateBrush>& Brush = IconBrushCache.FindOrAdd(Texture);
		if (!Brush.IsValid())
		{
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Texture);
			Brush->ImageSize = FVector2D(64.0f, 64.0f);
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
		return Brush.Get();
	}

	TWeakObjectPtr<UArenaInventoryWidget> Owner;
	TSharedPtr<FSlateRoundedBoxBrush> PanelBrush;
	TSharedPtr<FSlateRoundedBoxBrush> SectionBrush;
	TSharedPtr<FSlateRoundedBoxBrush> SlotBrush;
	TSharedPtr<FSlateRoundedBoxBrush> SelectedSlotBrush;
	FButtonStyle PrimaryButtonStyle;
	FButtonStyle SecondaryButtonStyle;
	FButtonStyle SelectedSlotStyle;
	FButtonStyle EmptyButtonStyle;
	TArray<FString> MaterialNames;
	TMap<FString, TWeakPtr<SWidget>> WeaponSlotWidgets;
	TMap<FString, FSlotAnimation> SlotAnimations;
	TMap<TWeakObjectPtr<UTexture2D>, TSharedPtr<FSlateBrush>> IconBrushCache;
	TSharedPtr<SHorizontalBox> MaterialsRow;
	TSharedPtr<SUniformGridPanel> WeaponsGrid;
	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<STextBlock> AmmoDescriptionText;
	TSharedPtr<STextBlock> AmmoCountText;
};

void UArenaInventoryWidget::InitializeInventory(AArenaPlayerController* InController, UFortnitePortingCharacterComponent* InInventory)
{
	OwnerController = InController;
	Inventory = InInventory;
	SetIsFocusable(true);
}

TSharedRef<SWidget> UArenaInventoryWidget::RebuildWidget()
{
	InventorySlate = SNew(SArenaInventorySlate).Owner(this);
	RefreshSlateState();
	return InventorySlate.ToSharedRef();
}

void UArenaInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshTimer -= InDeltaTime;
	if (RefreshTimer > 0.0f)
	{
		return;
	}

	RefreshTimer = 0.15f;
	UFortnitePortingCharacterComponent* Component = Inventory.Get();
	if (!Component)
	{
		return;
	}

	const int32 CurrentAmmo = Component->GetAmmoInMagazine();
	const bool bWeaponsChanged = LastWeaponSnapshot != Component->Weapons
		|| LastEquippedSnapshot != Component->GetCurrentWeapon();
	if (bWeaponsChanged || LastAmmoSnapshot != CurrentAmmo)
	{
		RefreshSlateState(bWeaponsChanged);
	}
}

FReply UArenaInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape
		|| InKeyEvent.GetKey() == ArenaControls::Get(ArenaControls::EAction::Inventory))
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UArenaInventoryWidget::RefreshSlateState(bool bRebuildWeapons)
{
	UFortnitePortingCharacterComponent* Component = Inventory.Get();
	if (!Component || !InventorySlate.IsValid())
	{
		return;
	}

	LastWeaponSnapshot = Component->Weapons;
	LastEquippedSnapshot = Component->GetCurrentWeapon();
	LastAmmoSnapshot = Component->GetAmmoInMagazine();
	if (bRebuildWeapons)
	{
		DisplayWeapons.RemoveAll([Component](const TObjectPtr<UFortnitePortingWeaponData>& Weapon)
		{
			return !IsValid(Weapon) || !Component->Weapons.Contains(Weapon);
		});
		for (UFortnitePortingWeaponData* Weapon : Component->Weapons)
		{
			if (IsValid(Weapon))
			{
				DisplayWeapons.AddUnique(Weapon);
			}
		}
	}
	if (!IsValid(SelectedWeapon) || !Component->Weapons.Contains(SelectedWeapon))
	{
		SelectedWeapon = Component->Weapons.IsEmpty() ? nullptr : Component->Weapons[0];
	}
	InventorySlate->Refresh(bRebuildWeapons);
}

void UArenaInventoryWidget::SelectWeapon(const FString& WeaponId)
{
	if (UFortnitePortingWeaponData* Weapon = FindWeaponById(WeaponId))
	{
		SelectedWeapon = Weapon;
		RefreshSlateState();
	}
}

void UArenaInventoryWidget::MoveWeaponToIndex(const FString& WeaponId, int32 TargetIndex)
{
	if (!Inventory.IsValid() || !InventorySlate.IsValid() || DisplayWeapons.IsEmpty())
	{
		return;
	}

	UFortnitePortingWeaponData* Weapon = FindWeaponById(WeaponId);
	const int32 SourceIndex = DisplayWeapons.IndexOfByKey(Weapon);
	if (!IsValid(Weapon) || SourceIndex == INDEX_NONE)
	{
		return;
	}

	TargetIndex = FMath::Clamp(TargetIndex, 0, DisplayWeapons.Num() - 1);
	if (SourceIndex == TargetIndex)
	{
		return;
	}

	DisplayWeapons.RemoveAt(SourceIndex);
	DisplayWeapons.Insert(Weapon, TargetIndex);
	Inventory->RequestReorderInventoryWeapon(Weapon, TargetIndex);
	RefreshSlateState(true);
}

void UArenaInventoryWidget::SwapWeaponsById(const FString& FirstWeaponId, const FString& SecondWeaponId)
{
	if (!Inventory.IsValid() || !InventorySlate.IsValid() || FirstWeaponId == SecondWeaponId)
	{
		return;
	}

	UFortnitePortingWeaponData* FirstWeapon = FindWeaponById(FirstWeaponId);
	UFortnitePortingWeaponData* SecondWeapon = FindWeaponById(SecondWeaponId);
	const int32 FirstIndex = DisplayWeapons.IndexOfByKey(FirstWeapon);
	const int32 SecondIndex = DisplayWeapons.IndexOfByKey(SecondWeapon);
	if (!IsValid(FirstWeapon) || !IsValid(SecondWeapon) || FirstIndex == INDEX_NONE || SecondIndex == INDEX_NONE)
	{
		return;
	}

	DisplayWeapons.Swap(FirstIndex, SecondIndex);
	Inventory->RequestReorderInventoryWeapon(FirstWeapon, SecondIndex);
	Inventory->RequestReorderInventoryWeapon(SecondWeapon, FirstIndex);
	InventorySlate->RefreshWeaponOrder();
}

void UArenaInventoryWidget::EquipSelectedWeapon()
{
	if (Inventory.IsValid() && IsValid(SelectedWeapon) && Inventory->Weapons.Contains(SelectedWeapon))
	{
		Inventory->RequestEquipInventoryWeapon(SelectedWeapon);
		RefreshSlateState();
	}
}

UFortnitePortingWeaponData* UArenaInventoryWidget::FindWeaponById(const FString& WeaponId) const
{
	UFortnitePortingCharacterComponent* Component = Inventory.Get();
	if (!Component || WeaponId.IsEmpty())
	{
		return nullptr;
	}

	for (UFortnitePortingWeaponData* Weapon : Component->Weapons)
	{
		if (IsValid(Weapon) && GetWeaponId(Weapon) == WeaponId)
		{
			return Weapon;
		}
	}
	return nullptr;
}

FString UArenaInventoryWidget::GetWeaponId(const UFortnitePortingWeaponData* Weapon) const
{
	return IsValid(Weapon) ? Weapon->GetPathName() : FString();
}

void UArenaInventoryWidget::DropWeapon(UFortnitePortingWeaponData* Weapon)
{
	if (Inventory.IsValid() && IsValid(Weapon) && Inventory->Weapons.Contains(Weapon))
	{
		Inventory->RequestDropInventoryWeapon(Weapon);
		RefreshSlateState();
	}
}

void UArenaInventoryWidget::DropWeaponById(const FString& WeaponId)
{
	DropWeapon(FindWeaponById(WeaponId));
}

void UArenaInventoryWidget::Close()
{
	if (AArenaPlayerController* Controller = OwnerController.Get())
	{
		Controller->CloseInventory();
	}
	else
	{
		RemoveFromParent();
	}
}

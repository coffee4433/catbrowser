#include "ArenaInventoryWidget.h"

#include "Arena.h"
#include "ArenaControls.h"
#include "ArenaPlayerController.h"
#include "ArenaGlassStyle.h"
#include "ArenaUISounds.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Input/DragAndDrop.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor InventoryAccent(0.55f, 0.80f, 1.0f);

	FSlateFontInfo InventoryFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
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
			NormalBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.06f, 0.09f, 0.18f, 0.88f), 14.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.30f), 1.2f);
			HoverBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.10f, 0.16f, 0.30f, 0.92f), 14.0f, FLinearColor(0.80f, 0.92f, 1.0f, 0.90f), 1.6f);
			SelectedBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.15f, 0.42f, 0.75f, 0.80f), 14.0f, FLinearColor(0.60f, 0.88f, 1.0f, 1.0f), 2.0f);
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
					SNew(SBox).WidthOverride(FMath::Max(CardWidth - 16.0f, 0.0f)).HeightOverride(80.0f)
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
							SNew(STextBlock).Text(FText::FromString(bWasEquipped ? TEXT("EN MANO") : TEXT(""))).Font(InventoryFont(7, true)).ColorAndOpacity(ArenaGlass::Mint)
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
				.BorderBackgroundColor(FLinearColor(0.015f, 0.022f, 0.06f, 0.42f))
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

namespace ArenaInventoryLayout
{
	constexpr int32 Columns = 5;
	constexpr int32 MinSlots = 10;
	constexpr float PanelWidth = 540.0f;
	constexpr float PanelRadius = 26.0f;
	constexpr float CardRadius = 14.0f;
	constexpr float CardHeight = 96.0f;
	constexpr float RevealSeconds = 0.38f;
}

// The inventory, docked on the right as a tall liquid glass panel: the weapon in hand with its magazine, the weapon grid
// (drag to reorder, drop outside the panel to throw it), the stats of the selected weapon and the building materials.
class SArenaInventorySlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaInventorySlate) {}
		SLATE_ARGUMENT(UArenaInventoryWidget*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		using namespace ArenaInventoryLayout;
		Owner = InArgs._Owner;
		PanelSheen = ArenaGlass::Sheen(PanelRadius, 0.12f);
		CardSheen = ArenaGlass::Sheen(CardRadius, 0.10f);

		auto Rounded = [](const FLinearColor& Fill, float Radius, const FLinearColor& Rim, float Width)
		{
			return FSlateRoundedBoxBrush(Fill, Radius, Rim, Width);
		};
		EmptyButtonStyle.SetNormal(Rounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.035f), CardRadius, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f));
		EmptyButtonStyle.SetHovered(Rounded(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), CardRadius, FLinearColor(0.80f, 0.92f, 1.0f, 0.50f), 1.2f));
		EmptyButtonStyle.SetPressed(EmptyButtonStyle.Normal);
		CardButtonStyle.SetNormal(Rounded(FLinearColor(0.06f, 0.09f, 0.18f, 0.72f), CardRadius, FLinearColor(1.0f, 1.0f, 1.0f, 0.20f), 1.0f));
		CardButtonStyle.SetHovered(Rounded(FLinearColor(0.10f, 0.16f, 0.30f, 0.85f), CardRadius, FLinearColor(0.80f, 0.92f, 1.0f, 0.85f), 1.5f));
		CardButtonStyle.SetPressed(Rounded(FLinearColor(0.05f, 0.08f, 0.15f, 0.85f), CardRadius, FLinearColor(0.80f, 0.92f, 1.0f, 0.85f), 1.5f));
		EquippedSlotStyle = CardButtonStyle;
		EquippedSlotStyle.SetNormal(Rounded(FLinearColor(0.05f, 0.20f, 0.22f, 0.70f), CardRadius, FLinearColor(0.37f, 0.92f, 0.83f, 0.80f), 1.4f));
		const FSlateRoundedBoxBrush Selected = Rounded(FLinearColor(0.15f, 0.42f, 0.75f, 0.55f), CardRadius, FLinearColor(0.60f, 0.88f, 1.0f, 1.0f), 2.0f);
		SelectedSlotStyle.SetNormal(Selected);
		SelectedSlotStyle.SetHovered(Selected);
		SelectedSlotStyle.SetPressed(Selected);
		for (FButtonStyle* Style : { &EmptyButtonStyle, &CardButtonStyle, &EquippedSlotStyle, &SelectedSlotStyle })
		{
			Style->SetNormalPadding(FMargin(0));
			Style->SetPressedPadding(FMargin(0));
		}
		PrimaryButtonStyle = ArenaGlass::ButtonStyle(0.34f, 14.0f, 0.75f, ArenaGlass::Ice);
		DangerButtonStyle = ArenaGlass::ButtonStyle(0.24f, 14.0f, 0.60f, ArenaGlass::Coral);
		CloseButtonStyle = ArenaGlass::ButtonStyle(0.10f, 18.0f, 0.35f);

		BarStyle = FProgressBarStyle()
			.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 3.0f))
			.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f))
			.SetMarqueeImage(FSlateRoundedBoxBrush(FLinearColor::White, 3.0f));
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

		const float BlurCorner = ArenaGlass::BlurCorner(PanelRadius);
		ChildSlot
		[
			SNew(SOverlay)
			// Dim + soft blue light behind the panel; dropping a weapon anywhere here throws it to the ground
			+ SOverlay::Slot()
			[
				SAssignNew(Backdrop, SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SArenaInventoryGroundDropTarget).Owner(Owner)
				]
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(0.0f, 0.0f, -460.0f, 0.0f))
				[
					SNew(SBox).WidthOverride(1150.0f).HeightOverride(1150.0f).Visibility(EVisibility::HitTestInvisible)
					[
						SNew(SImage).Image(&LightBrush).ColorAndOpacity(FLinearColor(0.20f, 0.45f, 1.0f, 0.22f))
					]
				]
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(FMargin(0.0f, 36.0f, 36.0f, 36.0f))
			[
				SAssignNew(PanelRoot, SBox).WidthOverride(PanelWidth)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBackgroundBlur).BlurStrength(34.0f).bApplyAlphaToBlur(false).Padding(FMargin(0.0f)).Visibility(EVisibility::HitTestInvisible)
						.CornerRadius(FVector4(BlurCorner, BlurCorner, BlurCorner, BlurCorner))
						[
							SNew(SImage).Image(&PanelSheen)
						]
					]
					+ SOverlay::Slot()
					[
						SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(24.0f, 22.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								MakeHeader()
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
							[
								MakeHeroCard()
							]
							+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 14.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
								+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 12.0f)
								[
									MakeWeaponsSection()
								]
								+ SScrollBox::Slot().Padding(0.0f, 0.0f, 0.0f, 12.0f)
								[
									MakeDetailsSection()
								]
								+ SScrollBox::Slot()
								[
									MakeMaterialsSection()
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
							[
								MakeFooter()
							]
						]
					]
					+ SOverlay::Slot()
					[
						SNew(SImage).Image(&PanelEdge).Visibility(EVisibility::HitTestInvisible)
					]
				]
			]
		];
		ApplyReveal();
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
		const int32 SlotCount = FMath::Max(ArenaInventoryLayout::MinSlots, Weapons.Num());
		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			UFortnitePortingWeaponData* Weapon = Weapons.IsValidIndex(Index) ? Weapons[Index] : nullptr;
			const int32 Row = Index / ArenaInventoryLayout::Columns;
			const int32 Column = Index % ArenaInventoryLayout::Columns;
			if (IsValid(Weapon))
			{
				if (TSharedPtr<SWidget>* ExistingWidget = ExistingWidgets.Find(Owner->GetWeaponId(Weapon)))
				{
					WeaponsGrid->AddSlot(Column, Row)[ExistingWidget->ToSharedRef()];
					continue;
				}
			}
			WeaponsGrid->AddSlot(Column, Row)[MakeEmptySlot(Index)];
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
		if (Reveal < 1.0f)
		{
			Reveal = FMath::Min(Reveal + DeltaTime / ArenaInventoryLayout::RevealSeconds, 1.0f);
			ApplyReveal();
		}

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

	void ApplyReveal()
	{
		// Ease out: the dim fades in while the panel slides in from the right edge
		const float Eased = 1.0f - FMath::Pow(1.0f - Reveal, 3.0f);
		if (Backdrop.IsValid())
		{
			Backdrop->SetRenderOpacity(Eased);
		}
		if (PanelRoot.IsValid())
		{
			PanelRoot->SetRenderOpacity(Eased);
			PanelRoot->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(90.0f * (1.0f - Eased), 0.0f))));
		}
	}

	void PlayHover() const
	{
		if (const UArenaInventoryWidget* InventoryWidget = Owner.Get())
		{
			ArenaUISounds::PlayHover(InventoryWidget);
		}
	}

	TSharedRef<SWidget> MakeBar(TAttribute<TOptional<float>> Percent, const FLinearColor& Color)
	{
		return SNew(SBox).HeightOverride(6.0f)
		[
			SNew(SProgressBar).Style(&BarStyle).Percent(Percent).FillColorAndOpacity(Color).BarFillType(EProgressBarFillType::LeftToRight)
		];
	}

	TSharedRef<SWidget> MakeDot(const FLinearColor& Color, float Size)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SImage).Image(&DotBrush).ColorAndOpacity(Color)
		];
	}

	TSharedRef<SWidget> MakeSectionHeading(const TCHAR* Title, const TCHAR* Subtitle)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Title)).Font(InventoryFont(12, true)).ColorAndOpacity(ArenaGlass::Ink)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock).Text(FText::FromString(Subtitle)).Font(InventoryFont(9)).ColorAndOpacity(ArenaGlass::Dim)
			];
	}

	TSharedRef<SWidget> MakeHeader()
	{
		TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("EQUIPO"))).Font(InventoryFont(10, true)).ColorAndOpacity(ArenaGlass::Mint)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("INVENTARIO"))).Font(InventoryFont(26, true)).ColorAndOpacity(ArenaGlass::Ink)
					.ShadowOffset(FVector2D(0.0f, 2.0f)).ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.05f, 0.5f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 10.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(&PillBrush).Padding(FMargin(12.0f, 6.0f))
				[
					SAssignNew(CountText, STextBlock).Font(InventoryFont(11, true)).ColorAndOpacity(ArenaGlass::Ink)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(40.0f).HeightOverride(40.0f)
				[
					SNew(SButton)
					.ButtonStyle(&CloseButtonStyle)
					.HAlign(HAlign_Center).VAlign(VAlign_Center)
					.ToolTipText(FText::FromString(TEXT("Cerrar  [Esc / Tab]")))
					.OnHovered_Lambda([WeakThis]() { if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin()) { Pinned->PlayHover(); } })
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
						SNew(STextBlock).Text(FText::FromString(TEXT("X"))).Font(InventoryFont(13, true)).ColorAndOpacity(ArenaGlass::Ink)
					]
				]
			];
	}

	TSharedRef<SWidget> MakeHeroCard()
	{
		return SNew(SBorder).BorderImage(&HeroBrush).Padding(FMargin(16.0f, 14.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(76.0f).HeightOverride(76.0f)
					[
						SNew(SBorder).BorderImage(&ChipBrush).Padding(FMargin(8.0f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SAssignNew(HeroIcon, SImage)
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(14.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							MakeDot(ArenaGlass::Mint, 7.0f)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("EN MANO"))).Font(InventoryFont(9, true)).ColorAndOpacity(ArenaGlass::Mint)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
					[
						SAssignNew(HeroNameText, STextBlock).Font(InventoryFont(16, true)).ColorAndOpacity(ArenaGlass::Ink)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SAssignNew(AmmoDescriptionText, STextBlock).Font(InventoryFont(10)).ColorAndOpacity(ArenaGlass::Dim)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SAssignNew(AmmoCountText, STextBlock).Font(InventoryFont(24, true)).ColorAndOpacity(ArenaGlass::Ink)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("MUNICIÓN"))).Font(InventoryFont(8, true)).ColorAndOpacity(ArenaGlass::Dim)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				MakeBar(TAttribute<TOptional<float>>::CreateLambda([this]() { return TOptional<float>(AmmoPercent); }), ArenaGlass::Ice)
			]
		];
	}

	TSharedRef<SWidget> MakeWeaponsSection()
	{
		return SNew(SBorder).BorderImage(&SectionBrush).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("ARMAS"), TEXT("Arrastra para ordenar · suéltala fuera del panel para tirarla"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(WeaponsGrid, SUniformGridPanel).SlotPadding(FMargin(4.0f))
			]
		];
	}

	TSharedRef<SWidget> MakeDetailsSection()
	{
		return SNew(SBorder).BorderImage(&SectionBrush).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("DETALLES"), TEXT("Estadísticas del arma seleccionada"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(DetailsBox, SVerticalBox)
			]
		];
	}

	TSharedRef<SWidget> MakeMaterialsSection()
	{
		return SNew(SBorder).BorderImage(&SectionBrush).Padding(FMargin(14.0f, 12.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeSectionHeading(TEXT("MATERIALES"), TEXT("El modo de construcción actual no consume recursos"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(MaterialsRow, SHorizontalBox)
			]
		];
	}

	TSharedRef<SWidget> MakeFooter()
	{
		TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
		auto SelectedInInventory = [WeakThis](bool bRequireNotEquipped)
		{
			if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin())
			{
				if (UArenaInventoryWidget* InventoryWidget = Pinned->Owner.Get())
				{
					const UFortnitePortingCharacterComponent* Component = InventoryWidget->Inventory.Get();
					return Component
						&& IsValid(InventoryWidget->SelectedWeapon)
						&& Component->Weapons.Contains(InventoryWidget->SelectedWeapon)
						&& (!bRequireNotEquipped || Component->GetCurrentWeapon() != InventoryWidget->SelectedWeapon);
				}
			}
			return false;
		};

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(48.0f)
					[
						SNew(SButton)
						.ButtonStyle(&PrimaryButtonStyle)
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.IsEnabled_Lambda([SelectedInInventory]() { return SelectedInInventory(true); })
						.OnHovered_Lambda([WeakThis]() { if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin()) { Pinned->PlayHover(); } })
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
							SNew(STextBlock).Text(FText::FromString(TEXT("EQUIPAR"))).Font(InventoryFont(12, true)).ColorAndOpacity(ArenaGlass::Ink)
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox).HeightOverride(48.0f)
					[
						SNew(SButton)
						.ButtonStyle(&DangerButtonStyle)
						.HAlign(HAlign_Center).VAlign(VAlign_Center)
						.IsEnabled_Lambda([SelectedInInventory]() { return SelectedInInventory(false); })
						.OnHovered_Lambda([WeakThis]() { if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin()) { Pinned->PlayHover(); } })
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
							SNew(STextBlock).Text(FText::FromString(TEXT("SOLTAR AL SUELO"))).Font(InventoryFont(12, true)).ColorAndOpacity(ArenaGlass::Ink)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("ESC / TAB para cerrar"))).Font(InventoryFont(9, true)).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.40f))
			];
	}

	TSharedRef<SWidget> MakeEmptySlot(int32 Index)
	{
		const TWeakPtr<SArenaInventorySlate> WeakThis = SharedThis(this);
		return SNew(SButton)
			.ButtonStyle(&EmptyButtonStyle)
			.ContentPadding(FMargin(0.0f))
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
				SNew(SBox).HeightOverride(ArenaInventoryLayout::CardHeight).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::AsNumber(Index + 1)).Font(InventoryFont(11, true)).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.16f))
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
				SNew(SBorder).BorderImage(&ChipBrush).Padding(FMargin(10.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 9.0f, 0.0f)
					[
						SNew(SBox).WidthOverride(30.0f).HeightOverride(30.0f)
						[
							Brush
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush))
								: StaticCastSharedRef<SWidget>(MakeDot(ArenaGlass::Mint, 12.0f))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(MaterialNames[Index])).Font(InventoryFont(10, true)).ColorAndOpacity(ArenaGlass::Ink)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::FromString(TEXT("ILIMITADO"))).Font(InventoryFont(8, true)).ColorAndOpacity(ArenaGlass::Mint)
						]
					]
				]
			];
		}
	}

	void RefreshAmmo()
	{
		if (!AmmoCountText.IsValid() || !AmmoDescriptionText.IsValid() || !HeroNameText.IsValid() || !HeroIcon.IsValid())
		{
			return;
		}

		const UFortnitePortingCharacterComponent* Component = Owner.IsValid() ? Owner->Inventory.Get() : nullptr;
		const UFortnitePortingWeaponData* Weapon = Component ? Component->GetCurrentWeapon() : nullptr;
		FSlateBrush* Icon = Weapon ? GetIconBrush(Weapon->Icon) : nullptr;
		HeroIcon->SetImage(Icon);
		HeroIcon->SetVisibility(Icon ? EVisibility::HitTestInvisible : EVisibility::Hidden);
		HeroNameText->SetText(FText::FromString(Weapon ? (Weapon->DisplayName.IsEmpty() ? Weapon->GetName() : Weapon->DisplayName.ToString()) : FString(TEXT("Manos libres"))));
		if (!Weapon || Weapon->MagazineSize <= 0)
		{
			AmmoDescriptionText->SetText(FText::FromString(Weapon ? TEXT("Este objeto no usa balas") : TEXT("Sin arma equipada")));
			AmmoCountText->SetText(FText::FromString(TEXT("—")));
			AmmoPercent = 0.0f;
			return;
		}

		const int32 InMagazine = Component->GetAmmoInMagazine();
		AmmoDescriptionText->SetText(FText::FromString(FString::Printf(TEXT("%s · cargador"), *Weapon->WeaponType.ToString())));
		AmmoCountText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), InMagazine, Weapon->MagazineSize)));
		AmmoPercent = FMath::Clamp(static_cast<float>(InMagazine) / Weapon->MagazineSize, 0.0f, 1.0f);
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
		const int32 SlotCount = FMath::Max(ArenaInventoryLayout::MinSlots, WeaponCount);
		if (CountText.IsValid())
		{
			CountText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), WeaponCount, SlotCount)));
		}

		for (int32 Index = 0; Index < SlotCount; ++Index)
		{
			UFortnitePortingWeaponData* Weapon = Weapons && Weapons->IsValidIndex(Index) ? (*Weapons)[Index] : nullptr;
			const int32 Row = Index / ArenaInventoryLayout::Columns;
			const int32 Column = Index % ArenaInventoryLayout::Columns;
			if (!IsValid(Weapon) || !Component)
			{
				WeaponsGrid->AddSlot(Column, Row)[MakeEmptySlot(Index)];
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
				.ButtonStyle(bSelected ? &SelectedSlotStyle : (bEquipped ? &EquippedSlotStyle : &CardButtonStyle))
				.AllowDragDrop(true)
				.ContentPadding(FMargin(0.0f))
				.OnHovered_Lambda([WeakThis]() { if (TSharedPtr<SArenaInventorySlate> Pinned = WeakThis.Pin()) { Pinned->PlayHover(); } })
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
					SNew(SBox).HeightOverride(ArenaInventoryLayout::CardHeight)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							SNew(SImage).Image(&CardSheen).Visibility(EVisibility::HitTestInvisible)
						]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(8.0f, 14.0f, 8.0f, 22.0f)
						[
							Weapon->Icon
								? StaticCastSharedRef<SWidget>(SNew(SImage).Image(GetIconBrush(Weapon->Icon)).ColorAndOpacity(FLinearColor::White))
								: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(WeaponType)).Font(InventoryFont(10, true)).ColorAndOpacity(ArenaGlass::Ice).Justification(ETextJustify::Center))
						]
						+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.0f, 5.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock).Text(FText::AsNumber(Index + 1)).Font(InventoryFont(9, true)).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.45f))
						]
						+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, 7.0f, 7.0f, 0.0f)
						[
							SNew(SBox).Visibility(bEquipped ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
							[
								MakeDot(ArenaGlass::Mint, 8.0f)
							]
						]
						+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(4.0f, 0.0f, 4.0f, 6.0f)
						[
							SNew(STextBlock).Text(FText::FromString(Name)).Font(InventoryFont(8, true)).ColorAndOpacity(ArenaGlass::Ink).Justification(ETextJustify::Center).AutoWrapText(false)
						]
					]
				];
			WeaponSlotWidgets.Add(WeaponId, WeaponButton);
			WeaponsGrid->AddSlot(Column, Row)[WeaponButton];
		}
	}

	TSharedRef<SWidget> MakeStat(const TCHAR* Label, const FString& Value, float Percent, const FLinearColor& Color)
	{
		const float Clamped = FMath::Clamp(Percent, 0.04f, 1.0f);
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Bottom)
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(InventoryFont(9, true)).ColorAndOpacity(ArenaGlass::Dim)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
				[
					SNew(STextBlock).Text(FText::FromString(Value)).Font(InventoryFont(12, true)).ColorAndOpacity(ArenaGlass::Ink)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
			[
				MakeBar(TOptional<float>(Clamped), Color)
			];
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
			DetailsBox->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Selecciona un arma para ver sus estadísticas."))).Font(InventoryFont(10)).ColorAndOpacity(ArenaGlass::Dim)
			];
			return;
		}

		const bool bEquipped = Component->GetCurrentWeapon() == Weapon;
		const FString Magazine = bEquipped && Weapon->MagazineSize > 0
			? FString::Printf(TEXT("%d / %d"), Component->GetAmmoInMagazine(), Weapon->MagazineSize)
			: (Weapon->MagazineSize > 0 ? FString::Printf(TEXT("%d"), Weapon->MagazineSize) : FString(TEXT("—")));
		const float Damage = FMath::Max(Weapon->Damage, 0.0f);
		const float FireRate = Weapon->FireInterval > 0.0f ? 1.0f / Weapon->FireInterval : 0.0f;
		const float Reload = FMath::Max(Weapon->ReloadTime, 0.0f);

		DetailsBox->AddSlot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Weapon->DisplayName.IsEmpty() ? Weapon->GetName() : Weapon->DisplayName.ToString())).Font(InventoryFont(15, true)).ColorAndOpacity(ArenaGlass::Ink)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(&PillBrush).Padding(FMargin(10.0f, 4.0f))
				[
					SNew(STextBlock).Text(FText::FromString(Weapon->WeaponType.ToString().ToUpper())).Font(InventoryFont(9, true)).ColorAndOpacity(ArenaGlass::Ice)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBorder).BorderImage(&EquippedPillBrush).Padding(FMargin(10.0f, 4.0f)).Visibility(bEquipped ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("EN MANO"))).Font(InventoryFont(9, true)).ColorAndOpacity(ArenaGlass::Ink)
				]
			]
		];

		TSharedRef<SUniformGridPanel> Stats = SNew(SUniformGridPanel).SlotPadding(FMargin(6.0f, 6.0f));
		Stats->AddSlot(0, 0)[MakeStat(TEXT("DAÑO"), FString::Printf(TEXT("%.0f"), Damage), Damage / 150.0f, ArenaGlass::Coral)];
		Stats->AddSlot(1, 0)[MakeStat(TEXT("CADENCIA"), FString::Printf(TEXT("%.1f/s"), FireRate), FireRate / 15.0f, ArenaGlass::Amber)];
		Stats->AddSlot(0, 1)[MakeStat(TEXT("CARGADOR"), Magazine, Weapon->MagazineSize / 60.0f, ArenaGlass::Ice)];
		// Shorter reloads fill more of the bar
		Stats->AddSlot(1, 1)[MakeStat(TEXT("RECARGA"), FString::Printf(TEXT("%.1fs"), Reload), Reload > 0.0f ? 1.0f - Reload / 5.0f : 0.0f, ArenaGlass::Mint)];
		DetailsBox->AddSlot().AutoHeight().Padding(-6.0f, 8.0f, -6.0f, 0.0f)
		[
			Stats
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
	FSlateRoundedBoxBrush PanelBrush = FSlateRoundedBoxBrush(FLinearColor(0.03f, 0.05f, 0.11f, 0.58f), ArenaInventoryLayout::PanelRadius, FLinearColor(1.0f, 1.0f, 1.0f, 0.38f), 1.3f);
	FSlateRoundedBoxBrush PanelEdge = ArenaGlass::EdgeGlow(ArenaInventoryLayout::PanelRadius, 0.09f);
	FSlateBrush PanelSheen;
	FSlateBrush CardSheen;
	FSlateRoundedBoxBrush SectionBrush = ArenaGlass::Surface(0.045f, 18.0f, 0.10f);
	FSlateRoundedBoxBrush HeroBrush = FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.30f, 0.55f, 0.35f), 20.0f, FLinearColor(0.55f, 0.85f, 1.0f, 0.55f), 1.2f);
	FSlateRoundedBoxBrush ChipBrush = FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.08f, 0.16f, 0.70f), 12.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.16f), 1.0f);
	FSlateRoundedBoxBrush PillBrush = ArenaGlass::Surface(0.10f, 10.0f, 0.25f);
	FSlateRoundedBoxBrush EquippedPillBrush = ArenaGlass::Surface(0.24f, 10.0f, 0.75f, ArenaGlass::Mint);
	FSlateRoundedBoxBrush DotBrush = FSlateRoundedBoxBrush(FLinearColor::White, 5.0f);
	FSlateRoundedBoxBrush LightBrush = FSlateRoundedBoxBrush(FLinearColor::White);
	FProgressBarStyle BarStyle;
	FButtonStyle PrimaryButtonStyle;
	FButtonStyle DangerButtonStyle;
	FButtonStyle CloseButtonStyle;
	FButtonStyle CardButtonStyle;
	FButtonStyle EquippedSlotStyle;
	FButtonStyle SelectedSlotStyle;
	FButtonStyle EmptyButtonStyle;
	TArray<FString> MaterialNames;
	TMap<FString, TWeakPtr<SWidget>> WeaponSlotWidgets;
	TMap<FString, FSlotAnimation> SlotAnimations;
	TMap<TWeakObjectPtr<UTexture2D>, TSharedPtr<FSlateBrush>> IconBrushCache;
	TSharedPtr<SWidget> Backdrop;
	TSharedPtr<SWidget> PanelRoot;
	TSharedPtr<SHorizontalBox> MaterialsRow;
	TSharedPtr<SUniformGridPanel> WeaponsGrid;
	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<STextBlock> CountText;
	TSharedPtr<STextBlock> HeroNameText;
	TSharedPtr<SImage> HeroIcon;
	TSharedPtr<STextBlock> AmmoDescriptionText;
	TSharedPtr<STextBlock> AmmoCountText;
	float AmmoPercent = 0.0f;
	float Reveal = 0.0f;
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

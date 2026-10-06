#include "ArenaEmoteWheel.h"
#include "ArenaGlassStyle.h"
#include "ArenaGlassButton.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ArenaEmoteWheel"

namespace
{
	constexpr float DiscSize = 580.0f;
	constexpr float SlotSize = 100.0f;
	constexpr float SlotRadius = 196.0f;

	FSlateFontInfo EmoteFont(UFontFace* Face, int32 Size)
	{
		if (Face == nullptr)
		{
			return FCoreStyle::GetDefaultFontStyle("Bold", Size);
		}
		static TMap<TWeakObjectPtr<UFontFace>, TSharedPtr<FCompositeFont>> Cache;
		TSharedPtr<FCompositeFont>& Font = Cache.FindOrAdd(Face);
		if (!Font.IsValid())
		{
			Font = MakeShared<FStandaloneCompositeFont>();
			FTypefaceEntry Entry(TEXT("Regular"));
			Entry.Font = FFontData(Face);
			Font->DefaultTypeface.Fonts.Add(Entry);
		}
		return FSlateFontInfo(Font, Size);
	}
}

void UArenaEmoteSlotHandler::HandleClicked() { if (UArenaEmoteWheel* Wheel = Owner.Get()) { Wheel->PlayEmote(Index); } }
void UArenaEmoteSlotHandler::HandleHovered() { if (UArenaEmoteWheel* Wheel = Owner.Get()) { Wheel->HoverEmote(Index); } }
void UArenaEmoteSlotHandler::HandleUnhovered() { if (UArenaEmoteWheel* Wheel = Owner.Get()) { Wheel->HoverEmote(INDEX_NONE); } }

UArenaEmoteWheel::UArenaEmoteWheel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
	static ConstructorHelpers::FObjectFinder<UFontFace> BodyFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankSmall_Black.FF_BurbankSmall_Black"));
	BodyFontFace = BodyFinder.Object;
}

UArenaEmoteWheel* UArenaEmoteWheel::Show(APlayerController* PC, UFortnitePortingCharacterComponent* InCosmetics, UUserWidget* InReturnFocus)
{
	if (PC == nullptr || InCosmetics == nullptr)
	{
		return nullptr;
	}
	UArenaEmoteWheel* Wheel = CreateWidget<UArenaEmoteWheel>(PC, UArenaEmoteWheel::StaticClass());
	if (Wheel == nullptr)
	{
		return nullptr;
	}
	Wheel->Cosmetics = InCosmetics;
	Wheel->ReturnFocus = InReturnFocus;
	Wheel->AddToViewport(60);

	if (InReturnFocus)
	{
		// Over the lobby the UI already has the input and the cursor
		return Wheel;
	}

	// Keep gameplay input active while the cursor picks an emote.
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetWidgetToFocus(Wheel->TakeWidget());
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	PC->SetIgnoreMoveInput(false);
	PC->SetIgnoreLookInput(false);
	return Wheel;
}

void UArenaEmoteWheel::Hide()
{
	RemoveFromParent();

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (UUserWidget* Lobby = ReturnFocus.Get())
		{
			FInputModeUIOnly Mode;
			Mode.SetWidgetToFocus(Lobby->TakeWidget());
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PC->SetInputMode(Mode);
			PC->SetShowMouseCursor(true);
		}
		else
		{
			const TWeakObjectPtr<APlayerController> WeakPlayer(PC);
			auto RestoreGameInput = [WeakPlayer]()
			{
				if (APlayerController* Player = WeakPlayer.Get())
				{
					FInputModeGameOnly Mode;
					Mode.SetConsumeCaptureMouseDown(false);
					Player->SetInputMode(Mode);
					Player->SetShowMouseCursor(false);
					Player->SetIgnoreMoveInput(false);
					Player->SetIgnoreLookInput(false);
					UWidgetBlueprintLibrary::SetFocusToGameViewport();
				}
			};
			RestoreGameInput();
			if (UWorld* World = PC->GetWorld())
			{
				World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(RestoreGameInput));
			}
		}
	}
}

void UArenaEmoteWheel::PlayEmote(int32 Index)
{
	if (Cosmetics && Cosmetics->Emotes.IsValidIndex(Index) && Cosmetics->Emotes[Index])
	{
		Cosmetics->RequestEmote(Cosmetics->Emotes[Index]);
	}
	Hide();
}

void UArenaEmoteWheel::HoverEmote(int32 Index)
{
	if (NameText == nullptr)
	{
		return;
	}
	if (Cosmetics && Cosmetics->Emotes.IsValidIndex(Index) && Cosmetics->Emotes[Index])
	{
		const FText Name = Cosmetics->Emotes[Index]->DisplayName;
		NameText->SetText(Name.IsEmpty() ? FText::FromString(Cosmetics->Emotes[Index]->GetName()) : Name);
		NameText->SetColorAndOpacity(FSlateColor(ArenaGlass::Ink));
	}
	else
	{
		NameText->SetText(LOCTEXT("PickEmote", "Elige un emote"));
		NameText->SetColorAndOpacity(FSlateColor(ArenaGlass::Dim));
	}
}

TSharedRef<SWidget> UArenaEmoteWheel::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		Build();
	}
	return Super::RebuildWidget();
}

void UArenaEmoteWheel::Build()
{
	using namespace ArenaGlass;
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("EmoteRoot"));
	WidgetTree->RootWidget = Root;

	auto Place = [Root](UWidget* Widget, const FVector2D& Offset, const FVector2D& Size)
	{
		UCanvasPanelSlot* CanvasSlot = Root->AddChildToCanvas(Widget);
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetPosition(Offset);
		CanvasSlot->SetSize(Size);
		return CanvasSlot;
	};

	// Faint dark over the game so the glass reads
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>();
	Backdrop->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.016f, 0.024f, 0.063f, 0.30f), 0.0f));
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Backdrop);
	DimSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	DimSlot->SetOffsets(FMargin(0.0f));

	// Disc of glass: colored light, the blur of the game behind it, the translucent surface with its rim
	auto AddBlob = [&](const FLinearColor& Color, const FVector2D& Offset, float Size)
	{
		UBorder* Blob = WidgetTree->ConstructWidget<UBorder>();
		Blob->SetBrush(FSlateRoundedBoxBrush(Color, Size * 0.5f));
		Blob->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Blob, Offset, FVector2D(Size, Size));
	};

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(32.0f);
	Blur->SetApplyAlphaToBlur(false);
	const float Corner = DiscSize * 0.5f; // a full circle: the blur needs the whole half-size, the 22 -> 11 rule is only for small corners
	Blur->SetCornerRadius(FVector4(Corner, Corner, Corner, Corner));
	UBorder* Disc = WidgetTree->ConstructWidget<UBorder>();
	Disc->SetBrush(Surface(0.12f, DiscSize * 0.5f, 0.45f, FLinearColor::White, 1.4f));
	Blur->SetContent(Layered(WidgetTree, Disc, DiscSize * 0.5f, FLinearColor::White, 0.10f, 0.08f));
	Place(Blur, FVector2D::ZeroVector, FVector2D(DiscSize, DiscSize))->SetZOrder(1);

	// Thin inner ring that frames the center
	{
		UBorder* Ring = WidgetTree->ConstructWidget<UBorder>();
		Ring->SetBrush(Surface(0.0f, 130.0f, 0.22f));
		Ring->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Ring, FVector2D::ZeroVector, FVector2D(260.0f, 260.0f))->SetZOrder(2);
	}

	// Center: what the cursor is on
	UVerticalBox* Center = WidgetTree->ConstructWidget<UVerticalBox>();
	Center->SetVisibility(ESlateVisibility::HitTestInvisible);
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(LOCTEXT("EmotesTitle", "EMOTES"));
	Title->SetFont(EmoteFont(BodyFontFace, 12));
	Title->SetColorAndOpacity(FSlateColor(Mint));
	Title->SetJustification(ETextJustify::Center);
	Center->AddChildToVerticalBox(Title)->SetHorizontalAlignment(HAlign_Center);
	NameText = WidgetTree->ConstructWidget<UTextBlock>();
	NameText->SetFont(EmoteFont(BodyFontFace, 20));
	NameText->SetJustification(ETextJustify::Center);
	Center->AddChildToVerticalBox(NameText)->SetHorizontalAlignment(HAlign_Center);
	UTextBlock* Hint = WidgetTree->ConstructWidget<UTextBlock>();
	Hint->SetText(LOCTEXT("EmoteHint", "Haz clic para bailar · B / ESC para cerrar"));
	Hint->SetFont(EmoteFont(BodyFontFace, 10));
	Hint->SetColorAndOpacity(FSlateColor(Dim));
	Hint->SetJustification(ETextJustify::Center);
	Center->AddChildToVerticalBox(Hint)->SetHorizontalAlignment(HAlign_Center);
	Place(Center, FVector2D::ZeroVector, FVector2D(200.0f, 90.0f))->SetZOrder(3);

	const int32 Count = Cosmetics ? Cosmetics->Emotes.Num() : 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UFortnitePortingEmoteData* Emote = Cosmetics->Emotes[Index];
		if (Emote == nullptr)
		{
			continue;
		}
		const float Angle = FMath::DegreesToRadians(-90.0f + Index * 360.0f / Count);
		const FVector2D Offset(FMath::Cos(Angle) * SlotRadius, FMath::Sin(Angle) * SlotRadius);

		UButton* Button = WidgetTree->ConstructWidget<UArenaGlassButton>();
		ArenaGlass::Style(Button, ButtonStyle(0.12f, SlotSize, 0.45f));

		// Round crop of the emote icon, like the avatars
		UImage* Icon = WidgetTree->ConstructWidget<UImage>();
		FSlateBrush Brush;
		Brush.SetResourceObject(Emote->Icon);
		Brush.ImageSize = FVector2D(SlotSize - 12.0f, SlotSize - 12.0f);
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.TintColor = Emote->Icon ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.15f));
		Icon->SetBrush(Brush);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Icon)))
		{
			ButtonSlot->SetPadding(FMargin(6.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}

		UArenaEmoteSlotHandler* Handler = NewObject<UArenaEmoteSlotHandler>(this);
		Handler->Index = Index;
		Handler->Owner = this;
		Handlers.Add(Handler);
		Button->OnClicked.AddDynamic(Handler, &UArenaEmoteSlotHandler::HandleClicked);
		Button->OnHovered.AddDynamic(Handler, &UArenaEmoteSlotHandler::HandleHovered);
		Button->OnUnhovered.AddDynamic(Handler, &UArenaEmoteSlotHandler::HandleUnhovered);
		Place(Button, Offset, FVector2D(SlotSize, SlotSize))->SetZOrder(2);
	}

	if (Count == 0)
	{
		NameText->SetText(LOCTEXT("NoEmotes", "Sin emotes"));
		NameText->SetColorAndOpacity(FSlateColor(Dim));
	}
	else
	{
		HoverEmote(INDEX_NONE);
	}
}

#undef LOCTEXT_NAMESPACE

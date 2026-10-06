#include "ArenaLobbyWidget.h"
#include "ArenaPlayerController.h"
#include "FortnitePortingStyleData.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "ArenaSettingsWidget.h"
#include "ArenaGlassStyle.h"
#include "ArenaEmoteWheel.h"
#include "ArenaUISounds.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "FortnitePortingCharacterComponent.h"
#include "GameFramework/Pawn.h"
#include "ArenaGlassButton.h"
#include "ArenaFriendsSubsystem.h"
#include "ArenaSessionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Components/EditableTextBox.h"
#include "Engine/GameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/BackgroundBlur.h"
#include "Components/BackgroundBlurSlot.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/WrapBoxSlot.h"
#include "Fonts/CompositeFont.h"
#include "ImageUtils.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ArenaLobby"

namespace ArenaLobbyStyle
{
	// Fortnite lobby palette
	const FLinearColor White(1.0f, 1.0f, 1.0f);
	const FLinearColor Muted(0.62f, 0.72f, 0.88f);
	const FLinearColor Yellow(1.0f, 0.82f, 0.02f);
	const FLinearColor YellowHover(1.0f, 0.9f, 0.3f);
	const FLinearColor YellowPressed(0.85f, 0.66f, 0.0f);
	const FLinearColor Navy(0.004f, 0.018f, 0.075f);
	const FLinearColor Blue(0.02f, 0.13f, 0.55f);
	const FLinearColor Panel(0.01f, 0.04f, 0.16f, 0.82f);
	const FLinearColor Cyan(0.2f, 0.85f, 1.0f);
	const FLinearColor Sky(0.45f, 0.72f, 1.0f);
	const FLinearColor Tile(0.09f, 0.3f, 0.95f);
	const FLinearColor TileHover(0.16f, 0.42f, 1.0f);
	const FLinearColor Clear(0.0f, 0.0f, 0.0f, 0.0f);

	// Fortnite headings are a slanted Burbank
	const FVector2D Italic(-9.0f, 0.0f);

	enum { TabPlay = 0, TabLocker = 1 };
}

namespace
{
	UTexture2D* LoadArena1v1Artwork(bool bModeCard)
	{
		const TCHAR* AssetPath = bModeCard
			? TEXT("/Game/MapIcons/Lvl_Arena1v1_CardRounded.Lvl_Arena1v1_CardRounded")
			: TEXT("/Game/MapIcons/Lvl_Arena1v1_Rounded.Lvl_Arena1v1_Rounded");
		UTexture2D* Artwork = LoadObject<UTexture2D>(nullptr, AssetPath);
		if (!Artwork)
		{
			UE_LOG(LogTemp, Warning, TEXT("Could not load rounded 1v1 map artwork from %s"), AssetPath);
		}
		return Artwork;
	}

	UTexture2D* LoadPawnSkinIcon(const APawn* Pawn)
	{
		if (Pawn == nullptr)
		{
			return nullptr;
		}

		FString PackagePath;
		FString AssetName;
		if (!Pawn->GetClass()->GetPathName().Split(TEXT("."), &PackagePath, &AssetName))
		{
			return nullptr;
		}

		FString SkinName = AssetName;
		SkinName.RemoveFromStart(TEXT("BP_"));
		SkinName.RemoveFromEnd(TEXT("_C"));
		if (SkinName.IsEmpty())
		{
			return nullptr;
		}

		FString SkinFolder = FPaths::GetPath(FPaths::GetPath(PackagePath));
		const int32 StyleMarker = SkinName.Find(TEXT("_Style_"));
		if (StyleMarker != INDEX_NONE)
		{
			SkinName.LeftInline(StyleMarker);
			SkinFolder = FPaths::GetPath(FPaths::GetPath(SkinFolder));
		}

		const FString IconName = FString::Printf(TEXT("T_%s_Icon"), *SkinName);
		return LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/Icon/%s.%s"), *SkinFolder, *IconName, *IconName));
	}
}

void UArenaSkinTileHandler::HandleClicked()
{
	if (UArenaLobbyWidget* Widget = Owner.Get())
	{
		if (bMap)
		{
			Widget->ClickMap(Index);
		}
		else
		{
			Widget->ClickSkin(Index);
		}
	}
}

void UArenaSkinTileHandler::HandleHovered()
{
	if (UArenaLobbyWidget* Widget = Owner.Get())
	{
		Widget->HoverSkinTile(Index);
	}
}

void UArenaSkinTileHandler::HandleUnhovered()
{
	if (UArenaLobbyWidget* Widget = Owner.Get())
	{
		Widget->UnhoverSkinTile(Index);
	}
}

void UArenaFriendActionHandler::HandleClicked()
{
	if (UArenaLobbyWidget* Widget = Owner.Get())
	{
		switch (Action)
		{
		case 0: Widget->AcceptFriendRequest(Index); break;
		case 1: Widget->DeclineFriendRequest(Index); break;
		case 2: Widget->InviteFriendToLobby(Index); break;
		case 3: Widget->AcceptLobbyInvite(Index); break;
		case 4: Widget->JoinFriendLobby(Index); break;
		case 5: Widget->SelectChatFriend(Index); break;
		default: break;
		}
	}
}

UArenaLobbyWidget::UArenaLobbyWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PlayerName = LOCTEXT("DefaultName", "JUGADOR");
	ModeName = LOCTEXT("DefaultMode", "ARENA");
	ModeDescription = LOCTEXT("DefaultModeDesc", "SOLO  ·  PRÁCTICA LIBRE");
	SetIsFocusable(true);

	static ConstructorHelpers::FObjectFinder<UFontFace> HeadingFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankBigCondensed_Black.FF_BurbankBigCondensed_Black"));
	static ConstructorHelpers::FObjectFinder<UFontFace> BodyFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankSmall_Black.FF_BurbankSmall_Black"));
	static ConstructorHelpers::FObjectFinder<UFontFace> BoldFinder(TEXT("/Game/UI/Fortnite/Fonts/FF_BurbankSmall_Bold.FF_BurbankSmall_Bold"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> CurrencyFinder(TEXT("/Game/UI/Fortnite/Textures/T_UI_VBucks.T_UI_VBucks"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> GradientFinder(TEXT("/Game/UI/Fortnite/Textures/T_UI_GradientV.T_UI_GradientV"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> VignetteFinder(TEXT("/Game/UI/Fortnite/Textures/T_UI_Vignette.T_UI_Vignette"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> GlowFinder(TEXT("/Game/UI/Fortnite/Textures/T_UI_Glow.T_UI_Glow"));
	HeadingFontFace = HeadingFinder.Object;
	BodyFontFace = BodyFinder.Object;
	BoldFontFace = BoldFinder.Object;
	CurrencyIcon = CurrencyFinder.Object;
	GradientTexture = GradientFinder.Object;
	VignetteTexture = VignetteFinder.Object;
	GlowTexture = GlowFinder.Object;
}

TSharedRef<SWidget> UArenaLobbyWidget::RebuildWidget()
{
	if (WidgetTree && WidgetTree->RootWidget == nullptr)
	{
		if (bMatchSocialOverlay)
		{
			BuildMatchSocialOverlay();
		}
		else
		{
			BuildLayout();
			BuildSidePanel(Cast<UCanvasPanel>(WidgetTree->RootWidget));
		}
	}
	return Super::RebuildWidget();
}

FSlateFontInfo UArenaLobbyWidget::MakeFont(UFontFace* Face, int32 Size) const
{
	if (Face == nullptr)
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	}

	// Font faces are used straight from a composite font, no UFont asset needed
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

UTextBlock* UArenaLobbyWidget::MakeText(const FText& Text, UFontFace* Face, int32 Size, const FLinearColor& Color, bool bShadow)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
	Block->SetText(Text);
	Block->SetFont(MakeFont(Face, Size));
	Block->SetColorAndOpacity(FSlateColor(Color));
	if (bShadow)
	{
		Block->SetShadowOffset(FVector2D(0.0f, 2.0f));
		Block->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.05f, 0.6f));
	}
	Block->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Block;
}

UButton* UArenaLobbyWidget::MakeButton(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed, float Radius)
{
	UButton* Button = WidgetTree->ConstructWidget<UArenaGlassButton>();
	FButtonStyle Style;
	Style.SetNormal(FSlateRoundedBoxBrush(Normal, Radius));
	Style.SetHovered(FSlateRoundedBoxBrush(Hovered, Radius));
	Style.SetPressed(FSlateRoundedBoxBrush(Pressed, Radius));
	Style.SetDisabled(FSlateRoundedBoxBrush(Normal * 0.5f, Radius));
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	Button->SetStyle(Style);
	Button->OnHovered.AddDynamic(this, &UArenaLobbyWidget::HandleButtonHovered);
	return Button;
}

void UArenaLobbyWidget::AddToCanvas(UCanvasPanel* Canvas, UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FVector2D& Position, const FVector2D& Size, bool bAutoSize)
{
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Widget);
	CanvasSlot->SetAnchors(Anchors);
	CanvasSlot->SetAlignment(Alignment);
	CanvasSlot->SetPosition(Position);
	CanvasSlot->SetAutoSize(bAutoSize);
	if (!bAutoSize)
	{
		CanvasSlot->SetSize(Size);
	}
}

UWidget* UArenaLobbyWidget::WrapGlass(UWidget* Content, const FMargin& InnerPadding, float Radius, float Fill, float Rim, const FLinearColor& Tint)
{
	// The blur of what is behind, clipped to the same rounded shape as the glass surface drawn over it
	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(30.0f);
	Blur->SetApplyAlphaToBlur(false);
	const float Corner = ArenaGlass::BlurCorner(Radius);
	Blur->SetCornerRadius(FVector4(Corner, Corner, Corner, Corner));
	UBorder* Surface = WidgetTree->ConstructWidget<UBorder>();
	Surface->SetBrush(ArenaGlass::Surface(Fill, Radius, Rim, Tint, 1.3f));
	Surface->SetPadding(InnerPadding);
	Surface->SetContent(Content);
	Blur->SetContent(Surface);
	return Blur;
}

void UArenaLobbyWidget::BuildLayout()
{
	using namespace ArenaLobbyStyle;
	using namespace ArenaGlass;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LobbyRoot"));
	WidgetTree->RootWidget = Root;

	auto MakeImage = [this](UTexture2D* Texture, const FLinearColor& Tint)
	{
		UImage* Image = WidgetTree->ConstructWidget<UImage>();
		if (Texture)
		{
			Image->SetBrushFromTexture(Texture);
		}
		Image->SetColorAndOpacity(Tint);
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Image;
	};

	// --- Fortnite lobby blue tint over the scene ---
	AddToCanvas(Root, MakeImage(VignetteTexture, FLinearColor(Blue.R, Blue.G, Blue.B, 0.85f)), FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);
	AddToCanvas(Root, MakeImage(GlowTexture, FLinearColor(0.25f, 0.55f, 1.0f, 0.18f)), FAnchors(0.5f, 0.55f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, FVector2D(1100.0f, 1100.0f));

	UImage* TopShade = MakeImage(GradientTexture, FLinearColor(Navy.R, Navy.G, Navy.B, 0.9f));
	TopShade->SetRenderTransformAngle(180.0f);
	AddToCanvas(Root, TopShade, FAnchors(0, 0, 1, 0), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D(0.0f, 170.0f));
	// Short bottom fade so the feet stay visible
	AddToCanvas(Root, MakeImage(GradientTexture, FLinearColor(Navy.R, Navy.G, Navy.B, 0.7f)), FAnchors(0, 1, 1, 1), FVector2D(0.0f, 1.0f), FVector2D::ZeroVector, FVector2D(0.0f, 190.0f));

	// --- Top navigation (shared by every page) ---
	UHorizontalBox* Nav = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddToCanvas(Root, Nav, FAnchors(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 20.0f), FVector2D::ZeroVector, true);
	TopBarWidgets.Add(Nav);
	{
		// The sections live in a glass pill, the selected one is a lit pane inside it
		UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>();
		const FText Tabs[] = {
			LOCTEXT("TabPlay", "JUGAR"), LOCTEXT("TabLocker", "TAQUILLA"), LOCTEXT("TabShop", "TIENDA")
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tabs); ++Index)
		{
			UButton* Tab = MakeButton(Clear, Clear, Clear, 14.0f);
			ArenaGlass::Style(Tab, ArenaGlass::ButtonStyle(0.0f, 18.0f, 0.0f));
			UTextBlock* Label = MakeText(Tabs[Index], BodyFontFace, 13, Dim, false);
			if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Tab->SetContent(Label)))
			{
				ButtonSlot->SetPadding(FMargin(15.0f, 7.0f));
			}
			if (Index == TabPlay)
			{
				Tab->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandlePlayTab);
			}
			else if (Index == TabLocker)
			{
				Tab->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleLockerTab);
			}
			else
			{
				Tab->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleComingSoon);
			}
			TabLabels.Add(Label);
			TabButtons.Add(Tab);
			UHorizontalBoxSlot* TabSlot = TabRow->AddChildToHorizontalBox(Tab);
			TabSlot->SetVerticalAlignment(VAlign_Center);
			TabSlot->SetPadding(FMargin(1.0f, 0.0f));
		}
		UBorder* TabTrack = WidgetTree->ConstructWidget<UBorder>();
		TabTrack->SetBrush(ArenaGlass::Surface(0.24f, 22.0f, 0.22f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));
		TabTrack->SetPadding(FMargin(4.0f));
		TabTrack->SetContent(TabRow);
		Nav->AddChildToHorizontalBox(TabTrack)->SetVerticalAlignment(VAlign_Center);
	}

	// Currency and level, top right
	UHorizontalBox* Wallet = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddToCanvas(Root, Wallet, FAnchors(1, 0), FVector2D(1.0f, 0.0f), FVector2D(-40.0f, 22.0f), FVector2D::ZeroVector, true);
	TopBarWidgets.Add(Wallet);
	{
		// Round avatar of the equipped outfit: opens the social side panel
		UButton* AvatarButton = MakeButton(Clear, Clear, Clear, 20.0f);
		ArenaGlass::Style(AvatarButton, ArenaGlass::ButtonStyle(0.18f, 20.0f, 0.55f));
		AvatarButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleAvatarClicked);
		USizeBox* AvatarBox = WidgetTree->ConstructWidget<USizeBox>();
		AvatarBox->SetWidthOverride(38.0f);
		AvatarBox->SetHeightOverride(38.0f);
		if (UButtonSlot* AvatarSlot = Cast<UButtonSlot>(AvatarButton->SetContent(MakeAvatar(32.0f))))
		{
			AvatarSlot->SetPadding(FMargin(2.0f));
			AvatarSlot->SetHorizontalAlignment(HAlign_Center);
			AvatarSlot->SetVerticalAlignment(VAlign_Center);
		}
		AvatarBox->SetContent(AvatarButton);
		UHorizontalBoxSlot* AvatarButtonSlot = Wallet->AddChildToHorizontalBox(AvatarBox);
		AvatarButtonSlot->SetVerticalAlignment(VAlign_Center);
	}

	// ===================== LOBBY PAGE =====================
	LobbyPage = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LobbyPage"));
	AddToCanvas(Root, LobbyPage, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);

	// Name plate over the character
	UVerticalBox* Plate = WidgetTree->ConstructWidget<UVerticalBox>();
	AddToCanvas(LobbyPage, Plate, FAnchors(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 92.0f), FVector2D::ZeroVector, true);
	{
		Plate->AddChildToVerticalBox(MakeText(PlayerName, BodyFontFace, 30, Ink))->SetHorizontalAlignment(HAlign_Center);
	}

	// Party invite slots beside the character
	for (const float Side : { -1.0f, 1.0f })
	{
		UVerticalBox* SlotBox = WidgetTree->ConstructWidget<UVerticalBox>();
		UButton* Invite = MakeButton(Clear, Clear, Clear, 30.0f);
		ArenaGlass::Style(Invite, ArenaGlass::ButtonStyle(0.10f, 30.0f, 0.32f));
		UTextBlock* Plus = MakeText(FText::FromString(TEXT("+")), BodyFontFace, 30, Ink, false);
		Plus->SetJustification(ETextJustify::Center);
		if (UButtonSlot* PlusSlot = Cast<UButtonSlot>(Invite->SetContent(Plus)))
		{
			PlusSlot->SetHorizontalAlignment(HAlign_Center);
			PlusSlot->SetVerticalAlignment(VAlign_Center);
		}
		Invite->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleComingSoon);
		USizeBox* InviteSize = WidgetTree->ConstructWidget<USizeBox>();
		InviteSize->SetWidthOverride(58.0f);
		InviteSize->SetHeightOverride(58.0f);
		InviteSize->SetContent(Invite);
		SlotBox->AddChildToVerticalBox(InviteSize)->SetHorizontalAlignment(HAlign_Center);
		SlotBox->AddChildToVerticalBox(MakeText(LOCTEXT("Invite", "INVITAR"), BodyFontFace, 11, Dim, false))->SetHorizontalAlignment(HAlign_Center);
		AddToCanvas(LobbyPage, SlotBox, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D(Side * 330.0f, 40.0f), FVector2D::ZeroVector, true);
	}

	// "Abandonar grupo": only visible while the player is in somebody's group (a session)
	{
		LeaveGroupButton = MakeButton(Clear, Clear, Clear, 18.0f);
		ArenaGlass::Style(LeaveGroupButton, ArenaGlass::ButtonStyle(0.28f, 18.0f, 0.62f, ArenaGlass::Coral));
		UTextBlock* LeaveLabel = MakeText(LOCTEXT("LeaveGroup", "ABANDONAR GRUPO"), BodyFontFace, 13, Ink, false);
		LeaveLabel->SetJustification(ETextJustify::Center);
		if (UButtonSlot* LeaveSlot = Cast<UButtonSlot>(LeaveGroupButton->SetContent(LeaveLabel)))
		{
			LeaveSlot->SetPadding(FMargin(18.0f, 8.0f));
			LeaveSlot->SetHorizontalAlignment(HAlign_Center);
			LeaveSlot->SetVerticalAlignment(VAlign_Center);
		}
		LeaveGroupButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleLeaveGroup);
		LeaveGroupButton->SetVisibility(ESlateVisibility::Collapsed);
		AddToCanvas(LobbyPage, LeaveGroupButton, FAnchors(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 140.0f), FVector2D::ZeroVector, true);
	}

	// Mode card + PLAY, bottom right
	UVerticalBox* PlayColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	AddToCanvas(LobbyPage, PlayColumn, FAnchors(1, 1), FVector2D(1.0f, 1.0f), FVector2D(-40.0f, -36.0f), FVector2D::ZeroVector, true);
	{
		UVerticalBox* CardContent = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* CardHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
		UHorizontalBoxSlot* SmallSlot = CardHeader->AddChildToHorizontalBox(MakeText(LOCTEXT("ModeSmall", "MODO SELECCIONADO"), BodyFontFace, 10, ArenaGlass::Mint, false));
		SmallSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SmallSlot->SetVerticalAlignment(VAlign_Center);
		CardContent->AddChildToVerticalBox(CardHeader);
		ModeNameText = MakeText(ModeName, BodyFontFace, 28, Ink);
		CardContent->AddChildToVerticalBox(ModeNameText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		CardContent->AddChildToVerticalBox(MakeText(ModeDescription, BodyFontFace, 11, Dim, false));

		// The whole card is the button: hover lights the glass, click opens the map selector
		UButton* ModeCard = MakeButton(Clear, Clear, Clear, 20.0f);
		FButtonStyle CardStyle = ArenaGlass::ButtonStyle(0.0f, 20.0f, 0.0f);
		CardStyle.SetHovered(ArenaGlass::Surface(0.12f, 20.0f, 0.55f));
		CardStyle.SetPressed(ArenaGlass::Surface(0.06f, 20.0f, 0.40f));
		ModeCard->SetStyle(CardStyle);
		UOverlay* ModeCardContent = WidgetTree->ConstructWidget<UOverlay>();
		ModeArtworkImage = WidgetTree->ConstructWidget<UImage>();
		if (UTexture2D* Artwork = LoadArena1v1Artwork(true))
		{
			ModeArtworkImage->SetBrushFromTexture(Artwork);
			ModeArtworkImage->SetColorAndOpacity(FLinearColor::White);
		}
		ModeArtworkImage->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* ModeArtworkSlot = ModeCardContent->AddChildToOverlay(ModeArtworkImage);
		ModeArtworkSlot->SetHorizontalAlignment(HAlign_Fill);
		ModeArtworkSlot->SetVerticalAlignment(VAlign_Fill);
		UOverlaySlot* CardContentSlot = ModeCardContent->AddChildToOverlay(CardContent);
		CardContentSlot->SetHorizontalAlignment(HAlign_Fill);
		CardContentSlot->SetVerticalAlignment(VAlign_Fill);
		CardContentSlot->SetPadding(FMargin(18.0f, 12.0f, 18.0f, 14.0f));
		ModeCardHoverFrame = WidgetTree->ConstructWidget<UBorder>();
		ModeCardHoverFrame->SetBrush(ArenaGlass::Surface(0.0f, 20.0f, 1.0f, ArenaGlass::Ice, 2.0f));
		ModeCardHoverFrame->SetPadding(FMargin(0.0f));
		ModeCardHoverFrame->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* HoverFrameSlot = ModeCardContent->AddChildToOverlay(ModeCardHoverFrame);
		HoverFrameSlot->SetHorizontalAlignment(HAlign_Fill);
		HoverFrameSlot->SetVerticalAlignment(VAlign_Fill);
		if (UButtonSlot* CardSlot = Cast<UButtonSlot>(ModeCard->SetContent(ModeCardContent)))
		{
			CardSlot->SetPadding(FMargin(0.0f));
			CardSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		ModeCard->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleModeCardClicked);
		ModeCard->OnHovered.AddDynamic(this, &UArenaLobbyWidget::HandleModeCardHovered);
		ModeCard->OnUnhovered.AddDynamic(this, &UArenaLobbyWidget::HandleModeCardUnhovered);
		USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>();
		CardSize->SetWidthOverride(330.0f);
		CardSize->SetContent(WrapGlass(ModeCard, FMargin(0.0f), 20.0f, 0.10f, 0.38f));
		PlayColumn->AddChildToVerticalBox(CardSize)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

		UButton* Play = MakeButton(Clear, Clear, Clear, 18.0f);
		ArenaGlass::Style(Play, ArenaGlass::ButtonStyle(0.42f, 20.0f, 0.75f, FLinearColor(0.32f, 0.62f, 1.0f)));
		UVerticalBox* PlayContent = WidgetTree->ConstructWidget<UVerticalBox>();
		PlayContent->AddChildToVerticalBox(MakeText(LOCTEXT("Play", "JUGAR"), BodyFontFace, 24, Ink, false))->SetHorizontalAlignment(HAlign_Center);
		PlayContent->AddChildToVerticalBox(MakeText(LOCTEXT("PlayHint", "ENTER"), BodyFontFace, 10, Dim, false))->SetHorizontalAlignment(HAlign_Center);
		if (UButtonSlot* PlaySlot = Cast<UButtonSlot>(Play->SetContent(PlayContent)))
		{
			PlaySlot->SetHorizontalAlignment(HAlign_Center);
			PlaySlot->SetVerticalAlignment(VAlign_Center);
		}
		Play->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandlePlayClicked);
		USizeBox* PlaySize = WidgetTree->ConstructWidget<USizeBox>();
		PlaySize->SetWidthOverride(330.0f);
		PlaySize->SetHeightOverride(58.0f);
		PlaySize->SetContent(Play);

		// "Detener baile" sits to the left of PLAY and only exists while a dance is playing
		UButton* StopDance = MakeButton(Clear, Clear, Clear, 20.0f);
		ArenaGlass::Style(StopDance, ArenaGlass::ButtonStyle(0.26f, 20.0f, 0.60f, ArenaGlass::Coral));
		UTextBlock* StopLabel = MakeText(LOCTEXT("StopDance", "DETENER BAILE"), BodyFontFace, 13, Ink, false);
		StopLabel->SetJustification(ETextJustify::Center);
		if (UButtonSlot* StopSlot = Cast<UButtonSlot>(StopDance->SetContent(StopLabel)))
		{
			StopSlot->SetPadding(FMargin(16.0f, 0.0f, 16.0f, 0.0f));
			StopSlot->SetHorizontalAlignment(HAlign_Center);
			StopSlot->SetVerticalAlignment(VAlign_Center);
		}
		StopDance->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleStopDance);
		USizeBox* StopSize = WidgetTree->ConstructWidget<USizeBox>();
		StopSize->SetHeightOverride(58.0f);
		StopSize->SetContent(StopDance);
		StopSize->SetVisibility(ESlateVisibility::Collapsed);
		StopDanceButton = StopSize;

		PlayColumn->AddChildToVerticalBox(PlaySize);

		// Out of the column: showing it must not move anything else. It fades in and out by opacity.
		StopSize->SetWidthOverride(160.0f);
		StopSize->SetRenderOpacity(0.0f);
		AddToCanvas(LobbyPage, StopSize, FAnchors(1, 1), FVector2D(1.0f, 1.0f), FVector2D(-40.0f - 330.0f - 10.0f, -36.0f), FVector2D::ZeroVector, true);
	}

	// ===================== LOCKER PAGE =====================
	LockerPage = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LockerPage"));
	AddToCanvas(Root, LockerPage, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);
	BuildLockerPage(LockerPage);

	// Toast for sections that are not implemented yet
	ToastText = MakeText(FText::GetEmpty(), BodyFontFace, 18, ArenaGlass::Amber);
	ToastText->SetJustification(ETextJustify::Center);
	ToastText->SetRenderOpacity(0.0f);
	AddToCanvas(Root, ToastText, FAnchors(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 96.0f), FVector2D::ZeroVector, true);

	if (bLockerOpen)
	{
		ShowLocker();
	}
	else
	{
		ShowLobby();
	}

	// ── Server browser overlay (starts hidden) ──
	BuildMapPage(Root);
}

void UArenaLobbyWidget::BuildLockerPage(UCanvasPanel* Page)
{
	using namespace ArenaLobbyStyle;
	using namespace ArenaGlass;

	// Right column: a glass panel with title, category tabs and the outfit grid
	USizeBox* ColumnSize = WidgetTree->ConstructWidget<USizeBox>();
	ColumnSize->SetWidthOverride(LockerColumns * 146.0f + 24.0f);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	ColumnSize->SetContent(WrapGlass(Column, FMargin(16.0f, 12.0f), 22.0f, 0.08f, 0.34f));
	{
		// Anchored to the right edge, stretched vertically between the nav bar and the back button
		UCanvasPanelSlot* ColumnSlot = Page->AddChildToCanvas(ColumnSize);
		ColumnSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
		ColumnSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		ColumnSlot->SetOffsets(FMargin(-60.0f, 84.0f, LockerColumns * 146.0f + 24.0f, 96.0f));
	}
	{
		UTextBlock* Title = MakeText(LOCTEXT("LockerTitle", "PERSONAJE"), BodyFontFace, 26, Ink);
		Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 8.0f));

		UHorizontalBox* Categories = WidgetTree->ConstructWidget<UHorizontalBox>();
		const FText CategoryNames[] = {
			LOCTEXT("CatOutfit", "TRAJE"), LOCTEXT("CatBackpack", "MOCHILA"), LOCTEXT("CatPickaxe", "PICO"), LOCTEXT("CatGlider", "ALA DELTA")
		};
		CategoryButtons.Reset();
		CategoryLabels.Reset();
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(CategoryNames); ++Index)
		{
			UButton* Category = MakeButton(Clear, Clear, Clear, 14.0f);
			ArenaGlass::Style(Category, ArenaGlass::ButtonStyle(0.06f, 15.0f, 0.20f));
			UTextBlock* Label = MakeText(CategoryNames[Index], BodyFontFace, 12, Ink, false);
			if (UButtonSlot* CategorySlot = Cast<UButtonSlot>(Category->SetContent(Label)))
			{
				CategorySlot->SetPadding(FMargin(14.0f, 5.0f));
			}
			switch (Index)
			{
			case ArenaLockerCategory::Outfit: Category->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleCategoryOutfit); break;
			case ArenaLockerCategory::Pickaxe: Category->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleCategoryPickaxe); break;
			case ArenaLockerCategory::Glider: Category->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleCategoryGlider); break;
			default: Category->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleComingSoon); break;
			}
			CategoryButtons.Add(Category);
			CategoryLabels.Add(Label);
			Categories->AddChildToHorizontalBox(Category)->SetPadding(FMargin(Index == 0 ? 6.0f : 4.0f, 0.0f, 0.0f, 0.0f));
		}
		Column->AddChildToVerticalBox(Categories)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));

		SkinCountText = MakeText(FText::GetEmpty(), BodyFontFace, 11, Dim, false);
		Column->AddChildToVerticalBox(SkinCountText)->SetPadding(FMargin(10.0f, 4.0f, 0.0f, 8.0f));

		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(Scroll);
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		SkinGrid = WidgetTree->ConstructWidget<UUniformGridPanel>();
		SkinGrid->SetSlotPadding(FMargin(6.0f));
		SkinGrid->SetMinDesiredSlotWidth(134.0f);
		SkinGrid->SetMinDesiredSlotHeight(162.0f);
		if (UScrollBoxSlot* GridSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(SkinGrid)))
		{
			// Tiles start at the left, whatever the number of outfits
			GridSlot->SetHorizontalAlignment(HAlign_Left);
		}

		StyleRows = WidgetTree->ConstructWidget<UVerticalBox>();
		StyleRows->SetVisibility(ESlateVisibility::Collapsed);
		if (UScrollBoxSlot* RowsSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(StyleRows)))
		{
			RowsSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// Bottom left: info about the outfit being worn
	UVerticalBox* Info = WidgetTree->ConstructWidget<UVerticalBox>();
	AddToCanvas(Page, Info, FAnchors(0, 1), FVector2D(0.0f, 1.0f), FVector2D(48.0f, -92.0f), FVector2D::ZeroVector, true);
	{
		KindText = MakeText(LOCTEXT("OutfitKind", "TRAJE"), BodyFontFace, 12, ArenaGlass::Mint, false);
		Info->AddChildToVerticalBox(KindText)->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 2.0f));
		SkinNameText = MakeText(FText::GetEmpty(), BodyFontFace, 38, Ink);
		Info->AddChildToVerticalBox(SkinNameText);
		Info->AddChildToVerticalBox(MakeText(LOCTEXT("OutfitDesc", "Importado con FortnitePorting."), BodyFontFace, 12, Dim, false))->SetPadding(FMargin(4.0f, 2.0f, 0.0f, 0.0f));
	}

	// Bottom right: back button
	UButton* Back = MakeButton(Clear, Clear, Clear, 14.0f);
	ArenaGlass::Style(Back, ArenaGlass::ButtonStyle(0.16f, 15.0f, 0.50f, ArenaGlass::Ice));
	UHorizontalBox* BackRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>();
	KeyCap->SetBrush(ArenaGlass::Surface(0.30f, 6.0f, 0.20f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));
	KeyCap->SetPadding(FMargin(6.0f, 1.0f));
	KeyCap->SetContent(MakeText(LOCTEXT("EscKey", "ESC"), BodyFontFace, 9, Ink, false));
	BackRow->AddChildToHorizontalBox(KeyCap)->SetVerticalAlignment(VAlign_Center);
	UHorizontalBoxSlot* BackLabelSlot = BackRow->AddChildToHorizontalBox(MakeText(LOCTEXT("Back", "ATRÁS"), BodyFontFace, 13, Ink, false));
	BackLabelSlot->SetVerticalAlignment(VAlign_Center);
	BackLabelSlot->SetPadding(FMargin(9.0f, 0.0f, 0.0f, 0.0f));
	if (UButtonSlot* BackSlot = Cast<UButtonSlot>(Back->SetContent(BackRow)))
	{
		BackSlot->SetPadding(FMargin(16.0f, 6.0f));
	}
	Back->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleBackPressed);

	// EDIT sits to the left of ATRAS; it only shows while the cursor is over an outfit that has more than one style
	EditButton = MakeButton(Clear, Clear, Clear, 14.0f);
	ArenaGlass::Style(EditButton, ArenaGlass::ButtonStyle(0.26f, 15.0f, 0.60f, ArenaGlass::Amber));
	UTextBlock* EditLabel = MakeText(LOCTEXT("EditStyles", "EDIT"), BodyFontFace, 13, Ink, false);
	if (UButtonSlot* EditSlot = Cast<UButtonSlot>(EditButton->SetContent(EditLabel)))
	{
		EditSlot->SetPadding(FMargin(18.0f, 6.0f));
	}
	EditButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleEditPressed);
	EditButton->OnHovered.AddDynamic(this, &UArenaLobbyWidget::HandleEditHovered);
	EditButton->OnUnhovered.AddDynamic(this, &UArenaLobbyWidget::HandleEditUnhovered);
	EditButton->SetVisibility(ESlateVisibility::Collapsed);

	UHorizontalBox* BottomButtons = WidgetTree->ConstructWidget<UHorizontalBox>();
	BottomButtons->AddChildToHorizontalBox(EditButton)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	BottomButtons->AddChildToHorizontalBox(Back);
	AddToCanvas(Page, BottomButtons, FAnchors(1, 1), FVector2D(1.0f, 1.0f), FVector2D(-60.0f, -34.0f), FVector2D::ZeroVector, true);

	RefreshCategoryTabs();
	RebuildSkinGrid();
}

TArray<FArenaSkinEntry>& UArenaLobbyWidget::ItemsOf(int32 LockerCategory)
{
	switch (LockerCategory)
	{
	case ArenaLockerCategory::Pickaxe: return Pickaxes;
	case ArenaLockerCategory::Glider: return Gliders;
	default: return Skins;
	}
}

int32& UArenaLobbyWidget::SelectionOf(int32 LockerCategory)
{
	switch (LockerCategory)
	{
	case ArenaLockerCategory::Pickaxe: return SelectedPickaxe;
	case ArenaLockerCategory::Glider: return SelectedGlider;
	case ArenaLockerCategory::Outfit: return SelectedSkin;
	default: return NoSelection;
	}
}

void UArenaLobbyWidget::RefreshCategoryTabs()
{
	using namespace ArenaLobbyStyle;
	for (int32 Index = 0; Index < CategoryButtons.Num(); ++Index)
	{
		const bool bSelected = Index == CurrentCategory;
		ArenaGlass::Style(CategoryButtons[Index], bSelected ? ArenaGlass::ButtonStyle(0.26f, 15.0f, 0.55f) : ArenaGlass::ButtonStyle(0.06f, 15.0f, 0.20f));
		if (CategoryLabels.IsValidIndex(Index) && CategoryLabels[Index])
		{
			CategoryLabels[Index]->SetColorAndOpacity(FSlateColor(bSelected ? ArenaGlass::Ink : ArenaGlass::Dim));
		}
	}
}

void UArenaLobbyWidget::SelectCategory(int32 LockerCategory)
{
	if (LockerCategory == ArenaLockerCategory::Backpack || LockerCategory < 0 || LockerCategory >= ArenaLockerCategory::Count)
	{
		HandleComingSoon();
		return;
	}

	if (bStyleView)
	{
		bStyleView = false;
		StyleViewSkin = INDEX_NONE;
	}
	HideEditButton();
	CurrentCategory = LockerCategory;
	RefreshCategoryTabs();
	RebuildSkinGrid();
	RestEditButton();
}

void UArenaLobbyWidget::HandleCategoryOutfit()
{
	SelectCategory(ArenaLockerCategory::Outfit);
}

void UArenaLobbyWidget::HandleCategoryPickaxe()
{
	SelectCategory(ArenaLockerCategory::Pickaxe);
}

void UArenaLobbyWidget::HandleCategoryGlider()
{
	SelectCategory(ArenaLockerCategory::Glider);
}

void UArenaLobbyWidget::SetLockerItems(int32 LockerCategory, const TArray<FArenaSkinEntry>& Items, int32 InSelected)
{
	if (LockerCategory == ArenaLockerCategory::Backpack || LockerCategory < 0 || LockerCategory >= ArenaLockerCategory::Count)
	{
		return;
	}

	ItemsOf(LockerCategory) = Items;
	SelectionOf(LockerCategory) = InSelected;
	if (LockerCategory == CurrentCategory)
	{
		RebuildSkinGrid();
	}
}

void UArenaLobbyWidget::SetLockerSelection(int32 LockerCategory, int32 Index)
{
	SelectionOf(LockerCategory) = Index;
	if (LockerCategory == CurrentCategory)
	{
		RefreshSkinInfo();
	}
}

UBorder* UArenaLobbyWidget::BuildTile(const FArenaSkinEntry& Skin, int32 Index)
{
	using namespace ArenaLobbyStyle;

	// Frame: turns yellow on the worn outfit
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
	Frame->SetPadding(FMargin(4.0f));
	Frame->SetBrush(ArenaGlass::Surface(0.0f, 18.0f, 0.0f));

	UButton* TileButton = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle TileButtonStyle = ArenaGlass::ButtonStyle(0.10f, 14.0f, 0.28f);
	TileButtonStyle.SetPressedPadding(FMargin(0.0f));
	ArenaGlass::Style(TileButton, TileButtonStyle);
	TileButton->OnHovered.AddDynamic(this, &UArenaLobbyWidget::HandleButtonHovered);
	UOverlay* TileContent = WidgetTree->ConstructWidget<UOverlay>();
	UImage* Shine = WidgetTree->ConstructWidget<UImage>();
	if (GradientTexture)
	{
		constexpr float TileCornerRadius = 14.0f;
		FSlateRoundedBoxBrush RoundedShineBrush(
			GradientTexture->GetFName(),
			FLinearColor::White,
			FVector4(TileCornerRadius, TileCornerRadius, TileCornerRadius, TileCornerRadius),
			FVector2D(126.0f, 154.0f));
		RoundedShineBrush.SetResourceObject(GradientTexture);
		Shine->SetBrush(RoundedShineBrush);
	}
	Shine->SetColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.0f, 0.14f));
	Shine->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* ShineSlot = TileContent->AddChildToOverlay(Shine);
	ShineSlot->SetHorizontalAlignment(HAlign_Fill);
	ShineSlot->SetVerticalAlignment(VAlign_Fill);

	if (Skin.Icon)
	{
		UImage* Icon = WidgetTree->ConstructWidget<UImage>();
		constexpr float TileCornerRadius = 14.0f;
		FSlateRoundedBoxBrush RoundedIconBrush(
			Skin.Icon->GetFName(),
			FLinearColor::White,
			FVector4(TileCornerRadius, TileCornerRadius, TileCornerRadius, TileCornerRadius),
			FVector2D(126.0f, 154.0f));
		RoundedIconBrush.SetResourceObject(Skin.Icon);
		Icon->SetBrush(RoundedIconBrush);
		Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* IconSize = WidgetTree->ConstructWidget<USizeBox>();
		IconSize->SetWidthOverride(126.0f);
		IconSize->SetHeightOverride(154.0f);
		IconSize->SetContent(Icon);
		IconSize->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* IconSlot = TileContent->AddChildToOverlay(IconSize);
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}
	else
	{
		UTextBlock* Initial = MakeText(FText::FromString(Skin.Name.ToString().Left(1)), BodyFontFace, 40, ArenaGlass::Ink);
		UOverlaySlot* InitialSlot = TileContent->AddChildToOverlay(Initial);
		InitialSlot->SetHorizontalAlignment(HAlign_Center);
		InitialSlot->SetVerticalAlignment(VAlign_Center);
	}

	// Name strip at the bottom of the tile
	UBorder* NameStrip = WidgetTree->ConstructWidget<UBorder>();
	NameStrip->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.0f, 0.0f, 0.04f, 0.42f), FVector4(12.0f, 12.0f, 12.0f, 12.0f)));
	NameStrip->SetPadding(FMargin(6.0f, 2.0f));
	NameStrip->SetVisibility(ESlateVisibility::HitTestInvisible);
	UTextBlock* NameLabel = MakeText(Skin.Name, BodyFontFace, 11, ArenaGlass::Ink, false);
	NameLabel->SetClipping(EWidgetClipping::ClipToBounds);
	NameStrip->SetContent(NameLabel);
	NameStrip->SetHorizontalAlignment(HAlign_Center);
	UOverlaySlot* StripSlot = TileContent->AddChildToOverlay(NameStrip);
	StripSlot->SetHorizontalAlignment(HAlign_Fill);
	StripSlot->SetVerticalAlignment(VAlign_Bottom);

	if (UButtonSlot* TileSlot = Cast<UButtonSlot>(TileButton->SetContent(TileContent)))
	{
		TileSlot->SetPadding(FMargin(0.0f));
		TileSlot->SetHorizontalAlignment(HAlign_Fill);
		TileSlot->SetVerticalAlignment(VAlign_Fill);
	}
	TileButton->SetClipping(EWidgetClipping::ClipToBounds);

	UArenaSkinTileHandler* Handler = NewObject<UArenaSkinTileHandler>(this);
	Handler->Index = Index;
	Handler->Owner = this;
	TileButton->OnClicked.AddDynamic(Handler, &UArenaSkinTileHandler::HandleClicked);
	TileButton->OnHovered.AddDynamic(Handler, &UArenaSkinTileHandler::HandleHovered);
	TileButton->OnUnhovered.AddDynamic(Handler, &UArenaSkinTileHandler::HandleUnhovered);
	TileHandlers.Add(Handler);

	USizeBox* TileSize = WidgetTree->ConstructWidget<USizeBox>();
	TileSize->SetWidthOverride(126.0f);
	TileSize->SetHeightOverride(154.0f);
	TileSize->SetContent(TileButton);
	Frame->SetContent(TileSize);

	SkinFrames.Add(Frame);
	return Frame;
}

void UArenaLobbyWidget::RebuildSkinGrid()
{
	using namespace ArenaLobbyStyle;
	if (SkinGrid == nullptr)
	{
		return;
	}

	SkinGrid->ClearChildren();
	if (StyleRows)
	{
		StyleRows->ClearChildren();
	}
	SkinFrames.Reset();
	TileHandlers.Reset();
	StyleTileChannel.Reset();
	StyleTileOption.Reset();

	// The styles view replaces the grid with one row of options per channel of the outfit
	const bool bShowingStyles = bStyleView && CurrentCategory == ArenaLockerCategory::Outfit && Skins.IsValidIndex(StyleViewSkin);
	SkinGrid->SetVisibility(bShowingStyles ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (StyleRows)
	{
		StyleRows->SetVisibility(bShowingStyles ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (bShowingStyles)
	{
		RebuildStyleRows();
		RefreshSkinInfo();
		return;
	}

	const int32 Columns = FMath::Max(1, LockerColumns);
	const TArray<FArenaSkinEntry>& Items = ItemsOf(CurrentCategory);
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		UBorder* Frame = BuildTile(Items[Index], Index);
		UUniformGridSlot* GridSlot = SkinGrid->AddChildToUniformGrid(Frame, Index / Columns, Index % Columns);
		GridSlot->SetHorizontalAlignment(HAlign_Center);
		GridSlot->SetVerticalAlignment(VAlign_Center);
	}

	RefreshSkinInfo();
}

void UArenaLobbyWidget::RebuildStyleRows()
{
	using namespace ArenaLobbyStyle;
	if (StyleRows == nullptr || !Skins.IsValidIndex(StyleViewSkin) || Skins[StyleViewSkin].StyleData == nullptr)
	{
		return;
	}

	const FArenaSkinEntry& Skin = Skins[StyleViewSkin];
	int32 TileIndex = 0;
	bool bFirstRow = true;
	for (int32 ChannelIndex = 0; ChannelIndex < Skin.StyleData->Channels.Num(); ++ChannelIndex)
	{
		const FFortnitePortingStyleChannel& Channel = Skin.StyleData->Channels[ChannelIndex];
		if (Channel.Options.Num() < 2)
		{
			continue;
		}

		UTextBlock* Header = MakeText(Channel.Name.ToUpper(), BodyFontFace, 12, ArenaGlass::Dim, false);
		StyleRows->AddChildToVerticalBox(Header)->SetPadding(FMargin(10.0f, bFirstRow ? 0.0f : 16.0f, 0.0f, 6.0f));
		bFirstRow = false;

		UWrapBox* Row = WidgetTree->ConstructWidget<UWrapBox>();
		Row->SetInnerSlotPadding(FVector2D(6.0f, 6.0f));
		for (int32 OptionIndex = 0; OptionIndex < Channel.Options.Num(); ++OptionIndex)
		{
			const FFortnitePortingStyleOption& Option = Channel.Options[OptionIndex];
			FArenaSkinEntry Entry;
			Entry.Name = Option.Name.ToUpper();
			Entry.Icon = Option.Icon ? Option.Icon : Skin.Icon;

			UBorder* Frame = BuildTile(Entry, TileIndex);
			StyleTileChannel.Add(ChannelIndex);
			StyleTileOption.Add(OptionIndex);
			Row->AddChildToWrapBox(Frame);
			++TileIndex;
		}
		StyleRows->AddChildToVerticalBox(Row);
	}
}

void UArenaLobbyWidget::RefreshSkinInfo()
{
	using namespace ArenaLobbyStyle;
	const bool bShowingStyles = bStyleView && CurrentCategory == ArenaLockerCategory::Outfit && Skins.IsValidIndex(StyleViewSkin);
	const TArray<FArenaSkinEntry>& Items = ItemsOf(CurrentCategory);
	const int32 Selected = SelectionOf(CurrentCategory);
	for (int32 Index = 0; Index < SkinFrames.Num(); ++Index)
	{
		if (SkinFrames[Index])
		{
			bool bWorn = Index == Selected;
			if (bShowingStyles)
			{
				const int32 Channel = StyleTileChannel.IsValidIndex(Index) ? StyleTileChannel[Index] : INDEX_NONE;
				const int32 Picked = StyleSelection.IsValidIndex(Channel) ? StyleSelection[Channel] : 0;
				bWorn = StyleTileOption.IsValidIndex(Index) && StyleTileOption[Index] == Picked;
			}
			SkinFrames[Index]->SetBrush(bWorn ? ArenaGlass::Surface(0.16f, 18.0f, 0.90f, ArenaGlass::Mint, 2.0f) : ArenaGlass::Surface(0.0f, 18.0f, 0.0f));
		}
	}

	if (bShowingStyles)
	{
		const FArenaSkinEntry& StyledSkin = Skins[StyleViewSkin];
		int32 Channels = 0;
		int32 Options = 0;
		FText SingleName = StyledSkin.Name;
		if (StyledSkin.StyleData)
		{
			for (int32 ChannelIndex = 0; ChannelIndex < StyledSkin.StyleData->Channels.Num(); ++ChannelIndex)
			{
				const FFortnitePortingStyleChannel& Channel = StyledSkin.StyleData->Channels[ChannelIndex];
				if (Channel.Options.Num() < 2)
				{
					continue;
				}
				++Channels;
				Options += Channel.Options.Num();
				const int32 Picked = StyleSelection.IsValidIndex(ChannelIndex) ? StyleSelection[ChannelIndex] : 0;
				if (Channel.Options.IsValidIndex(Picked))
				{
					SingleName = Channel.Options[Picked].Name.ToUpper();
				}
			}
		}
		if (KindText)
		{
			KindText->SetText(FText::Format(LOCTEXT("StyleKind", "ESTILOS - {0}"), StyledSkin.Name));
		}
		if (SkinNameText)
		{
			SkinNameText->SetText(Channels == 1 ? SingleName : StyledSkin.Name);
		}
		if (SkinCountText)
		{
			SkinCountText->SetText(FText::Format(LOCTEXT("StyleCount", "{0} OPCIONES"), FText::AsNumber(Options)));
		}
		return;
	}

	const bool bOutfits = CurrentCategory == ArenaLockerCategory::Outfit;
	const bool bPickaxes = CurrentCategory == ArenaLockerCategory::Pickaxe;
	if (KindText)
	{
		KindText->SetText(bOutfits ? LOCTEXT("OutfitKind", "TRAJE - ESTILO DE FORTNITE")
			: bPickaxes ? LOCTEXT("PickaxeKind", "PICO - HERRAMIENTA DE RECOLECCIÓN") : LOCTEXT("GliderKind", "ALA DELTA"));
	}
	if (SkinNameText)
	{
		SkinNameText->SetText(Items.IsValidIndex(Selected) ? Items[Selected].Name : LOCTEXT("NoOutfit", "VACÍO"));
	}
	if (SkinCountText)
	{
		const FText Format = bOutfits ? LOCTEXT("OutfitCount", "{0} TRAJES") : bPickaxes ? LOCTEXT("PickaxeCount", "{0} PICOS") : LOCTEXT("GliderCount", "{0} ALAS DELTA");
		SkinCountText->SetText(FText::Format(Format, FText::AsNumber(Items.Num())));
	}
}

void UArenaLobbyWidget::SetSkins(const TArray<FArenaSkinEntry>& InSkins, int32 InSelected)
{
	Skins = InSkins;
	SelectedSkin = InSelected;
	UpdateAvatars();
	if (CurrentCategory == ArenaLockerCategory::Outfit)
	{
		RebuildSkinGrid();
	}
	RestEditButton();
}

void UArenaLobbyWidget::SetSelectedSkin(int32 Index)
{
	SelectedSkin = Index;
	RefreshSkinInfo();
	UpdateAvatars();
	RestEditButton();
}

void UArenaLobbyWidget::SetStyleSelection(const TArray<int32>& Selection)
{
	StyleSelection = Selection;
	RefreshSkinInfo();
}

void UArenaLobbyWidget::ShowEditButton(int32 SkinIndex)
{
	EditTargetSkin = SkinIndex;
	if (EditButton)
	{
		EditButton->SetVisibility(ESlateVisibility::Visible);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EditHideTimer);
	}
}

void UArenaLobbyWidget::HideEditButton()
{
	EditTargetSkin = INDEX_NONE;
	bEditHovered = false;
	if (EditButton)
	{
		EditButton->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EditHideTimer);
	}
}

void UArenaLobbyWidget::RestEditButton()
{
	const bool bOutfitGrid = CurrentCategory == ArenaLockerCategory::Outfit && !bStyleView && !bServerBrowserOpen;
	if (bOutfitGrid && Skins.IsValidIndex(SelectedSkin) && Skins[SelectedSkin].StyleData && Skins[SelectedSkin].StyleData->HasChoices())
	{
		bEditHovered = false;
		ShowEditButton(SelectedSkin);
	}
	else
	{
		HideEditButton();
	}
}

void UArenaLobbyWidget::HoverSkinTile(int32 Index)
{
	// Only outfits with more than one style offer EDIT (not the styles view, nor pickaxes, gliders or servers)
	const bool bOutfitGrid = CurrentCategory == ArenaLockerCategory::Outfit && !bStyleView && !bServerBrowserOpen;
	if (bOutfitGrid && Skins.IsValidIndex(Index) && Skins[Index].StyleData && Skins[Index].StyleData->HasChoices())
	{
		ShowEditButton(Index);
	}
	else
	{
		RestEditButton();
	}
}

void UArenaLobbyWidget::UnhoverSkinTile(int32 Index)
{
	// The button stays a moment so the cursor can travel from the tile to it
	if (EditTargetSkin != INDEX_NONE && !bEditHovered)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(EditHideTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (!bEditHovered)
				{
					RestEditButton();
				}
			}), 0.8f, false);
		}
	}
}

void UArenaLobbyWidget::HandleEditHovered()
{
	bEditHovered = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EditHideTimer);
	}
}

void UArenaLobbyWidget::HandleEditUnhovered()
{
	bEditHovered = false;
	UnhoverSkinTile(EditTargetSkin);
}

void UArenaLobbyWidget::HandleEditPressed()
{
	if (Skins.IsValidIndex(EditTargetSkin))
	{
		EnterStyleView(EditTargetSkin);
	}
}

void UArenaLobbyWidget::EnterStyleView(int32 SkinIndex)
{
	if (!Skins.IsValidIndex(SkinIndex) || Skins[SkinIndex].StyleData == nullptr || !Skins[SkinIndex].StyleData->HasChoices())
	{
		return;
	}

	// The character shown is the one being edited: wear the outfit first if another one was worn
	if (SelectedSkin != SkinIndex)
	{
		SelectedSkin = SkinIndex;
		StyleSelection.Reset();
		OnSkinSelected.Broadcast(SkinIndex);
		UpdateAvatars();
	}

	HideEditButton();
	bStyleView = true;
	StyleViewSkin = SkinIndex;
	RebuildSkinGrid();
}

void UArenaLobbyWidget::ExitStyleView()
{
	if (!bStyleView)
	{
		return;
	}

	bStyleView = false;
	StyleViewSkin = INDEX_NONE;
	RebuildSkinGrid();
	RestEditButton();
}

void UArenaLobbyWidget::HandleBackPressed()
{
	if (bStyleView)
	{
		ExitStyleView();
		return;
	}
	HandlePlayTab();
}

void UArenaLobbyWidget::ClickSkin(int32 Index)
{
	// If the server browser is open, route the click as a server selection
	if (bServerBrowserOpen)
	{
		if (CachedServerList.IsValidIndex(Index))
		{
			OnJoinServerSelected.Broadcast(Index);
		}
		return;
	}

	// In the styles view a click picks a style of the outfit being edited
	if (bStyleView && Skins.IsValidIndex(StyleViewSkin))
	{
		if (StyleTileChannel.IsValidIndex(Index))
		{
			const int32 Channel = StyleTileChannel[Index];
			const int32 Option = StyleTileOption[Index];
			if (StyleSelection.Num() <= Channel)
			{
				StyleSelection.SetNumZeroed(Channel + 1);
			}
			if (StyleSelection[Channel] != Option)
			{
				StyleSelection[Channel] = Option;
				RefreshSkinInfo();
				OnStyleSelected.Broadcast(StyleViewSkin, Channel, Option);
			}
		}
		return;
	}

	const TArray<FArenaSkinEntry>& Items = ItemsOf(CurrentCategory);
	int32& Selected = SelectionOf(CurrentCategory);
	if (!Items.IsValidIndex(Index) || Index == Selected)
	{
		return;
	}

	Selected = Index;
	RefreshSkinInfo();
	UpdateAvatars();
	if (CurrentCategory == ArenaLockerCategory::Outfit)
	{
		OnSkinSelected.Broadcast(Index);
	}
	else
	{
		OnLockerItemSelected.Broadcast(CurrentCategory, Index);
	}
}

void UArenaLobbyWidget::SetTabSelected(int32 Tab)
{
	using namespace ArenaLobbyStyle;
	for (int32 Index = 0; Index < TabLabels.Num(); ++Index)
	{
		const bool bSelected = Index == Tab;
		if (TabLabels[Index])
		{
			TabLabels[Index]->SetColorAndOpacity(FSlateColor(bSelected ? ArenaGlass::Ink : ArenaGlass::Dim));
		}
		if (TabButtons.IsValidIndex(Index) && TabButtons[Index])
		{
			ArenaGlass::Style(TabButtons[Index], bSelected ? ArenaGlass::ButtonStyle(0.26f, 18.0f, 0.55f) : ArenaGlass::ButtonStyle(0.0f, 18.0f, 0.0f));
		}
	}
}

void UArenaLobbyWidget::ShowLobby()
{
	const bool bChanged = bLockerOpen;
	ExitStyleView();
	bLockerOpen = false;
	LockerZoom = 0.0f;
	LockerYaw = 0.0f;
	bRotatingCharacter = false;
	if (LobbyPage)
	{
		LobbyPage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (LockerPage)
	{
		LockerPage->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetTabSelected(ArenaLobbyStyle::TabPlay);
	for (UWidget* Widget : TopBarWidgets)
	{
		Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (bChanged)
	{
		OnPageChanged.Broadcast(false);
	}
}

void UArenaLobbyWidget::ShowLocker()
{
	const bool bChanged = !bLockerOpen;
	bLockerOpen = true;
	if (LobbyPage)
	{
		LobbyPage->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (LockerPage)
	{
		LockerPage->SetVisibility(ESlateVisibility::Visible);
	}
	SetTabSelected(ArenaLobbyStyle::TabLocker);
	// Fortnite's locker is full screen: no top navigation, ESC / ATRÁS goes back
	for (UWidget* Widget : TopBarWidgets)
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}
	RestEditButton();
	if (bChanged)
	{
		OnPageChanged.Broadcast(true);
	}
}

void UArenaLobbyWidget::ShowToast(const FText& Message)
{
	if (ToastText)
	{
		ToastText->SetText(Message);
		ToastText->SetRenderOpacity(1.0f);
		ToastTimer = 1.6f;
	}
}

void UArenaLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// "Detener baile" fades in while the character dances and fades out when it stops
	if (StopDanceButton)
	{
		APawn* DancePawn = GetOwningPlayerPawn();
		const UFortnitePortingCharacterComponent* Dancer = DancePawn ? DancePawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;
		const bool bDancing = Dancer && Dancer->IsEmoting();
		StopDanceAlpha = FMath::FInterpTo(StopDanceAlpha, bDancing ? 1.0f : 0.0f, InDeltaTime, 6.0f);
		if (StopDanceAlpha < 0.01f)
		{
			StopDanceAlpha = 0.0f;
		}
		StopDanceButton->SetRenderOpacity(StopDanceAlpha);
		const ESlateVisibility Wanted = StopDanceAlpha > 0.0f ? (bDancing ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible) : ESlateVisibility::Collapsed;
		if (StopDanceButton->GetVisibility() != Wanted)
		{
			StopDanceButton->SetVisibility(Wanted);
		}
	}

	// the leave group button exists only while the player is in a group
	GroupCheckTimer -= InDeltaTime;
	if (LeaveGroupButton && GroupCheckTimer <= 0.0f)
	{
		GroupCheckTimer = 0.5f;
		bool bInGroup = false;
		if (const UGameInstance* Instance = GetGameInstance())
		{
			if (const UArenaSessionSubsystem* Sessions = Instance->GetSubsystem<UArenaSessionSubsystem>())
			{
				bInGroup = Sessions->IsInSession();
			}
		}
		LeaveGroupButton->SetVisibility(bInGroup ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// The blur applies the UI scale to its corner radius itself, exactly like the glass border brush, so the same units match at any resolution
	if (SidePanelBlur)
	{
		const float Radius = ArenaGlass::BlurRadius(); // SBackgroundBlur multiplies the radius by the widget scale itself, like the border brush
		if (!FMath::IsNearlyEqual(Radius, SidePanelBlurScale, 0.01f))
		{
			SidePanelBlurScale = Radius;
			SidePanelBlur->SetCornerRadius(FVector4(Radius, Radius, Radius, Radius));
		}
	}

	// Side panel slides in from the right edge (ease out)
	const float SlideTarget = bSidePanelOpen ? 0.0f : 1.0f;
	if (!FMath::IsNearlyEqual(SidePanelSlide, SlideTarget, 0.001f))
	{
		SidePanelSlide = FMath::FInterpTo(SidePanelSlide, SlideTarget, InDeltaTime, 14.0f);
		if (FMath::IsNearlyEqual(SidePanelSlide, SlideTarget, 0.002f))
		{
			SidePanelSlide = SlideTarget;
		}
		if (SidePanel)
		{
			SidePanel->SetRenderTranslation(FVector2D(SidePanelSlide * 640.0f, 0.0f));
			SidePanel->SetVisibility(SidePanelSlide >= 1.0f ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		}
		if (SidePanelShade)
		{
			SidePanelShade->SetRenderOpacity(1.0f - SidePanelSlide);
		}
	}
	if (LobbyInviteNotice && bLobbyInviteNoticeVisible)
	{
		if (bLobbyInviteNoticeEntering)
		{
			LobbyInviteNoticeOffscreenX = 72.0f;
			LobbyInviteNoticeSlide = -LobbyInviteNoticeOffscreenX;
			bLobbyInviteNoticeEntering = false;
		}

		if (!bLobbyInviteNoticeLeaving)
		{
			LobbyInviteNoticeTimer -= InDeltaTime;
			if (LobbyInviteNoticeTimer <= 0.0f)
			{
				DismissLobbyInviteNotice();
			}
		}

		const float TargetSlide = bLobbyInviteNoticeLeaving ? -LobbyInviteNoticeOffscreenX : 0.0f;
		LobbyInviteNoticeSlide = FMath::FInterpTo(LobbyInviteNoticeSlide, TargetSlide, InDeltaTime, 12.0f);
		if (FMath::IsNearlyEqual(LobbyInviteNoticeSlide, TargetSlide, 0.5f))
		{
			LobbyInviteNoticeSlide = TargetSlide;
		}
		LobbyInviteNotice->SetRenderTranslation(FVector2D(LobbyInviteNoticeSlide, 0.0f));
		const float Opacity = bLobbyInviteNoticeLeaving
			? FMath::Clamp(1.0f + LobbyInviteNoticeSlide / LobbyInviteNoticeOffscreenX, 0.0f, 1.0f)
			: FMath::Clamp(1.0f + LobbyInviteNoticeSlide / LobbyInviteNoticeOffscreenX, 0.0f, 1.0f);
		LobbyInviteNotice->SetRenderOpacity(Opacity);

		if (bLobbyInviteNoticeLeaving && LobbyInviteNoticeSlide <= -LobbyInviteNoticeOffscreenX + 0.5f)
		{
			LobbyInviteNotice->SetVisibility(ESlateVisibility::Collapsed);
			bLobbyInviteNoticeVisible = false;
			bLobbyInviteNoticeLeaving = false;
			bLobbyInviteNoticeEntering = false;
			ActiveLobbyInviteNotice = FArenaLobbyInviteEntry();
			ShowNextLobbyInviteNotice();
		}
	}
	if (ToastTimer > 0.0f && ToastText)
	{
		ToastTimer -= InDeltaTime;
		ToastText->SetRenderOpacity(FMath::Clamp(ToastTimer / 0.4f, 0.0f, 1.0f));
	}
}

void UArenaLobbyWidget::HandleLeaveGroup()
{
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = Instance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->LeaveSession();
		}
	}
	// a fresh lobby: nobody else is in it any more
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Lobby/Lvl_Lobby")));
}

void UArenaLobbyWidget::HandleComingSoon()
{
	ShowToast(LOCTEXT("ComingSoon", "PRÓXIMAMENTE"));
}

void UArenaLobbyWidget::HandlePlayTab()
{
	HideMapPage();
	ShowLobby();
}

void UArenaLobbyWidget::HandleLockerTab()
{
	HideMapPage();
	ShowLocker();
}

void UArenaLobbyWidget::HandlePlayClicked()
{
	RequestPlay();
}

void UArenaLobbyWidget::HandleHostClicked()
{
	RequestHost();
}

void UArenaLobbyWidget::HandleJoinClicked()
{
	RequestJoin();
}

void UArenaLobbyWidget::HandleRefreshClicked()
{
	if (ServerBrowserStatusText)
	{
		ServerBrowserStatusText->SetText(LOCTEXT("Searching", "BUSCANDO SERVIDORES..."));
		ServerBrowserStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (ServerListBox)
	{
		ServerListBox->ClearChildren();
	}
	OnJoinPressed.Broadcast();
}

void UArenaLobbyWidget::HandleCloseServerBrowser()
{
	HideServerBrowser();
}

void UArenaLobbyWidget::RequestPlay()
{
	if (bPlayRequested)
	{
		return;
	}
	bPlayRequested = true;
	OnPlayPressed.Broadcast();
}

void UArenaLobbyWidget::RequestHost()
{
	ShowToast(LOCTEXT("Hosting", "CREANDO SERVIDOR..."));
	OnHostPressed.Broadcast();
}

void UArenaLobbyWidget::RequestJoin()
{
	// Show the server browser panel
	if (ServerBrowserPanel)
	{
		ServerBrowserPanel->SetVisibility(ESlateVisibility::Visible);
		bServerBrowserOpen = true;
	}
	HideEditButton();
	if (ServerBrowserStatusText)
	{
		ServerBrowserStatusText->SetText(LOCTEXT("Searching", "BUSCANDO SERVIDORES..."));
		ServerBrowserStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (ServerListBox)
	{
		ServerListBox->ClearChildren();
	}
	OnJoinPressed.Broadcast();
}

void UArenaLobbyWidget::SetServerList(const TArray<FArenaServerInfo>& Servers)
{
	CachedServerList = Servers;
	RebuildServerList();
}

void UArenaLobbyWidget::HideServerBrowser()
{
	if (ServerBrowserPanel)
	{
		ServerBrowserPanel->SetVisibility(ESlateVisibility::Collapsed);
		bServerBrowserOpen = false;
	}
	RestEditButton();
}

void UArenaLobbyWidget::SetServerBrowserStatus(const FText& Status)
{
	if (ServerBrowserStatusText)
	{
		ServerBrowserStatusText->SetText(Status);
		ServerBrowserStatusText->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

namespace
{
	/** "/Game/Variant_Combat/Lvl_Combat" -> "Combat"; "Lvl_SideScrolling" -> "Side Scrolling" */
	FText MapDisplayName(const FString& AssetName)
	{
		if (AssetName.Contains(TEXT("Arena1v1"), ESearchCase::IgnoreCase))
		{
			return FText::FromString(TEXT("Arena 1v1"));
		}
		FString Name = AssetName;
		if (Name.StartsWith(TEXT("Lvl_"))) { Name.RightChopInline(4); }
		else if (Name.StartsWith(TEXT("L_"))) { Name.RightChopInline(2); }
		Name.ReplaceInline(TEXT("_"), TEXT(" "));
		FString Spaced;
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			if (Index > 0 && FChar::IsUpper(Name[Index]) && FChar::IsLower(Name[Index - 1]))
			{
				Spaced.AppendChar(TEXT(' '));
			}
			Spaced.AppendChar(Name[Index]);
		}
		return FText::FromString(Spaced);
	}
}

void UArenaLobbyWidget::BuildMapPage(UCanvasPanel* Root)
{
	using namespace ArenaLobbyStyle;
	using namespace ArenaGlass;

	// Maps of the project: the ones in the asset registry under /Game (no sample content)
	MapPaths.Reset();
	MapNames.Reset();
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Worlds;
		Registry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), Worlds, false);
		Worlds.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& World : Worlds)
		{
			const FString Package = World.PackageName.ToString();
			if (!Package.StartsWith(TEXT("/Game/")) || Package.Contains(TEXT("Lobby")) || Package.Contains(TEXT("/FortnitePorting/")) || Package.Contains(TEXT("/Developers/")) || Package.Contains(TEXT("_BuiltData")))
			{
				continue;
			}
			MapPaths.Add(Package);
			MapNames.Add(MapDisplayName(World.AssetName.ToString()));
		}
	}

	// The level that is open now is the one selected at the start
	FString Current = GetWorld() ? GetWorld()->GetOutermost()->GetName() : FString();
	Current = UWorld::RemovePIEPrefix(Current);
	SelectedMap = MapPaths.IndexOfByKey(Current);
	if (SelectedMap == INDEX_NONE)
	{
		FString SavedMap;
		if (GConfig && GConfig->GetString(TEXT("ArenaLobby"), TEXT("LastSelectedMap"), SavedMap, GGameUserSettingsIni))
		{
			SelectedMap = MapPaths.IndexOfByKey(SavedMap);
		}
		if (SelectedMap == INDEX_NONE)
		{
			// The lobby is not a place to play: start on the main level when there is one
			SelectedMap = MapPaths.IndexOfByPredicate([](const FString& Path) { return Path.Contains(TEXT("ThirdPerson")); });
			if (SelectedMap == INDEX_NONE && MapPaths.Num() > 0)
			{
				SelectedMap = 0;
			}
		}
	}
	if (MapNames.IsValidIndex(SelectedMap))
	{
		ModeName = MapNames[SelectedMap];
		if (ModeNameText)
		{
			ModeNameText->SetText(ModeName);
		}
		if (ModeArtworkImage)
		{
			ModeArtworkImage->SetVisibility(MapPaths[SelectedMap].Contains(TEXT("Lvl_Arena1v1"), ESearchCase::IgnoreCase)
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
		}
	}

	MapPage = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MapPage"));
	{
		UCanvasPanelSlot* PageSlot = Root->AddChildToCanvas(MapPage);
		PageSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		PageSlot->SetOffsets(FMargin(0.0f, 70.0f, 0.0f, 0.0f));
	}
	MapPage->SetVisibility(ESlateVisibility::Collapsed);

	// Soft dark glass over the stage so the tiles read well
	UBorder* Scrim = WidgetTree->ConstructWidget<UBorder>();
	Scrim->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.016f, 0.03f, 0.10f, 0.90f), 0.0f));
	AddToCanvas(MapPage, Scrim, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
	{
		UCanvasPanelSlot* ContentSlot = MapPage->AddChildToCanvas(Content);
		ContentSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		ContentSlot->SetOffsets(FMargin(48.0f, 14.0f, 48.0f, 60.0f));
	}
	Content->AddChildToVerticalBox(MakeText(LOCTEXT("MapsTitle", "Mapas"), BodyFontFace, 24, Ink))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	Content->AddChildToVerticalBox(MakeText(LOCTEXT("MapsSub", "Elige dónde quieres jugar"), BodyFontFace, 12, Dim, false))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	Content->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UWrapBox* Grid = WidgetTree->ConstructWidget<UWrapBox>();
	Grid->SetInnerSlotPadding(FVector2D(14.0f, 14.0f));
	Scroll->AddChild(Grid);

	if (MapPaths.IsEmpty())
	{
		Grid->AddChildToWrapBox(MakeText(LOCTEXT("NoMaps", "No hay mapas disponibles."), BodyFontFace, 14, Dim, false));
	}

	const FLinearColor Tints[] = { Violet, Mint, Ice, Coral, Amber };
	MapFrames.Reset();
	MapHandlers.Reset();
	UTexture2D* Arena1v1Artwork = nullptr;
	for (int32 Index = 0; Index < MapPaths.Num(); ++Index)
	{
		const FLinearColor Tint = Tints[Index % UE_ARRAY_COUNT(Tints)];

		UButton* MapTile = MakeButton(Clear, Clear, Clear, 18.0f);
		MapTile->SetStyle(ButtonStyle(0.0f, 18.0f, 0.0f));
		UOverlay* TileContent = WidgetTree->ConstructWidget<UOverlay>();

		// Colored glass as the artwork of the map, brighter at the top like the panels
		UBorder* Art = WidgetTree->ConstructWidget<UBorder>();
		Art->SetBrush(Surface(0.34f, 18.0f, 0.30f, Tint));
		Art->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* ArtSlot = TileContent->AddChildToOverlay(Art);
		ArtSlot->SetHorizontalAlignment(HAlign_Fill);
		ArtSlot->SetVerticalAlignment(VAlign_Fill);

		if (MapPaths[Index].Contains(TEXT("Lvl_Arena1v1"), ESearchCase::IgnoreCase))
		{
			Arena1v1Artwork = LoadArena1v1Artwork(false);
			if (Arena1v1Artwork)
			{
				UImage* Artwork = WidgetTree->ConstructWidget<UImage>();
				Artwork->SetBrushFromTexture(Arena1v1Artwork, true);
				Artwork->SetColorAndOpacity(FLinearColor::White);
				Artwork->SetVisibility(ESlateVisibility::HitTestInvisible);
				UOverlaySlot* ArtworkSlot = TileContent->AddChildToOverlay(Artwork);
				ArtworkSlot->SetHorizontalAlignment(HAlign_Fill);
				ArtworkSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}

		UBorder* Glow = WidgetTree->ConstructWidget<UBorder>();
		Glow->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), FVector4(18.0f, 18.0f, 0.0f, 0.0f)));
		Glow->SetVisibility(ESlateVisibility::HitTestInvisible);
		USizeBox* GlowSize = WidgetTree->ConstructWidget<USizeBox>();
		GlowSize->SetHeightOverride(60.0f);
		GlowSize->SetContent(Glow);
		GlowSize->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* GlowSlot = TileContent->AddChildToOverlay(GlowSize);
		GlowSlot->SetHorizontalAlignment(HAlign_Fill);
		GlowSlot->SetVerticalAlignment(VAlign_Top);

		UVerticalBox* Labels = WidgetTree->ConstructWidget<UVerticalBox>();
		Labels->AddChildToVerticalBox(MakeText(LOCTEXT("MapTag", "MAPA"), BodyFontFace, 10, Ink, false));
		Labels->AddChildToVerticalBox(MakeText(MapNames[Index], BodyFontFace, 20, Ink));
		Labels->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* LabelSlot = TileContent->AddChildToOverlay(Labels);
		LabelSlot->SetHorizontalAlignment(HAlign_Left);
		LabelSlot->SetVerticalAlignment(VAlign_Bottom);
		LabelSlot->SetPadding(FMargin(16.0f, 0.0f, 0.0f, 14.0f));

		if (UButtonSlot* TileSlot = Cast<UButtonSlot>(MapTile->SetContent(TileContent)))
		{
			TileSlot->SetPadding(FMargin(0.0f));
			TileSlot->SetHorizontalAlignment(HAlign_Fill);
			TileSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UArenaSkinTileHandler* Handler = NewObject<UArenaSkinTileHandler>(this);
		Handler->Index = Index;
		Handler->bMap = true;
		Handler->Owner = this;
		MapTile->OnClicked.AddDynamic(Handler, &UArenaSkinTileHandler::HandleClicked);
		MapHandlers.Add(Handler);

		USizeBox* TileSize = WidgetTree->ConstructWidget<USizeBox>();
		TileSize->SetWidthOverride(260.0f);
		TileSize->SetHeightOverride(150.0f);
		TileSize->SetContent(MapTile);

		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetPadding(FMargin(3.0f));
		Frame->SetContent(TileSize);
		MapFrames.Add(Frame);
		Grid->AddChildToWrapBox(Frame);
	}
	RefreshMapTiles();
}

void UArenaLobbyWidget::RefreshMapTiles()
{
	for (int32 Index = 0; Index < MapFrames.Num(); ++Index)
	{
		if (MapFrames[Index])
		{
			MapFrames[Index]->SetBrush(Index == SelectedMap ? ArenaGlass::Surface(0.10f, 21.0f, 0.95f, ArenaGlass::Mint, 2.0f) : ArenaGlass::Surface(0.0f, 21.0f, 0.0f));
		}
	}
}

void UArenaLobbyWidget::ShowMapPage()
{
	if (MapPage == nullptr)
	{
		return;
	}
	bMapPageOpen = true;
	MapPage->SetVisibility(ESlateVisibility::Visible);
}

void UArenaLobbyWidget::HideMapPage()
{
	if (MapPage == nullptr)
	{
		return;
	}
	bMapPageOpen = false;
	MapPage->SetVisibility(ESlateVisibility::Collapsed);
}

void UArenaLobbyWidget::HandleStopDance()
{
	APawn* LobbyPawn = GetOwningPlayerPawn();
	if (UFortnitePortingCharacterComponent* Cosmetics = LobbyPawn ? LobbyPawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr)
	{
		// The dance blends out and the music fades with it
		Cosmetics->StopEmote(0.7f);
	}
}

void UArenaLobbyWidget::HandleModeCardClicked()
{
	ShowMapPage();
}

void UArenaLobbyWidget::HandleModeCardHovered()
{
	if (ModeCardHoverFrame)
	{
		ModeCardHoverFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UArenaLobbyWidget::HandleModeCardUnhovered()
{
	if (ModeCardHoverFrame)
	{
		ModeCardHoverFrame->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UArenaLobbyWidget::ClickMap(int32 Index)
{
	if (!MapPaths.IsValidIndex(Index))
	{
		return;
	}
	SelectedMap = Index;
	if (GConfig)
	{
		GConfig->SetString(TEXT("ArenaLobby"), TEXT("LastSelectedMap"), *MapPaths[Index], GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	ModeName = MapNames[Index];
	if (ModeNameText)
	{
		ModeNameText->SetText(ModeName);
	}
	if (ModeArtworkImage)
	{
		ModeArtworkImage->SetVisibility(MapPaths[Index].Contains(TEXT("Lvl_Arena1v1"), ESearchCase::IgnoreCase)
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	RefreshMapTiles();
	HideMapPage();
}

void UArenaLobbyWidget::BuildServerBrowser(UCanvasPanel* Root)
{
	using namespace ArenaLobbyStyle;

	ServerBrowserPanel = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ServerBrowser"));
	AddToCanvas(Root, ServerBrowserPanel, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);
	ServerBrowserPanel->SetVisibility(ESlateVisibility::Collapsed);

	// Dim the lobby behind the browser
	UBorder* Shade = WidgetTree->ConstructWidget<UBorder>();
	Shade->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.016f, 0.024f, 0.063f, 0.55f), 0.0f));
	AddToCanvas(ServerBrowserPanel, Shade, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);

	UVerticalBox* BrowserContent = WidgetTree->ConstructWidget<UVerticalBox>();
	AddToCanvas(ServerBrowserPanel, WrapGlass(BrowserContent, FMargin(26.0f, 20.0f), 22.0f, 0.10f, 0.38f), FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, FVector2D(620.0f, 460.0f));

	// Header row: title + close button
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		UTextBlock* Title = MakeText(LOCTEXT("BrowserTitle", "Servidores LAN"), BodyFontFace, 24, ArenaGlass::Ink);
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(Title);
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);

		UButton* CloseBtn = MakeButton(Clear, Clear, Clear, 12.0f);
		ArenaGlass::Style(CloseBtn, ArenaGlass::ButtonStyle(0.14f, 13.0f, 0.45f, ArenaGlass::Coral));
		UTextBlock* CloseLabel = MakeText(LOCTEXT("Close", "X"), BodyFontFace, 12, ArenaGlass::Ink, false);
		if (UButtonSlot* CloseSlot = Cast<UButtonSlot>(CloseBtn->SetContent(CloseLabel)))
		{
			CloseSlot->SetPadding(FMargin(11.0f, 4.0f));
			CloseSlot->SetHorizontalAlignment(HAlign_Center);
		}
		CloseBtn->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleCloseServerBrowser);
		Header->AddChildToHorizontalBox(CloseBtn)->SetVerticalAlignment(VAlign_Center);
	}
	BrowserContent->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	// Column headers
	UHorizontalBox* ColumnHeaders = WidgetTree->ConstructWidget<UHorizontalBox>();
	{
		auto AddColumnHeader = [&](const FText& Text)
		{
			UHorizontalBoxSlot* Slot = ColumnHeaders->AddChildToHorizontalBox(MakeText(Text, BodyFontFace, 10, ArenaGlass::Dim, false));
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetVerticalAlignment(VAlign_Center);
		};
		AddColumnHeader(LOCTEXT("ColName", "SERVIDOR"));
		AddColumnHeader(LOCTEXT("ColPlayers", "JUGADORES"));
		AddColumnHeader(LOCTEXT("ColPing", "PING"));
	}
	BrowserContent->AddChildToVerticalBox(ColumnHeaders)->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 6.0f));

	// Separator line
	UBorder* Separator = WidgetTree->ConstructWidget<UBorder>();
	Separator->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1, 1, 1, 0.16f), 1.0f));
	USizeBox* SepSize = WidgetTree->ConstructWidget<USizeBox>();
	SepSize->SetHeightOverride(1.0f);
	SepSize->SetContent(Separator);
	BrowserContent->AddChildToVerticalBox(SepSize)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	// Server list (scrollable)
	UScrollBox* ServerScroll = WidgetTree->ConstructWidget<UScrollBox>();
	ServerScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	ServerListBox = WidgetTree->ConstructWidget<UVerticalBox>();
	ServerScroll->AddChild(ServerListBox);
	UVerticalBoxSlot* ScrollSlot = BrowserContent->AddChildToVerticalBox(ServerScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// Status text (shown while searching)
	ServerBrowserStatusText = MakeText(LOCTEXT("Searching", "BUSCANDO SERVIDORES..."), BodyFontFace, 13, ArenaGlass::Dim, false);
	ServerBrowserStatusText->SetJustification(ETextJustify::Center);
	BrowserContent->AddChildToVerticalBox(ServerBrowserStatusText)->SetPadding(FMargin(0.0f, 14.0f));

	// Refresh button at the bottom
	UButton* RefreshBtn = MakeButton(Clear, Clear, Clear, 12.0f);
	ArenaGlass::Style(RefreshBtn, ArenaGlass::ButtonStyle(0.22f, 15.0f, 0.55f, ArenaGlass::Mint));
	UTextBlock* RefreshLabel = MakeText(LOCTEXT("Refresh", "ACTUALIZAR"), BodyFontFace, 13, ArenaGlass::Ink, false);
	RefreshLabel->SetJustification(ETextJustify::Center);
	if (UButtonSlot* RefreshSlot = Cast<UButtonSlot>(RefreshBtn->SetContent(RefreshLabel)))
	{
		RefreshSlot->SetPadding(FMargin(0.0f, 6.0f));
		RefreshSlot->SetHorizontalAlignment(HAlign_Center);
		RefreshSlot->SetVerticalAlignment(VAlign_Center);
	}
	RefreshBtn->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleRefreshClicked);
	USizeBox* RefreshSize = WidgetTree->ConstructWidget<USizeBox>();
	RefreshSize->SetHeightOverride(34.0f);
	RefreshSize->SetContent(RefreshBtn);
	BrowserContent->AddChildToVerticalBox(RefreshSize)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
}

void UArenaLobbyWidget::RebuildServerList()
{
	using namespace ArenaLobbyStyle;

	if (ServerListBox == nullptr)
	{
		return;
	}

	ServerListBox->ClearChildren();
	ServerTileHandlers.Reset();

	if (CachedServerList.IsEmpty())
	{
		if (ServerBrowserStatusText)
		{
			ServerBrowserStatusText->SetText(LOCTEXT("NoServers", "NO SE ENCONTRARON SERVIDORES"));
			ServerBrowserStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

	if (ServerBrowserStatusText)
	{
		ServerBrowserStatusText->SetVisibility(ESlateVisibility::Collapsed);
	}

	for (int32 i = 0; i < CachedServerList.Num(); ++i)
	{
		const FArenaServerInfo& Info = CachedServerList[i];

		UButton* RowBtn = MakeButton(Clear, Clear, Clear, 6.0f);
		ArenaGlass::Style(RowBtn, ArenaGlass::ButtonStyle(i % 2 == 0 ? 0.05f : 0.09f, 13.0f, 0.16f));

		UHorizontalBox* RowContent = WidgetTree->ConstructWidget<UHorizontalBox>();

		// Server name
		UHorizontalBoxSlot* NameSlot = RowContent->AddChildToHorizontalBox(
			MakeText(FText::FromString(Info.ServerName), BodyFontFace, 14, ArenaGlass::Ink, false));
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);

		// Players
		const FText PlayerText = FText::Format(LOCTEXT("PlayerCount", "{0}/{1}"),
			FText::AsNumber(Info.CurrentPlayers), FText::AsNumber(Info.MaxPlayers));
		UHorizontalBoxSlot* PlayersSlot = RowContent->AddChildToHorizontalBox(
			MakeText(PlayerText, BodyFontFace, 14, ArenaGlass::Dim, false));
		PlayersSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PlayersSlot->SetVerticalAlignment(VAlign_Center);

		// Ping
		const FText PingText = FText::Format(LOCTEXT("PingMs", "{0} ms"), FText::AsNumber(Info.PingMs));
		UHorizontalBoxSlot* PingSlot = RowContent->AddChildToHorizontalBox(
			MakeText(PingText, BodyFontFace, 14, ArenaGlass::Dim, false));
		PingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PingSlot->SetVerticalAlignment(VAlign_Center);

		if (UButtonSlot* BtnSlot = Cast<UButtonSlot>(RowBtn->SetContent(RowContent)))
		{
			BtnSlot->SetPadding(FMargin(12.0f, 7.0f));
		}

		// Use a handler object to carry the server index (like the skin tile handlers)
		UArenaSkinTileHandler* Handler = NewObject<UArenaSkinTileHandler>(this);
		Handler->Index = i;
		Handler->Owner = this;
		RowBtn->OnClicked.AddDynamic(Handler, &UArenaSkinTileHandler::HandleClicked);
		ServerTileHandlers.Add(Handler);

		ServerListBox->AddChildToVerticalBox(RowBtn)->SetPadding(FMargin(0.0f, 2.0f));
	}
}

void UArenaLobbyWidget::OpenEmoteWheel()
{
	if (EmoteWheel != nullptr && EmoteWheel->IsInViewport())
	{
		return;
	}
	APawn* LobbyPawn = GetOwningPlayerPawn();
	UFortnitePortingCharacterComponent* Cosmetics = LobbyPawn ? LobbyPawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;
	UE_LOG(LogTemp, Warning, TEXT("[ArenaEmote] pawn=%s cosmetics=%d emotes=%d"), LobbyPawn ? *LobbyPawn->GetName() : TEXT("none"), Cosmetics != nullptr, Cosmetics ? Cosmetics->Emotes.Num() : -1);
	if (Cosmetics)
	{
		EmoteWheel = UArenaEmoteWheel::Show(GetOwningPlayer(), Cosmetics, this);
	}
	else
	{
		ShowToast(LOCTEXT("NoEmoteChar", "NO HAY PERSONAJE PARA LOS EMOTES"));
	}
}

void UArenaLobbyWidget::CloseEmoteWheel()
{
	if (EmoteWheel && EmoteWheel->IsInViewport())
	{
		EmoteWheel->Hide();
	}
	EmoteWheel = nullptr;
}

FReply UArenaLobbyWidget::HandleKey(const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (bMatchSocialOverlay)
	{
		if (bSidePanelOpen && (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right))
		{
			SetSidePanelOpen(false);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (Key == EKeys::B)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArenaEmote] B pressed in the lobby (map=%d side=%d)"), bMapPageOpen, bSidePanelOpen);
		if (!bMapPageOpen && !bSidePanelOpen && !bServerBrowserOpen)
		{
			OpenEmoteWheel();
		}
		return FReply::Handled();
	}
	if (bMapPageOpen)
	{
		if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
		{
			HideMapPage();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (bServerBrowserOpen)
	{
		if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
		{
			HideServerBrowser();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (bSidePanelOpen)
	{
		if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
		{
			SetSidePanelOpen(false);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	if (bLockerOpen)
	{
		if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
		{
			HandleBackPressed();
			return FReply::Handled();
		}
	}
	else if (Key == EKeys::Escape || Key == EKeys::F10 || Key == EKeys::Gamepad_Special_Right)
	{
		// Fortnite: ESC in the lobby opens the settings
		UArenaSettingsWidget::Open(GetOwningPlayer(), this);
		return FReply::Handled();
	}
	else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		RequestPlay();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UArenaLobbyWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FReply Reply = HandleKey(InKeyEvent);
	return Reply.IsEventHandled() ? Reply : Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UArenaLobbyWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::B && EmoteWheel && EmoteWheel->IsInViewport())
	{
		CloseEmoteWheel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

FReply UArenaLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FReply Reply = HandleKey(InKeyEvent);
	return Reply.IsEventHandled() ? Reply : Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UArenaLobbyWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Only reached when no list under the cursor used the wheel: zoom the locker camera like Fortnite
	if (bLockerOpen)
	{
		LockerZoom = FMath::Clamp(LockerZoom + InMouseEvent.GetWheelDelta() * 0.2f, 0.0f, 1.0f);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UArenaLobbyWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Drag on the empty part of the locker (not a tile/button) to turn the character, like Fortnite
	if (bLockerOpen && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bRotatingCharacter = true;
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UArenaLobbyWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bRotatingCharacter && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bRotatingCharacter = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UArenaLobbyWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bRotatingCharacter)
	{
		LockerYaw -= InMouseEvent.GetCursorDelta().X * 0.5f;
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

// ===================== SOCIAL SIDE PANEL =====================

UImage* UArenaLobbyWidget::MakeAvatar(float Size)
{
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	FSlateBrush Brush;
	Brush.ImageSize = FVector2D(Size, Size);
	Image->SetBrush(Brush);
	Image->SetVisibility(ESlateVisibility::HitTestInvisible);
	AvatarImages.Add(Image);
	return Image;
}

void UArenaLobbyWidget::UpdateAvatars()
{
	UTexture2D* Icon = Skins.IsValidIndex(SelectedSkin) ? Skins[SelectedSkin].Icon.Get() : nullptr;
	if (Icon == nullptr)
	{
		Icon = LoadPawnSkinIcon(GetOwningPlayerPawn());
	}

	for (UImage* Image : AvatarImages)
	{
		if (Image == nullptr)
		{
			continue;
		}
		// Circle crop of the outfit icon
		FSlateBrush Brush;
		Brush.SetResourceObject(Icon);
		Brush.ImageSize = Image->GetBrush().ImageSize;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.TintColor = Icon ? FSlateColor(FLinearColor::White) : FSlateColor(FLinearColor(0.2f, 0.3f, 0.6f));
		Image->SetBrush(Brush);
	}
}

void UArenaLobbyWidget::InitDefaultSocialData()
{
	// Clean slate: no hardcoded or mock sample friends/requests
	FriendsList.Empty();
	FriendRequestsList.Empty();
	LobbyInvitesList.Empty();
}

void UArenaLobbyWidget::AddFriend(const FText& InName, const FText& InStatus, bool bOnline)
{
	FriendsList.Add({ InName, InStatus, bOnline });
	if (ActiveSidePanelTab == 2)
	{
		RebuildAmigosContent();
	}
	else if (ActiveSidePanelTab == 1)
	{
		RebuildChatContent();
	}
}

void UArenaLobbyWidget::AddFriendRequest(const FText& RequesterName)
{
	FriendRequestsList.Add({ RequesterName });
	if (ActiveSidePanelTab == 2)
	{
		RebuildAmigosContent();
	}
}

void UArenaLobbyWidget::AddLobbyInvite(const FText& SenderName, const FString& InSessionId)
{
	LobbyInvitesList.Add({ SenderName, InSessionId });
	if (ActiveSidePanelTab == 2)
	{
		RebuildAmigosContent();
	}
}

void UArenaLobbyWidget::AcceptFriendRequest(int32 Index)
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends && FriendRequestsList.IsValidIndex(Index))
	{
		Friends->AcceptRequest(FriendRequestsList[Index].NetId);
	}
}

void UArenaLobbyWidget::DeclineFriendRequest(int32 Index)
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends && FriendRequestsList.IsValidIndex(Index))
	{
		Friends->DeclineRequest(FriendRequestsList[Index].NetId);
	}
}

void UArenaLobbyWidget::InviteFriendToLobby(int32 Index)
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends && FriendsList.IsValidIndex(Index))
	{
		Friends->InviteToLobby(FriendsList[Index].NetId);
	}
}

void UArenaLobbyWidget::SelectChatFriend(int32 Index)
{
	if (!FriendsList.IsValidIndex(Index))
	{
		return;
	}

	SelectedChatFriendId = FriendsList[Index].NetId;
	SelectedChatFriendName = FriendsList[Index].Name.ToString();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = GameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->OpenGameChat(SelectedChatFriendId);
		}
	}
	RebuildChatContent();
}

void UArenaLobbyWidget::SendChatMessage()
{
	if (SelectedChatFriendId.IsEmpty() || ChatMessageInput == nullptr)
	{
		ShowToast(LOCTEXT("ChatSelectBeforeSending", "SELECCIONA UN AMIGO PARA CHATEAR"));
		return;
	}

	const FString Body = ChatMessageInput->GetText().ToString().TrimStartAndEnd();
	if (Body.IsEmpty())
	{
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = GameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->SendGameChatMessage(SelectedChatFriendId, Body);
			ChatMessageInput->SetText(FText::GetEmpty());
		}
	}
}

void UArenaLobbyWidget::HandleChatSendClicked()
{
	SendChatMessage();
}

void UArenaLobbyWidget::HandleChatInputCommitted(const FText& /*Text*/, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SendChatMessage();
	}
}

void UArenaLobbyWidget::RebuildChatContent()
{
	if (ChatFriendsListBox == nullptr || ChatMessagesListBox == nullptr || ChatConversationTitle == nullptr)
	{
		return;
	}

	using namespace ArenaGlass;
	ChatActionHandlers.Empty();
	ChatFriendsListBox->ClearChildren();
	ChatMessagesListBox->ClearChildren();

	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends == nullptr || Friends->GetFriends().IsEmpty())
	{
		ChatFriendsListBox->AddChildToVerticalBox(MakeText(
			LOCTEXT("ChatNoFriends", "No tienes amigos disponibles para chatear."),
			BodyFontFace, 13, Dim, false));
		SelectedChatFriendId.Reset();
		SelectedChatFriendName.Reset();
		ChatConversationTitle->SetText(LOCTEXT("ChatNoConversation", "Selecciona un amigo para empezar a chatear."));
		return;
	}

	bool bSelectedFriendFound = false;
	for (int32 Index = 0; Index < FriendsList.Num(); ++Index)
	{
		const FArenaFriendEntry& Friend = FriendsList[Index];
		if (Friend.NetId.IsEmpty())
		{
			continue;
		}

		if (Friend.NetId == SelectedChatFriendId)
		{
			bSelectedFriendFound = true;
			SelectedChatFriendName = Friend.Name.ToString();
		}

		UArenaFriendActionHandler* Handler = NewObject<UArenaFriendActionHandler>(this);
		Handler->Action = 5;
		Handler->Index = Index;
		Handler->Owner = this;
		ChatActionHandlers.Add(Handler);

		UButton* FriendButton = MakeButton(FLinearColor::Transparent, FLinearColor::Transparent, FLinearColor::Transparent, 12.0f);
		FriendButton->SetStyle(ButtonStyle(Friend.NetId == SelectedChatFriendId ? 0.24f : 0.08f, 14.0f, 0.35f, Friend.NetId == SelectedChatFriendId ? Mint : FLinearColor::White));
		FriendButton->SetContent(MakeText(Friend.Name, BodyFontFace, 13, Ink, false));
		FriendButton->OnClicked.AddDynamic(Handler, &UArenaFriendActionHandler::HandleClicked);
		ChatFriendsListBox->AddChildToVerticalBox(FriendButton)->SetPadding(FMargin(0.0f, 2.0f));
	}

	if (!bSelectedFriendFound)
	{
		SelectedChatFriendId.Reset();
		SelectedChatFriendName.Reset();
	}

	if (SelectedChatFriendId.IsEmpty())
	{
		ChatConversationTitle->SetText(LOCTEXT("ChatNoConversation", "Selecciona un amigo para empezar a chatear."));
		return;
	}

	if (Friends->GetChatFriendId() == SelectedChatFriendId && !Friends->GetChatFriendName().IsEmpty())
	{
		SelectedChatFriendName = Friends->GetChatFriendName();
	}
	ChatConversationTitle->SetText(FText::Format(
		LOCTEXT("ChatConversationWith", "Conversación con {0}"),
		FText::FromString(SelectedChatFriendName)));

	if (Friends->GetChatFriendId() != SelectedChatFriendId)
	{
		ChatMessagesListBox->AddChildToVerticalBox(MakeText(
			LOCTEXT("ChatLoading", "Cargando conversación..."),
			BodyFontFace, 13, Dim, false));
		return;
	}

	for (const FArenaChatMessageInfo& Message : Friends->GetChatMessages())
	{
		const FString Prefix = Message.bMine ? TEXT("Tú: ") : SelectedChatFriendName + TEXT(": ");
		const FLinearColor Color = Message.bMine ? Mint : Ink;
		UTextBlock* MessageText = MakeText(
			FText::FromString(Prefix + Message.Body),
			BodyFontFace, 13, Color, false);
		MessageText->SetAutoWrapText(true);
		ChatMessagesListBox->AddChildToVerticalBox(MessageText)->SetPadding(FMargin(4.0f, 3.0f));
	}

	if (Friends->GetChatMessages().IsEmpty())
	{
		ChatMessagesListBox->AddChildToVerticalBox(MakeText(
			LOCTEXT("ChatEmptyConversation", "Todavía no hay mensajes. ¡Saluda!"),
			BodyFontFace, 13, Dim, false));
	}
}

void UArenaLobbyWidget::JoinFriendLobby(int32 Index)
{
	if (!FriendsList.IsValidIndex(Index) || FriendsList[Index].LobbyName.IsEmpty())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		ShowToast(LOCTEXT("FriendLobbyUnavailable", "NO SE PUDO CONECTAR CON EL SISTEMA DE PARTIDAS"));
		return;
	}

	ShowToast(FText::Format(
		LOCTEXT("JoiningFriendLobbyToast", "BUSCANDO EL LOBBY DE {0} EN LA RED LOCAL..."),
		FriendsList[Index].Name));
	Sessions->OnJoinReady.RemoveDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
	Sessions->OnJoinReady.AddDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
	bWaitingForLauncherInviteJoin = true;
	Sessions->JoinLANSessionByName(FriendsList[Index].LobbyName);
}

void UArenaLobbyWidget::AcceptLobbyInvite(int32 Index)
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends && LobbyInvitesList.IsValidIndex(Index))
	{
		const FArenaLobbyInviteEntry Invite = LobbyInvitesList[Index];
		if (Invite.bFromLauncher)
		{
			const FString LobbyName = Friends->AcceptLobbyInvite(Invite.SessionId);
			if (LobbyName.IsEmpty())
			{
				return;
			}
			ShowToast(FText::Format(LOCTEXT("JoiningLanLobbyToast", "Buscando en la red local el lobby de {0}..."), Invite.SenderName));
			if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
			{
				Sessions->OnJoinReady.RemoveDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
				Sessions->OnJoinReady.AddDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
				bWaitingForLauncherInviteJoin = true;
				Sessions->JoinLANSessionByName(LobbyName);
			}
			return;
		}

		ShowToast(FText::Format(LOCTEXT("JoiningLobbyToast", "Uniéndote al lobby de {0}..."), Invite.SenderName));
		Friends->AcceptLobbyInvite(Invite.SessionId);
	}
}

void UArenaLobbyWidget::HandleLauncherInviteJoinComplete(bool bSuccess)
{
	if (!bWaitingForLauncherInviteJoin)
	{
		return;
	}
	bWaitingForLauncherInviteJoin = false;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->OnJoinReady.RemoveDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
		}
	}

	if (!bSuccess)
	{
		ShowToast(LOCTEXT("LanInviteJoinFailed", "NO SE ENCONTRÓ EL LOBBY EN LA RED LOCAL. COMPRUEBA QUE EL ANFITRIÓN SIGUE EN PARTIDA."));
	}
}

void UArenaLobbyWidget::HandleLobbyInviteAcceptClicked()
{
	if (!bLobbyInviteNoticeVisible)
	{
		return;
	}

	const int32 InviteIndex = LobbyInvitesList.IndexOfByPredicate([this](const FArenaLobbyInviteEntry& Invite)
	{
		return Invite.SessionId == ActiveLobbyInviteNotice.SessionId;
	});
	if (InviteIndex != INDEX_NONE)
	{
		AcceptLobbyInvite(InviteIndex);
	}
	DismissLobbyInviteNotice();
}

void UArenaLobbyWidget::HandleLobbyInviteRejectClicked()
{
	if (!bLobbyInviteNoticeVisible)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends && Friends->DeclineLobbyInvite(ActiveLobbyInviteNotice.SessionId))
	{
		DismissLobbyInviteNotice();
	}
}

void UArenaLobbyWidget::QueueLobbyInviteNotice(const FArenaLobbyInviteEntry& Invite)
{
	if (Invite.SessionId.IsEmpty() || Invite.SessionId == ActiveLobbyInviteNotice.SessionId)
	{
		return;
	}
	if (QueuedLobbyInviteNotices.ContainsByPredicate([&Invite](const FArenaLobbyInviteEntry& Queued)
	{
		return Queued.SessionId == Invite.SessionId;
	}))
	{
		return;
	}
	QueuedLobbyInviteNotices.Add(Invite);
	ShowNextLobbyInviteNotice();
}

void UArenaLobbyWidget::ShowNextLobbyInviteNotice()
{
	if (bLobbyInviteNoticeVisible || QueuedLobbyInviteNotices.IsEmpty() || LobbyInviteNotice == nullptr)
	{
		return;
	}

	ActiveLobbyInviteNotice = QueuedLobbyInviteNotices[0];
	QueuedLobbyInviteNotices.RemoveAt(0);
	if (LobbyInviteSenderText)
	{
		LobbyInviteSenderText->SetText(ActiveLobbyInviteNotice.SenderName);
	}
	bLobbyInviteNoticeVisible = true;
	bLobbyInviteNoticeLeaving = false;
	bLobbyInviteNoticeEntering = true;
	LobbyInviteNoticeTimer = 10.0f;
	LobbyInviteNoticeSlide = -LobbyInviteNoticeOffscreenX;
	LobbyInviteNotice->SetRenderTranslation(FVector2D(LobbyInviteNoticeSlide, 0.0f));
	LobbyInviteNotice->SetRenderOpacity(0.0f);
	LobbyInviteNotice->SetVisibility(ESlateVisibility::Visible);
}

void UArenaLobbyWidget::DismissLobbyInviteNotice()
{
	if (bLobbyInviteNoticeVisible)
	{
		bLobbyInviteNoticeLeaving = true;
	}
}

void UArenaLobbyWidget::HandleAddFriendPrompt()
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends == nullptr)
	{
		return;
	}

	if (!Friends->IsAvailable())
	{
		ShowToast(LOCTEXT("FriendsNeedLauncher", "INICIA SESIÓN EN EL LAUNCHER DE ARENA PARA USAR LOS AMIGOS"));
		return;
	}

	Friends->SendFriendRequestByName(PendingFriendName);
	PendingFriendName.Reset();
	if (AddFriendInput)
	{
		AddFriendInput->SetText(FText::GetEmpty());
	}
}

void UArenaLobbyWidget::HandleFriendNameChanged(const FText& Text)
{
	PendingFriendName = Text.ToString();
}

void UArenaLobbyWidget::HandleFriendsMessage(const FString& Message)
{
	ShowToast(FText::FromString(Message.ToUpper()));
}

void UArenaLobbyWidget::HandleFriendsChanged()
{
	TSet<FString> PreviousInvites;
	for (const FArenaLobbyInviteEntry& Invite : LobbyInvitesList)
	{
		PreviousInvites.Add(Invite.SessionId);
	}
	TSet<FString> PreviousRequests;
	for (const FArenaFriendRequestEntry& Request : FriendRequestsList)
	{
		PreviousRequests.Add(Request.NetId);
	}

	SyncFromFriendsSubsystem();
	bool bReceivedNewSocialAlert = false;
	for (const FArenaLobbyInviteEntry& Invite : LobbyInvitesList)
	{
		if (!Invite.SessionId.IsEmpty() && !PreviousInvites.Contains(Invite.SessionId))
		{
			QueueLobbyInviteNotice(Invite);
			bReceivedNewSocialAlert = true;
		}
	}
	for (const FArenaFriendRequestEntry& Request : FriendRequestsList)
	{
		if (!Request.NetId.IsEmpty() && !PreviousRequests.Contains(Request.NetId))
		{
			bReceivedNewSocialAlert = true;
		}
	}
	if (bReceivedNewSocialAlert)
	{
		ArenaUISounds::PlayNotification(this);
	}
}

void UArenaLobbyWidget::HandleEpicLoginClicked()
{
	if (bEpicLoginPending)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr)
	{
		EpicLoginMessage = TEXT("No se pudo iniciar sesión con Epic: no está disponible el subsistema de sesiones.");
		RebuildAmigosContent();
		ShowToast(FText::FromString(EpicLoginMessage.ToUpper()));
		return;
	}

	Sessions->OnLoginComplete.RemoveDynamic(this, &UArenaLobbyWidget::HandleEpicLoginComplete);
	Sessions->OnLoginComplete.AddDynamic(this, &UArenaLobbyWidget::HandleEpicLoginComplete);
	bEpicLoginPending = true;
	EpicLoginMessage = TEXT("Solicitud de inicio de sesión enviada. Completa la autorización en el navegador; esperando respuesta de Epic.");
	RebuildAmigosContent();
	ShowToast(LOCTEXT("FriendsEpicLogin", "CONECTANDO CON EPIC GAMES... AUTORIZA EN EL NAVEGADOR"));
	Sessions->Login(TEXT("accountportal"));
}

void UArenaLobbyWidget::HandleEpicLoginComplete(bool bWasSuccessful, const FString& Error)
{
	bEpicLoginPending = false;

	UGameInstance* GameInstance = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions)
	{
		Sessions->OnLoginComplete.RemoveDynamic(this, &UArenaLobbyWidget::HandleEpicLoginComplete);
	}

	if (bWasSuccessful)
	{
		EpicLoginMessage = TEXT("Inicio de sesión con Epic completado correctamente.");
		ShowToast(LOCTEXT("FriendsEpicLoginSuccess", "SESIÓN DE EPIC INICIADA"));
		if (UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr)
		{
			Friends->RefreshFriends();
		}
	}
	else
	{
		EpicLoginMessage = Error.IsEmpty() ? TEXT("Error desconocido al iniciar sesión con Epic.") : Error;
		ShowToast(FText::Format(
			LOCTEXT("FriendsEpicLoginError", "NO SE PUDO INICIAR SESIÓN CON EPIC: {0}"),
			FText::FromString(EpicLoginMessage)));
	}

	SyncFromFriendsSubsystem();
	RebuildAmigosContent();
}

void UArenaLobbyWidget::HandleCopyEpicLoginMessage()
{
	if (EpicLoginMessage.IsEmpty())
	{
		return;
	}

	FPlatformApplicationMisc::ClipboardCopy(*EpicLoginMessage);
	ShowToast(LOCTEXT("FriendsEpicLoginMessageCopied", "MENSAJE DE EPIC COPIADO"));
}

void UArenaLobbyWidget::SyncFromFriendsSubsystem()
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (Friends == nullptr)
	{
		return;
	}

	FriendsList.Reset();
	for (const FArenaFriendInfo& Friend : Friends->GetFriends())
	{
		FArenaFriendEntry& Entry = FriendsList.AddDefaulted_GetRef();
		Entry.Name = FText::FromString(Friend.Name);
		Entry.Status = FText::FromString(Friend.Status);
		Entry.bOnline = Friend.bOnline;
		Entry.NetId = Friend.NetId;
		Entry.LobbyName = Friend.LobbyName;
		Entry.AvatarUrl = Friend.AvatarUrl;
	}

	FriendRequestsList.Reset();
	for (const FArenaFriendRequestInfo& Request : Friends->GetRequests())
	{
		FriendRequestsList.Add({ FText::FromString(Request.Name), Request.NetId });
	}

	LobbyInvitesList.Reset();
	for (const FArenaLobbyInviteInfo& Invite : Friends->GetLobbyInvites())
	{
		FArenaLobbyInviteEntry& Entry = LobbyInvitesList.AddDefaulted_GetRef();
		Entry.SenderName = FText::FromString(Invite.SenderName);
		Entry.SessionId = Invite.SenderNetId;
		Entry.LobbyName = Invite.LobbyName;
		Entry.bFromLauncher = Invite.bFromLauncher;
	}

	if (SelectedChatFriendId.IsEmpty() && !Friends->GetChatFriendId().IsEmpty())
	{
		SelectedChatFriendId = Friends->GetChatFriendId();
		SelectedChatFriendName = Friends->GetChatFriendName();
	}

	if (ActiveSidePanelTab == 2)
	{
		RebuildAmigosContent();
	}
	else if (ActiveSidePanelTab == 1)
	{
		RebuildChatContent();
	}
}

/** Sees the keys before any widget does: clicking an empty part of the lobby takes the keyboard focus away from the lobby widget, B must still work */
class FArenaLobbyKeyProcessor : public IInputProcessor
{
public:
	explicit FArenaLobbyKeyProcessor(UArenaLobbyWidget* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::B && !InKeyEvent.IsRepeat())
		{
			if (UArenaLobbyWidget* Lobby = Owner.Get()) { Lobby->HandleEmoteKey(true); }
		}
		return false;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::B)
		{
			if (UArenaLobbyWidget* Lobby = Owner.Get()) { Lobby->HandleEmoteKey(false); }
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("ArenaLobbyKeys"); }

private:
	TWeakObjectPtr<UArenaLobbyWidget> Owner;
};

void UArenaLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!bMatchSocialOverlay && FSlateApplication::IsInitialized() && !KeyProcessor.IsValid())
	{
		KeyProcessor = MakeShared<FArenaLobbyKeyProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(KeyProcessor);
	}
}

void UArenaLobbyWidget::HandleEmoteKey(bool bDown)
{
	if (bMatchSocialOverlay)
	{
		return;
	}
	if (bDown)
	{
		if (!bMapPageOpen && !bSidePanelOpen && !bServerBrowserOpen && IsVisible())
		{
			OpenEmoteWheel();
		}
	}
	else
	{
		CloseEmoteWheel();
	}
}

void UArenaLobbyWidget::NativeDestruct()
{
	if (KeyProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyProcessor);
	}
	KeyProcessor.Reset();
	CloseEmoteWheel();
	UGameInstance* GameInstance = GetGameInstance();
	if (UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr)
	{
		Sessions->OnLoginComplete.RemoveDynamic(this, &UArenaLobbyWidget::HandleEpicLoginComplete);
		Sessions->OnJoinReady.RemoveDynamic(this, &UArenaLobbyWidget::HandleLauncherInviteJoinComplete);
	}
	if (UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr)
	{
		Friends->OnFriendsChanged.RemoveAll(this);
		Friends->OnFriendsMessage.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UArenaLobbyWidget::HandleSidePanelSocial()
{
	SetSidePanelTab(0);
}

void UArenaLobbyWidget::ToggleSidePanel()
{
	SetSidePanelOpen(!bSidePanelOpen);
}

void UArenaLobbyWidget::HandleSidePanelChat()
{
	SetSidePanelTab(1);
}

void UArenaLobbyWidget::HandleSidePanelAmigos()
{
	SetSidePanelTab(2);
}

void UArenaLobbyWidget::SetSidePanelTab(int32 TabIndex)
{
	using namespace ArenaLobbyStyle;
	const int32 PreviousTab = ActiveSidePanelTab;
	ActiveSidePanelTab = TabIndex;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = GameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			if (PreviousTab == 1 && TabIndex != 1)
			{
				Friends->SetGameChatOpen(false);
			}
			else if (TabIndex == 1)
			{
				Friends->SetGameChatOpen(true);
			}
		}
	}

	for (int32 i = 0; i < SidePanelTabButtons.Num(); ++i)
	{
		const bool bSelected = (i == TabIndex);
		if (SidePanelTabButtons.IsValidIndex(i) && SidePanelTabButtons[i])
		{
			ArenaGlass::Style(SidePanelTabButtons[i], bSelected ? ArenaGlass::ButtonStyle(0.26f, 18.0f, 0.55f) : ArenaGlass::ButtonStyle(0.0f, 18.0f, 0.0f));
		}

		if (SidePanelTabTexts.IsValidIndex(i) && SidePanelTabTexts[i])
		{
			SidePanelTabTexts[i]->SetColorAndOpacity(FSlateColor(bSelected ? ArenaGlass::Ink : ArenaGlass::Dim));
		}
	}

	if (SocialViewContent)
	{
		SocialViewContent->SetVisibility(TabIndex == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (ChatViewContent)
	{
		ChatViewContent->SetVisibility(TabIndex == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (AmigosViewContent)
	{
		AmigosViewContent->SetVisibility(TabIndex == 2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (TabIndex == 2)
		{
			UGameInstance* GameInstance = GetGameInstance();
			if (UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr)
			{
				Friends->RefreshFriends();
			}
			RebuildAmigosContent();
		}
	}
}

void UArenaLobbyWidget::RebuildAmigosContent()
{
	using namespace ArenaLobbyStyle;
	if (AmigosViewContent == nullptr)
	{
		return;
	}

	AmigosViewContent->ClearChildren();
	FriendActionHandlers.Empty();
	FriendAvatarImages.Empty();
	FriendAvatarFallbacks.Empty();

	using namespace ArenaGlass;
	const FLinearColor SoftText = Dim;
	auto Wrap = [](UTextBlock* Block) { Block->SetAutoWrapText(true); return Block; };

	auto MakeCard = [&](UWidget* Content, const FMargin& CardPadding, float Radius)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(Surface(0.07f, FMath::Max(Radius, 16.0f), 0.22f));
		Card->SetPadding(CardPadding);
		Card->SetContent(Content);
		return Card;
	};

	auto MakeTextButton = [&](const FText& Label, const FLinearColor& Tint, int32 Size)
	{
		UButton* Button = MakeButton(Clear, Clear, Clear, 14.0f);
		Button->SetStyle(ButtonStyle(0.16f, 14.0f, 0.45f, Tint));
		UTextBlock* Caption = MakeText(Label, BodyFontFace, Size, Ink, false);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Caption)))
		{
			ButtonSlot->SetPadding(FMargin(12.0f, 6.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		return Button;
	};

	// --- Title ---
	AmigosViewContent->AddChildToVerticalBox(MakeText(LOCTEXT("AmigosTitle", "Amigos"), BodyFontFace, 32, Ink, false))->SetPadding(FMargin(4.0f, 16.0f, 0.0f, 10.0f));

	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
	UVerticalBoxSlot* ScrollSlot = AmigosViewContent->AddChildToVerticalBox(Scroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UVerticalBox* Inner = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(Inner);

	if (!EpicLoginMessage.IsEmpty())
	{
		UVerticalBox* ResultLines = WidgetTree->ConstructWidget<UVerticalBox>();
		ResultLines->AddChildToVerticalBox(MakeText(
			LOCTEXT("FriendsEpicLoginResultTitle", "Resultado del inicio de sesión"),
			BodyFontFace, 16, Amber, false));
		ResultLines->AddChildToVerticalBox(Wrap(MakeText(
			FText::FromString(EpicLoginMessage), BodyFontFace, 13, SoftText, false)))
			->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 8.0f));

		UButton* CopyButton = MakeTextButton(LOCTEXT("FriendsEpicLoginCopy", "COPIAR MENSAJE"), Amber, 13);
		CopyButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleCopyEpicLoginMessage);
		ResultLines->AddChildToVerticalBox(CopyButton);

		UBorder* ResultCard = MakeCard(ResultLines, FMargin(14.0f, 12.0f), 12.0f);
		ResultCard->SetBrush(Surface(0.14f, 18.0f, 0.60f, Amber, 1.4f));
		Inner->AddChildToVerticalBox(ResultCard)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	}

	// ── 0. LAUNCHER ACCOUNT ──
	{
		UGameInstance* GameInstance = GetGameInstance();
		UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
		if (Friends && !Friends->IsAvailable())
		{
			UVerticalBox* LoginLines = WidgetTree->ConstructWidget<UVerticalBox>();
			LoginLines->AddChildToVerticalBox(MakeText(LOCTEXT("FriendsLoginTitle", "Amigos del launcher"), BodyFontFace, 18, Amber, false));
			const FText LoginDescription = LOCTEXT("FriendsLoginBody", "Inicia sesión en el launcher de Arena para ver a tus amigos. Para jugar juntos, conecta ambos equipos a la misma red local.");
			LoginLines->AddChildToVerticalBox(Wrap(MakeText(LoginDescription, BodyFontFace, 13, SoftText, false)))->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 8.0f));
			UBorder* LoginCard = MakeCard(LoginLines, FMargin(14.0f, 12.0f), 12.0f);
			LoginCard->SetBrush(Surface(0.14f, 18.0f, 0.60f, Amber, 1.4f));
			Inner->AddChildToVerticalBox(LoginCard)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		}
	}

	// ── 1. LOBBY INVITATIONS ──
	if (LobbyInvitesList.Num() > 0)
	{
		UHorizontalBox* InviteHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
		InviteHeader->AddChildToHorizontalBox(MakeText(LOCTEXT("LobbyInvitesHdr", "Invitaciones de lobby"), BodyFontFace, 18, Amber, false))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		InviteHeader->AddChildToHorizontalBox(MakeText(FText::AsNumber(LobbyInvitesList.Num()), BodyFontFace, 18, Amber, false));
		Inner->AddChildToVerticalBox(InviteHeader)->SetPadding(FMargin(4.0f, 8.0f, 4.0f, 8.0f));

		for (int32 i = 0; i < LobbyInvitesList.Num(); ++i)
		{
			const FArenaLobbyInviteEntry& Invite = LobbyInvitesList[i];
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->AddChildToHorizontalBox(MakeAvatar(44.0f))->SetVerticalAlignment(VAlign_Center);

			UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
			Lines->AddChildToVerticalBox(MakeText(Invite.SenderName, BodyFontFace, 18, Ink, false));
			Lines->AddChildToVerticalBox(MakeText(LOCTEXT("InviteLobbySub", "¡Te invitó a unirte a su lobby!"), BodyFontFace, 13, Mint, false));
			UHorizontalBoxSlot* LinesSlot = Row->AddChildToHorizontalBox(Lines);
			LinesSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
			LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LinesSlot->SetVerticalAlignment(VAlign_Center);

			// UNIRSE button
			UArenaFriendActionHandler* JoinHandler = NewObject<UArenaFriendActionHandler>(this);
			JoinHandler->Action = 3;
			JoinHandler->Index = i;
			JoinHandler->Owner = this;
			FriendActionHandlers.Add(JoinHandler);

			UButton* JoinBtn = MakeTextButton(LOCTEXT("JoinLobbyBtn", "UNIRSE"), Mint, 14);
			JoinBtn->OnClicked.AddDynamic(JoinHandler, &UArenaFriendActionHandler::HandleClicked);
			Row->AddChildToHorizontalBox(JoinBtn)->SetVerticalAlignment(VAlign_Center);

			UBorder* InviteCard = MakeCard(Row, FMargin(12.0f, 8.0f), 12.0f);
			InviteCard->SetBrush(Surface(0.14f, 18.0f, 0.60f, Mint, 1.4f));
			Inner->AddChildToVerticalBox(InviteCard)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
		}
	}

	// ── 2. FRIEND REQUESTS ──
	UHorizontalBox* ReqHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	ReqHeader->AddChildToHorizontalBox(MakeText(LOCTEXT("ReqHeader", "Peticiones de amistad"), BodyFontFace, 18, Ink, false))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ReqHeader->AddChildToHorizontalBox(MakeText(FText::AsNumber(FriendRequestsList.Num()), BodyFontFace, 18, SoftText, false));
	Inner->AddChildToVerticalBox(ReqHeader)->SetPadding(FMargin(4.0f, 12.0f, 4.0f, 8.0f));

	if (FriendRequestsList.Num() == 0)
	{
		UBorder* EmptyCard = MakeCard(Wrap(MakeText(LOCTEXT("NoRequests", "No tienes peticiones de amistad pendientes."), BodyFontFace, 14, SoftText, false)), FMargin(14.0f, 10.0f), 10.0f);
		Inner->AddChildToVerticalBox(EmptyCard)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	else
	{
		for (int32 i = 0; i < FriendRequestsList.Num(); ++i)
		{
			const FArenaFriendRequestEntry& Req = FriendRequestsList[i];
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
			Row->AddChildToHorizontalBox(MakeAvatar(40.0f))->SetVerticalAlignment(VAlign_Center);

			UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
			Lines->AddChildToVerticalBox(MakeText(Req.RequesterName, BodyFontFace, 17, Ink, false));
			Lines->AddChildToVerticalBox(MakeText(LOCTEXT("ReqSentSub", "Te envió una solicitud"), BodyFontFace, 13, SoftText, false));
			UHorizontalBoxSlot* LinesSlot = Row->AddChildToHorizontalBox(Lines);
			LinesSlot->SetPadding(FMargin(10.0f, 0.0f, 6.0f, 0.0f));
			LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LinesSlot->SetVerticalAlignment(VAlign_Center);

			// Accept button
			UArenaFriendActionHandler* AcceptHandler = NewObject<UArenaFriendActionHandler>(this);
			AcceptHandler->Action = 0;
			AcceptHandler->Index = i;
			AcceptHandler->Owner = this;
			FriendActionHandlers.Add(AcceptHandler);

			UButton* AcceptBtn = MakeTextButton(LOCTEXT("AcceptBtn", "Aceptar"), Mint, 13);
			AcceptBtn->OnClicked.AddDynamic(AcceptHandler, &UArenaFriendActionHandler::HandleClicked);
			Row->AddChildToHorizontalBox(AcceptBtn)->SetVerticalAlignment(VAlign_Center);

			// Decline button
			UArenaFriendActionHandler* DeclineHandler = NewObject<UArenaFriendActionHandler>(this);
			DeclineHandler->Action = 1;
			DeclineHandler->Index = i;
			DeclineHandler->Owner = this;
			FriendActionHandlers.Add(DeclineHandler);

			UButton* DeclineBtn = MakeTextButton(LOCTEXT("DeclineBtn", "Rechazar"), Coral, 13);
			DeclineBtn->OnClicked.AddDynamic(DeclineHandler, &UArenaFriendActionHandler::HandleClicked);
			UHorizontalBoxSlot* DeclineSlot = Row->AddChildToHorizontalBox(DeclineBtn);
			DeclineSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
			DeclineSlot->SetVerticalAlignment(VAlign_Center);

			Inner->AddChildToVerticalBox(MakeCard(Row, FMargin(10.0f, 8.0f), 10.0f))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}
	}

	// ── 3. ADD FRIEND (Arena display name) ──
	AddFriendInput = WidgetTree->ConstructWidget<UEditableTextBox>();
	{
		FEditableTextBoxStyle InputStyle = AddFriendInput->WidgetStyle;
		const FSlateRoundedBoxBrush Inset = Surface(0.30f, 14.0f, 0.22f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
		InputStyle.SetBackgroundImageNormal(Inset);
		InputStyle.SetBackgroundImageHovered(Inset);
		InputStyle.SetBackgroundImageFocused(Surface(0.34f, 14.0f, 0.60f, Ice, 1.4f));
		InputStyle.SetBackgroundImageReadOnly(Inset);
		InputStyle.SetForegroundColor(FSlateColor(Ink));
		InputStyle.SetFocusedForegroundColor(FSlateColor(Ink));
		InputStyle.SetPadding(FMargin(14.0f, 10.0f));
		AddFriendInput->WidgetStyle = InputStyle;
	}
	AddFriendInput->SetHintText(LOCTEXT("AddFriendHint", "Nombre del jugador de Arena"));
	AddFriendInput->SetText(FText::FromString(PendingFriendName));
	AddFriendInput->OnTextChanged.AddDynamic(this, &UArenaLobbyWidget::HandleFriendNameChanged);
	Inner->AddChildToVerticalBox(AddFriendInput)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 6.0f));

	UButton* AddFriendBtn = MakeTextButton(LOCTEXT("AddFriendBtn", "+ Enviar petición de amistad"), Ice, 14);
	AddFriendBtn->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleAddFriendPrompt);
	Inner->AddChildToVerticalBox(AddFriendBtn)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));

	// ── 4. FRIENDS LIST ──
	int32 OnlineCount = 0;
	for (const FArenaFriendEntry& F : FriendsList)
	{
		if (F.bOnline) { ++OnlineCount; }
	}

	UHorizontalBox* FriendsHeader = WidgetTree->ConstructWidget<UHorizontalBox>();
	FriendsHeader->AddChildToHorizontalBox(MakeText(LOCTEXT("FriendsHeader", "Amigos"), BodyFontFace, 18, Ink, false))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FriendsHeader->AddChildToHorizontalBox(MakeText(
		FText::Format(LOCTEXT("FriendsCount", "{0} en línea · {1} total"), FText::AsNumber(OnlineCount), FText::AsNumber(FriendsList.Num())),
		BodyFontFace, 12, SoftText, false));
	Inner->AddChildToVerticalBox(FriendsHeader)->SetPadding(FMargin(4.0f, 8.0f, 4.0f, 8.0f));

	if (FriendsList.Num() == 0)
	{
		UGameInstance* GameInstance = GetGameInstance();
		UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
		const bool bFriendsAvailable = Friends && Friends->IsAvailable();
		const FText EmptyMessage = bFriendsAvailable
			? LOCTEXT("NoFriends", "No tienes amigos añadidos todavía.")
			: LOCTEXT("FriendsLoginRequired", "Inicia sesión en el launcher de Arena y vuelve a abrir el panel para cargar tus amigos.");
		UBorder* EmptyFriendsCard = MakeCard(Wrap(MakeText(EmptyMessage, BodyFontFace, 14, SoftText, false)), FMargin(14.0f, 10.0f), 10.0f);
		Inner->AddChildToVerticalBox(EmptyFriendsCard)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	}
	else
	{
		for (int32 i = 0; i < FriendsList.Num(); ++i)
		{
			const FArenaFriendEntry& Friend = FriendsList[i];
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();

			UOverlay* Avatar = WidgetTree->ConstructWidget<UOverlay>();
			UBorder* AvatarBackground = WidgetTree->ConstructWidget<UBorder>();
			AvatarBackground->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.18f, 0.30f, 0.52f, 1.0f), 22.0f));
			Avatar->AddChildToOverlay(AvatarBackground);
			UImage* AvatarImage = WidgetTree->ConstructWidget<UImage>();
			FSlateBrush AvatarBrush;
			AvatarBrush.ImageSize = FVector2D(44.0f, 44.0f);
			AvatarBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
			AvatarBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
			AvatarImage->SetBrush(AvatarBrush);
			AvatarImage->SetVisibility(ESlateVisibility::Collapsed);
			Avatar->AddChildToOverlay(AvatarImage)->SetHorizontalAlignment(HAlign_Fill);
			UTextBlock* AvatarFallback = MakeText(
				Friend.Name.IsEmpty() ? FText::FromString(TEXT("?")) : FText::FromString(Friend.Name.ToString().Left(1).ToUpper()),
				BodyFontFace, 17, Ink, false);
			AvatarFallback->SetJustification(ETextJustify::Center);
			UOverlaySlot* FallbackSlot = Avatar->AddChildToOverlay(AvatarFallback);
			FallbackSlot->SetHorizontalAlignment(HAlign_Fill);
			FallbackSlot->SetVerticalAlignment(VAlign_Center);
			USizeBox* AvatarSize = WidgetTree->ConstructWidget<USizeBox>();
			AvatarSize->SetWidthOverride(44.0f);
			AvatarSize->SetHeightOverride(44.0f);
			AvatarSize->SetContent(Avatar);
			Row->AddChildToHorizontalBox(AvatarSize)->SetVerticalAlignment(VAlign_Center);
			FriendAvatarImages.Add(Friend.NetId, AvatarImage);
			FriendAvatarFallbacks.Add(Friend.NetId, AvatarFallback);

			if (TObjectPtr<UTexture2D>* CachedAvatar = FriendAvatarTextures.Find(Friend.AvatarUrl))
			{
				AvatarBrush.SetResourceObject(*CachedAvatar);
				AvatarImage->SetBrush(AvatarBrush);
				AvatarImage->SetVisibility(ESlateVisibility::Visible);
				AvatarFallback->SetVisibility(ESlateVisibility::Collapsed);
			}
			else if (!Friend.AvatarUrl.IsEmpty())
			{
				RequestFriendAvatar(Friend.NetId, Friend.AvatarUrl);
			}

			UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
			Lines->AddChildToVerticalBox(MakeText(Friend.Name, BodyFontFace, 18, Ink, false));

			UHorizontalBox* StatLine = WidgetTree->ConstructWidget<UHorizontalBox>();
			UBorder* Dot = WidgetTree->ConstructWidget<UBorder>();
			Dot->SetBrush(FSlateRoundedBoxBrush(Friend.bOnline ? Mint : FLinearColor(1.0f, 1.0f, 1.0f, 0.30f), 4.0f));
			Dot->SetPadding(FMargin(4.0f));
			StatLine->AddChildToHorizontalBox(Dot)->SetVerticalAlignment(VAlign_Center);
			UHorizontalBoxSlot* StatTextSlot = StatLine->AddChildToHorizontalBox(MakeText(Friend.Status, BodyFontFace, 13, SoftText, false));
			StatTextSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
			StatTextSlot->SetVerticalAlignment(VAlign_Center);
			Lines->AddChildToVerticalBox(StatLine);

			UHorizontalBoxSlot* LinesSlot = Row->AddChildToHorizontalBox(Lines);
			LinesSlot->SetPadding(FMargin(10.0f, 0.0f, 6.0f, 0.0f));
			LinesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LinesSlot->SetVerticalAlignment(VAlign_Center);

			const bool bFriendHasLobby = Friend.bOnline && !Friend.LobbyName.IsEmpty();
			const int32 Action = bFriendHasLobby ? 4 : 2;
			UArenaFriendActionHandler* LobbyActionHandler = NewObject<UArenaFriendActionHandler>(this);
			LobbyActionHandler->Action = Action;
			LobbyActionHandler->Index = i;
			LobbyActionHandler->Owner = this;
			FriendActionHandlers.Add(LobbyActionHandler);

			UButton* LobbyActionButton = MakeTextButton(
				bFriendHasLobby
					? LOCTEXT("JoinFriendLobbyBtnText", "UNIRSE AL LOBBY")
					: LOCTEXT("InviteLobbyBtnText", "INVITAR AL LOBBY"),
				Ice,
				11);
			LobbyActionButton->OnClicked.AddDynamic(LobbyActionHandler, &UArenaFriendActionHandler::HandleClicked);
			// Keep the invite action clickable: the session can start after this panel was built.
			// InviteToLobby validates the current LAN lobby at click time and shows the reason if unavailable.
			USizeBox* ActionSize = WidgetTree->ConstructWidget<USizeBox>();
			ActionSize->SetWidthOverride(132.0f);
			ActionSize->SetContent(LobbyActionButton);
			Row->AddChildToHorizontalBox(ActionSize)->SetVerticalAlignment(VAlign_Center);

			Inner->AddChildToVerticalBox(MakeCard(Row, FMargin(12.0f, 8.0f), 12.0f))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		}
	}
}

void UArenaLobbyWidget::RequestFriendAvatar(const FString& NetId, const FString& AvatarUrl)
{
	const FString AllowedPrefix = TEXT("https://pub-bc20b0803ed843a68f8414d2139f22db.r2.dev/avatars/");
	if (!AvatarUrl.StartsWith(AllowedPrefix, ESearchCase::CaseSensitive)
		|| !AvatarUrl.Contains(TEXT("/avatar.png"), ESearchCase::CaseSensitive))
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping untrusted friend avatar URL for %s"), *NetId);
		return;
	}
	if (PendingFriendAvatarUrls.Contains(AvatarUrl))
	{
		return;
	}

	PendingFriendAvatarUrls.Add(AvatarUrl);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(AvatarUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(10.0f);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, NetId, AvatarUrl](FHttpRequestPtr /*CompletedRequest*/, FHttpResponsePtr Response, bool bWasSuccessful)
		{
			PendingFriendAvatarUrls.Remove(AvatarUrl);
			if (!bWasSuccessful || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("Could not download friend avatar for %s"), *NetId);
				return;
			}

			const TArray<uint8>& Bytes = Response->GetContent();
			if (Bytes.IsEmpty() || Bytes.Num() > 200 * 1024)
			{
				UE_LOG(LogTemp, Warning, TEXT("Friend avatar response has an invalid size for %s"), *NetId);
				return;
			}

			UTexture2D* Texture = FImageUtils::ImportBufferAsTexture2D(Bytes);
			if (Texture == nullptr)
			{
				UE_LOG(LogTemp, Warning, TEXT("Could not decode friend avatar for %s"), *NetId);
				return;
			}
			FriendAvatarTextures.Add(AvatarUrl, Texture);

			if (TObjectPtr<UImage>* Image = FriendAvatarImages.Find(NetId))
			{
				FSlateBrush AvatarBrush = (*Image)->GetBrush();
				AvatarBrush.SetResourceObject(Texture);
				AvatarBrush.ImageSize = FVector2D(44.0f, 44.0f);
				AvatarBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
				AvatarBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
				(*Image)->SetBrush(AvatarBrush);
				(*Image)->SetVisibility(ESlateVisibility::Visible);
				if (TObjectPtr<UTextBlock>* Fallback = FriendAvatarFallbacks.Find(NetId))
				{
					(*Fallback)->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		});
	if (!Request->ProcessRequest())
	{
		PendingFriendAvatarUrls.Remove(AvatarUrl);
		UE_LOG(LogTemp, Warning, TEXT("Could not start friend avatar request for %s"), *NetId);
	}
}

void UArenaLobbyWidget::BuildSidePanel(UCanvasPanel* Root)
{
	using namespace ArenaLobbyStyle;
	if (Root == nullptr)
	{
		return;
	}
	InitDefaultSocialData();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = GameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->OnFriendsChanged.RemoveAll(this);
			Friends->OnFriendsChanged.AddDynamic(this, &UArenaLobbyWidget::HandleFriendsChanged);
			Friends->OnFriendsMessage.RemoveAll(this);
			Friends->OnFriendsMessage.AddDynamic(this, &UArenaLobbyWidget::HandleFriendsMessage);
			Friends->SetMyPresence(bMatchSocialOverlay ? TEXT("En partida") : TEXT("En el lobby"));
			SyncFromFriendsSubsystem();
			Friends->RefreshFriends();
		}
	}

	using namespace ArenaGlass;
	const FLinearColor CardColor(1.0f, 1.0f, 1.0f, 0.07f);
	const FLinearColor CardHover(1.0f, 1.0f, 1.0f, 0.14f);
	const FLinearColor SoftText = Dim;

	// Dim the lobby a little behind the panel; clicking it closes the panel
	const FLinearColor ShadeColor(0.016f, 0.024f, 0.063f, 0.38f);
	UButton* Shade = MakeButton(ShadeColor, ShadeColor, ShadeColor, 0.0f);
	Shade->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelClose);
	Shade->SetVisibility(ESlateVisibility::Collapsed);
	Shade->SetRenderOpacity(0.0f);
	AddToCanvas(Root, Shade, FAnchors(0, 0, 1, 1), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector);
	SidePanelShade = Shade;

	// The panel is a stack: blur of whatever is behind it, then the glass surface and its content
	UBorder* SidePanelBox = WidgetTree->ConstructWidget<UBorder>();
	SidePanelBox->SetBrush(FSlateColorBrush(FLinearColor::Transparent));
	SidePanelBox->SetPadding(FMargin(0.0f));
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(SidePanelBox);
	PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
	PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	PanelSlot->SetOffsets(FMargin(-16.0f, 16.0f, 480.0f, 16.0f));
	SidePanelBox->SetRenderTranslation(FVector2D(640.0f, 0.0f));
	SidePanelBox->SetVisibility(ESlateVisibility::Collapsed);
	SidePanel = SidePanelBox;

	UOverlay* PanelStack = WidgetTree->ConstructWidget<UOverlay>();
	SidePanelBox->SetContent(PanelStack);

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(32.0f);
	Blur->SetApplyAlphaToBlur(false);
	Blur->SetCornerRadius(FVector4(11.0f, 11.0f, 11.0f, 11.0f));
	SidePanelBlur = Blur;
	UOverlaySlot* BlurSlot = PanelStack->AddChildToOverlay(Blur);
	BlurSlot->SetHorizontalAlignment(HAlign_Fill);
	BlurSlot->SetVerticalAlignment(VAlign_Fill);

	UBorder* GlassSurface = WidgetTree->ConstructWidget<UBorder>();
	GlassSurface->SetBrush(Surface(0.10f, 22.0f, 0.42f, FLinearColor::White, 1.4f));
	GlassSurface->SetPadding(FMargin(22.0f, 16.0f));
	Blur->SetContent(GlassSurface);

	// Specular streak along the top edge
	{
		USizeBox* StreakBox = WidgetTree->ConstructWidget<USizeBox>();
		StreakBox->SetHeightOverride(1.6f);
		UBorder* Streak = WidgetTree->ConstructWidget<UBorder>();
		Streak->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.55f), 1.0f));
		StreakBox->SetContent(Streak);
		StreakBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* StreakSlot = PanelStack->AddChildToOverlay(StreakBox);
		StreakSlot->SetHorizontalAlignment(HAlign_Fill);
		StreakSlot->SetVerticalAlignment(VAlign_Top);
		StreakSlot->SetPadding(FMargin(40.0f, 1.0f, 40.0f, 0.0f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	GlassSurface->SetContent(Column);

	auto MakeCard = [&](UWidget* Content, const FMargin& CardPadding, float Radius)
	{
		UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
		Card->SetBrush(Surface(0.07f, FMath::Max(Radius, 16.0f), 0.22f));
		Card->SetPadding(CardPadding);
		Card->SetContent(Content);
		return Card;
	};
	auto MakeTextButton = [&](const FText& Label, const FLinearColor& Tint, int32 Size, float Fill = 0.16f)
	{
		UButton* Button = MakeButton(Clear, Clear, Clear, 14.0f);
		Button->SetStyle(ButtonStyle(Fill, 16.0f, 0.55f, Tint));
		UTextBlock* Caption = MakeText(Label, BodyFontFace, Size, Ink, false);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(Caption)))
		{
			ButtonSlot->SetPadding(FMargin(14.0f, 8.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
		return Button;
	};

	// --- Header: avatar + a glass pill with the sections [SOCIAL] [CHAT] [AMIGOS] [AJUSTES] and a close button ---
	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	Header->AddChildToHorizontalBox(MakeAvatar(36.0f))->SetVerticalAlignment(VAlign_Center);

	SidePanelTabButtons.Empty();
	SidePanelTabTexts.Empty();

	struct FHeaderButton { FText Label; int32 Action; };
	const FHeaderButton HeaderButtons[] = {
		{ LOCTEXT("PanelSocial", "SOCIAL"), 0 },
		{ LOCTEXT("PanelChat", "CHAT"), 1 },
		{ LOCTEXT("PanelAmigos", "AMIGOS"), 2 },
		{ LOCTEXT("PanelSettings", "AJUSTES"), 3 },
		{ LOCTEXT("PanelClose", "X"), 4 },
	};

	UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(HeaderButtons); ++Index)
	{
		const FHeaderButton& Entry = HeaderButtons[Index];
		const bool bSelected = (Index == 0);
		UButton* Button = MakeButton(Clear, Clear, Clear, 14.0f);
		Button->SetStyle(bSelected ? ButtonStyle(0.26f, 18.0f, 0.55f) : ButtonStyle(0.0f, 18.0f, 0.0f));
		UTextBlock* TabText = MakeText(Entry.Label, BodyFontFace, 13, bSelected ? Ink : Dim, false);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->SetContent(TabText)))
		{
			ButtonSlot->SetPadding(FMargin(Entry.Action == 4 ? 10.0f : 8.0f, 6.0f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (Entry.Action == 0) { Button->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelSocial); }
		else if (Entry.Action == 1) { Button->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelChat); }
		else if (Entry.Action == 2) { Button->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelAmigos); }
		else if (Entry.Action == 3) { Button->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelSettings); }
		else if (Entry.Action == 4) { Button->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelClose); }

		if (Index <= 2)
		{
			SidePanelTabButtons.Add(Button);
			SidePanelTabTexts.Add(TabText);
		}

		UHorizontalBoxSlot* ButtonSlot = TabRow->AddChildToHorizontalBox(Button);
		ButtonSlot->SetPadding(FMargin(Index == 0 ? 0.0f : 2.0f, 0.0f, 0.0f, 0.0f));
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
	UBorder* TabTrack = WidgetTree->ConstructWidget<UBorder>();
	TabTrack->SetBrush(Surface(0.22f, 24.0f, 0.22f, FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)));
	TabTrack->SetPadding(FMargin(3.0f));
	TabTrack->SetContent(TabRow);
	UHorizontalBoxSlot* TrackSlot = Header->AddChildToHorizontalBox(TabTrack);
	TrackSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
	TrackSlot->SetVerticalAlignment(VAlign_Center);

	Column->AddChildToVerticalBox(Header);

	// Section label in the launcher's style: small, uppercase, dim, with the count on the right
	auto MakeSection = [&](const FText& Label, const FText& Count)
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>();
		Line->AddChildToHorizontalBox(MakeText(FText::FromString(Label.ToString().ToUpper()), BodyFontFace, 14, Dim, false))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (!Count.IsEmpty())
		{
			Line->AddChildToHorizontalBox(MakeText(Count, BodyFontFace, 14, Dim, false));
		}
		return Line;
	};

	// ── VIEW 0: SOCIAL ──
	SocialViewContent = WidgetTree->ConstructWidget<UVerticalBox>();
	Column->AddChildToVerticalBox(SocialViewContent);
	{
		SocialViewContent->AddChildToVerticalBox(MakeText(LOCTEXT("SocialTitle", "Social"), BodyFontFace, 32, Ink, false))->SetPadding(FMargin(4.0f, 20.0f, 0.0f, 6.0f));

		SocialViewContent->AddChildToVerticalBox(MakeSection(LOCTEXT("YouSection", "Tú"), FText::GetEmpty()))->SetPadding(FMargin(4.0f, 12.0f, 4.0f, 8.0f));

		UHorizontalBox* Member = WidgetTree->ConstructWidget<UHorizontalBox>();
		Member->AddChildToHorizontalBox(MakeAvatar(48.0f))->SetVerticalAlignment(VAlign_Center);
		UVerticalBox* MemberLines = WidgetTree->ConstructWidget<UVerticalBox>();
		UHorizontalBox* NameLine = WidgetTree->ConstructWidget<UHorizontalBox>();
		NameLine->AddChildToHorizontalBox(MakeText(PlayerName, BodyFontFace, 20, Ink, false))->SetVerticalAlignment(VAlign_Center);
		UBorder* YouBadge = WidgetTree->ConstructWidget<UBorder>();
		YouBadge->SetBrush(Surface(0.22f, 10.0f, 0.60f, Mint));
		YouBadge->SetPadding(FMargin(9.0f, 1.0f));
		YouBadge->SetContent(MakeText(LOCTEXT("You", "TÚ"), BodyFontFace, 13, Ink, false));
		UHorizontalBoxSlot* BadgeSlot = NameLine->AddChildToHorizontalBox(YouBadge);
		BadgeSlot->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
		MemberLines->AddChildToVerticalBox(NameLine);
		MemberLines->AddChildToVerticalBox(MakeText(FText::Format(LOCTEXT("MemberStatus", "{0}: en el lobby"), ModeName), BodyFontFace, 14, Dim, false));
		UHorizontalBoxSlot* LinesSlot = Member->AddChildToHorizontalBox(MemberLines);
		LinesSlot->SetPadding(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
		LinesSlot->SetVerticalAlignment(VAlign_Center);
		SocialViewContent->AddChildToVerticalBox(MakeCard(Member, FMargin(12.0f, 10.0f), 18.0f));

		SocialViewContent->AddChildToVerticalBox(MakeSection(LOCTEXT("ChatsSection", "Chats"), FText::GetEmpty()))->SetPadding(FMargin(4.0f, 22.0f, 4.0f, 8.0f));
		UHorizontalBox* Chats = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto AddChatCard = [&](const FText& Title, const FText& Sub, bool bActive)
		{
			UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>();
			Lines->AddChildToVerticalBox(MakeText(Title, BodyFontFace, 17, Ink, false));
			Lines->AddChildToVerticalBox(MakeText(Sub, BodyFontFace, 13, Dim, false));
			UBorder* Card = MakeCard(Lines, FMargin(16.0f, 11.0f), 18.0f);
			if (bActive)
			{
				Card->SetBrush(Surface(0.16f, 18.0f, 0.60f, Ice, 1.4f));
			}
			UHorizontalBoxSlot* CardSlot = Chats->AddChildToHorizontalBox(Card);
			CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CardSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		};
		AddChatCard(LOCTEXT("PartyChat", "Chat de grupo"), LOCTEXT("PartyChatSub", "Solo tú"), false);
		AddChatCard(LOCTEXT("GameChat", "Chat de partida"), LOCTEXT("GameChatSub", "1 miembro"), true);
		SocialViewContent->AddChildToVerticalBox(Chats);

		UTextBlock* VoiceHint = MakeText(LOCTEXT("VoiceOff", "El chat de voz está desactivado en tus ajustes"), BodyFontFace, 14, Dim, false);
		VoiceHint->SetAutoWrapText(true);
		SocialViewContent->AddChildToVerticalBox(VoiceHint)->SetPadding(FMargin(4.0f, 16.0f, 4.0f, 10.0f));

		UButton* SettingsButton = MakeTextButton(LOCTEXT("OpenSettings", "Abrir configuración"), FLinearColor::White, 16);
		SettingsButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleSidePanelSettings);
		SocialViewContent->AddChildToVerticalBox(SettingsButton);

		SocialViewContent->AddChildToVerticalBox(MakeSection(LOCTEXT("Online", "Amigos en línea"), FText::AsNumber(FriendsList.Num())))->SetPadding(FMargin(4.0f, 24.0f, 4.0f, 0.0f));

		UVerticalBoxSlot* Filler = SocialViewContent->AddChildToVerticalBox(WidgetTree->ConstructWidget<USizeBox>());
		Filler->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		if (bMatchSocialOverlay)
		{
			const FLinearColor DangerRed(0.92f, 0.08f, 0.12f);
			UButton* LeaveMatchButton = MakeTextButton(LOCTEXT("LeaveMatch", "Abandonar partida"), DangerRed, 16, 0.76f);
			LeaveMatchButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleQuitGame);
			SocialViewContent->AddChildToVerticalBox(LeaveMatchButton);
		}
		else
		{
			const FLinearColor DangerRed(0.82f, 0.10f, 0.12f);
			UButton* ExitGameButton = MakeTextButton(LOCTEXT("ExitGame", "Salir del juego"), DangerRed, 16, 0.76f);
			ExitGameButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleQuitGame);
			SocialViewContent->AddChildToVerticalBox(ExitGameButton);
		}
	}

	// ── VIEW 1: CHAT ──
	ChatViewContent = WidgetTree->ConstructWidget<UVerticalBox>();
	ChatViewContent->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(ChatViewContent);
	{
		ChatViewContent->AddChildToVerticalBox(MakeText(LOCTEXT("ChatTitle", "Chat"), BodyFontFace, 32, Ink, false))->SetPadding(FMargin(4.0f, 16.0f, 0.0f, 8.0f));
		ChatViewContent->AddChildToVerticalBox(MakeText(LOCTEXT("ChatSelectFriend", "ELIGE UN AMIGO"), BodyFontFace, 13, Dim, false))->SetPadding(FMargin(4.0f, 4.0f, 0.0f, 4.0f));
		UScrollBox* FriendScroll = WidgetTree->ConstructWidget<UScrollBox>();
		FriendScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		FriendScroll->SetAllowOverscroll(false);
		ChatFriendsListBox = WidgetTree->ConstructWidget<UVerticalBox>();
		FriendScroll->AddChild(ChatFriendsListBox);
		USizeBox* FriendListSize = WidgetTree->ConstructWidget<USizeBox>();
		FriendListSize->SetHeightOverride(170.0f);
		FriendListSize->SetContent(FriendScroll);
		ChatViewContent->AddChildToVerticalBox(FriendListSize);

		ChatConversationTitle = MakeText(LOCTEXT("ChatNoConversation", "Selecciona un amigo para empezar a chatear."), BodyFontFace, 14, Dim, false);
		ChatViewContent->AddChildToVerticalBox(ChatConversationTitle)->SetPadding(FMargin(4.0f, 10.0f, 0.0f, 6.0f));
		UScrollBox* MessageScroll = WidgetTree->ConstructWidget<UScrollBox>();
		MessageScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
		MessageScroll->SetAllowOverscroll(false);
		ChatMessagesListBox = WidgetTree->ConstructWidget<UVerticalBox>();
		MessageScroll->AddChild(ChatMessagesListBox);
		ChatViewContent->AddChildToVerticalBox(MessageScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		ChatMessageInput = WidgetTree->ConstructWidget<UEditableTextBox>();
		ChatMessageInput->SetHintText(LOCTEXT("ChatMessageHint", "Escribe un mensaje..."));
		ChatMessageInput->OnTextCommitted.AddDynamic(this, &UArenaLobbyWidget::HandleChatInputCommitted);
		ChatViewContent->AddChildToVerticalBox(ChatMessageInput)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 4.0f));
		UButton* SendChatButton = MakeTextButton(LOCTEXT("SendChatButton", "ENVIAR"), Ice, 14);
		SendChatButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleChatSendClicked);
		ChatViewContent->AddChildToVerticalBox(SendChatButton);
	}
	RebuildChatContent();

	// ── VIEW 2: AMIGOS ──
	AmigosViewContent = WidgetTree->ConstructWidget<UVerticalBox>();
	AmigosViewContent->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(AmigosViewContent);

	RebuildAmigosContent();
	UpdateAvatars();

	// Keep incoming invitations actionable without opening the social panel automatically.
	UVerticalBox* InviteNoticeContent = WidgetTree->ConstructWidget<UVerticalBox>();
	InviteNoticeContent->AddChildToVerticalBox(MakeText(LOCTEXT("LobbyInviteNoticeTitle", "INVITACIÓN AL LOBBY"), BodyFontFace, 11, Ice, false))
		->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	LobbyInviteSenderText = MakeText(FText::GetEmpty(), BodyFontFace, 20, Ink, false);
	InviteNoticeContent->AddChildToVerticalBox(LobbyInviteSenderText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UHorizontalBox* InviteNoticeButtons = WidgetTree->ConstructWidget<UHorizontalBox>();
	UButton* RejectInviteButton = MakeTextButton(LOCTEXT("RejectLobbyInviteNotice", "RECHAZAR"), FLinearColor(1.0f, 0.46f, 0.55f), 13, 0.20f);
	RejectInviteButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleLobbyInviteRejectClicked);
	UHorizontalBoxSlot* RejectInviteSlot = InviteNoticeButtons->AddChildToHorizontalBox(RejectInviteButton);
	RejectInviteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RejectInviteSlot->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
	UButton* AcceptInviteButton = MakeTextButton(LOCTEXT("AcceptLobbyInviteNotice", "ACEPTAR"), Ice, 13, 0.34f);
	AcceptInviteButton->OnClicked.AddDynamic(this, &UArenaLobbyWidget::HandleLobbyInviteAcceptClicked);
	UHorizontalBoxSlot* AcceptInviteSlot = InviteNoticeButtons->AddChildToHorizontalBox(AcceptInviteButton);
	AcceptInviteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AcceptInviteSlot->SetPadding(FMargin(5.0f, 0.0f, 0.0f, 0.0f));
	InviteNoticeContent->AddChildToVerticalBox(InviteNoticeButtons);

	LobbyInviteNotice = WrapGlass(InviteNoticeContent, FMargin(18.0f, 14.0f), 24.0f, 0.15f, 0.55f, FLinearColor(0.78f, 0.92f, 1.0f));
	UCanvasPanelSlot* InviteNoticeSlot = Root->AddChildToCanvas(LobbyInviteNotice);
	InviteNoticeSlot->SetAnchors(FAnchors(0.0f, 0.0f));
	InviteNoticeSlot->SetOffsets(FMargin(24.0f, 86.0f, 390.0f, 150.0f));
	InviteNoticeSlot->SetZOrder(100);
	LobbyInviteNotice->SetRenderTranslation(FVector2D(-LobbyInviteNoticeOffscreenX, 0.0f));
	LobbyInviteNotice->SetRenderOpacity(0.0f);
	LobbyInviteNotice->SetVisibility(ESlateVisibility::Collapsed);

	for (const FArenaLobbyInviteEntry& Invite : LobbyInvitesList)
	{
		QueueLobbyInviteNotice(Invite);
	}
	ShowNextLobbyInviteNotice();
}

void UArenaLobbyWidget::BuildMatchSocialOverlay()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MatchSocialRoot"));
	WidgetTree->RootWidget = Root;
	BuildSidePanel(Root);
}

void UArenaLobbyWidget::SetSidePanelOpen(bool bOpen)
{
	bSidePanelOpen = bOpen;
	if (bOpen)
	{
		UpdateAvatars();
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaFriendsSubsystem* Friends = GameInstance->GetSubsystem<UArenaFriendsSubsystem>())
		{
			Friends->SetGameChatOpen(bOpen && ActiveSidePanelTab == 1);
		}
	}
	if (SidePanel && bOpen)
	{
		SidePanel->SetVisibility(ESlateVisibility::Visible);
	}
	if (SidePanelShade)
	{
		SidePanelShade->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	SidePanelSlide = FMath::Clamp(SidePanelSlide + (bOpen ? -0.01f : 0.01f), 0.0f, 1.0f);
	if (bMatchSocialOverlay)
	{
		if (APlayerController* Player = GetOwningPlayer())
		{
			Player->SetShowMouseCursor(bOpen);
			if (bOpen)
			{
				FInputModeGameAndUI InputMode;
				InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				InputMode.SetHideCursorDuringCapture(false);
				Player->SetInputMode(InputMode);
			}
			else
			{
				Player->SetInputMode(FInputModeGameOnly());
			}
			Player->SetIgnoreLookInput(bOpen);
			Player->SetIgnoreMoveInput(bOpen);
		}
	}
}

void UArenaLobbyWidget::HandleAvatarClicked()
{
	SetSidePanelOpen(!bSidePanelOpen);
}

void UArenaLobbyWidget::HandleButtonHovered()
{
	ArenaUISounds::PlayHover(this);
}

void UArenaLobbyWidget::HandleSidePanelClose()
{
	SetSidePanelOpen(false);
}

void UArenaLobbyWidget::HandleSidePanelSettings()
{
	SetSidePanelOpen(false);
	UArenaSettingsWidget::Open(GetOwningPlayer(), bMatchSocialOverlay ? nullptr : this);
}

void UArenaLobbyWidget::HandleQuitGame()
{
	if (bMatchSocialOverlay)
	{
		SetSidePanelOpen(false);
		if (AArenaPlayerController* Player = Cast<AArenaPlayerController>(GetOwningPlayer()))
		{
			Player->LeaveMatch();
		}
		return;
	}
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

#undef LOCTEXT_NAMESPACE

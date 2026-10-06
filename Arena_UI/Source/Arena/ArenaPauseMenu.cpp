#include "ArenaPauseMenu.h"

#include "ArenaFriendsSubsystem.h"
#include "ArenaPlayerController.h"
#include "ArenaUISounds.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ArenaPauseMenu"

namespace
{
	FSlateFontInfo Bold(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", Size);
	}

	constexpr float PanelRadius = 26.0f;
	constexpr float ButtonRadius = 18.0f;
	constexpr float RevealSeconds = 0.42f;
}

TSharedRef<SWidget> SArenaPauseMenu::MakeGlass(TSharedRef<SWidget> Content, float Radius, const FSlateBrush* SheenBrush, const FSlateBrush* EdgeBrush, float Blur)
{
	const float Corner = ArenaGlass::BlurCorner(Radius);
	return SNew(SBackgroundBlur)
		.BlurStrength(Blur)
		.bApplyAlphaToBlur(false)
		.Padding(FMargin(0.0f))
		.CornerRadius(FVector4(Corner, Corner, Corner, Corner))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(SheenBrush).Visibility(EVisibility::HitTestInvisible)
			]
			+ SOverlay::Slot()
			[
				Content
			]
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(EdgeBrush).Visibility(EVisibility::HitTestInvisible)
			]
		];
}

TSharedRef<SWidget> SArenaPauseMenu::MakeLight(const FLinearColor& Color, float Size)
{
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
	[
		SNew(SImage).Image(&LightBrush).ColorAndOpacity(Color)
	];
}

TSharedRef<SWidget> SArenaPauseMenu::MakeButton(const FText& Label, const FText& Hint, const FButtonStyle& Style, const FLinearColor& Tint, TFunction<void()> OnClick)
{
	TWeakObjectPtr<AArenaPlayerController> Weak = Controller;
	return SNew(SBox).WidthOverride(330.0f).HeightOverride(62.0f)
	[
		MakeGlass(
			SNew(SButton)
			.ButtonStyle(&Style)
			.ContentPadding(FMargin(22.0f, 0.0f))
			.HAlign(HAlign_Fill).VAlign(VAlign_Center)
			.OnHovered_Lambda([Weak]() { if (Weak.IsValid()) { ArenaUISounds::PlayHover(Weak.Get()); } })
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
				[
					SNew(SBox).WidthOverride(8.0f).HeightOverride(8.0f)
					[
						SNew(SImage).Image(&DotBrush).ColorAndOpacity(Tint)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Label).Font(Bold(17)).ColorAndOpacity(ArenaGlass::Ink)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Hint).Font(Bold(10)).ColorAndOpacity(ArenaGlass::Dim)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT(">"))).Font(Bold(16)).ColorAndOpacity(ArenaGlass::Dim)
				]
			],
			ButtonRadius, &ButtonSheen, &ButtonEdge, 24.0f)
	];
}

void SArenaPauseMenu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	PrimaryStyle = ArenaGlass::ButtonStyle(0.34f, ButtonRadius, 0.75f, ArenaGlass::Ice);
	GlassStyle = ArenaGlass::ButtonStyle(0.14f, ButtonRadius, 0.42f);
	DangerStyle = ArenaGlass::ButtonStyle(0.26f, ButtonRadius, 0.62f, ArenaGlass::Coral);
	SmallStyle = ArenaGlass::ButtonStyle(0.24f, 11.0f, 0.55f, ArenaGlass::Mint);
	PanelSheen = ArenaGlass::Sheen(PanelRadius, 0.12f);
	ButtonSheen = ArenaGlass::Sheen(ButtonRadius, 0.18f);

	TWeakObjectPtr<AArenaPlayerController> Weak = Controller;
	const FLinearColor Deep = ArenaGlass::Deep;

	ChildSlot
	[
		SNew(SOverlay)
		// the dim, big soft colored lights and the blur of the match: the thing the glass refracts
		+ SOverlay::Slot()
		[
			SAssignNew(Backdrop, SOverlay)
			.Visibility(EVisibility::HitTestInvisible)
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(&DimBrush).ColorAndOpacity(FLinearColor(Deep.R, Deep.G, Deep.B, 0.55f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(-380.0f, -480.0f, 0.0f, 0.0f))
			[
				MakeLight(FLinearColor(0.45f, 0.32f, 1.0f, 0.42f), 1000.0f)
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(FMargin(0.0f, 0.0f, -320.0f, -520.0f))
			[
				MakeLight(FLinearColor(0.10f, 0.80f, 0.80f, 0.34f), 1000.0f)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.0f, 0.0f, 0.0f, -650.0f))
			[
				MakeLight(FLinearColor(0.20f, 0.45f, 1.0f, 0.30f), 900.0f)
			]
			+ SOverlay::Slot()
			[
				SNew(SBackgroundBlur).BlurStrength(40.0f).bApplyAlphaToBlur(false).Padding(FMargin(0.0f))
			]
		]
		// the buttons, centre left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(90, 0, 0, 0)
		[
			SAssignNew(Menu, SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 0, 2)
			[
				SNew(STextBlock).Text(LOCTEXT("InMatch", "PARTIDA EN CURSO")).Font(Bold(12)).ColorAndOpacity(ArenaGlass::Mint)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 26)
			[
				SNew(STextBlock).Text(LOCTEXT("Pause", "PAUSA")).Font(Bold(54)).ColorAndOpacity(ArenaGlass::Ink)
				.ShadowOffset(FVector2D(0.0f, 3.0f)).ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.05f, 0.5f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Continue", "CONTINUAR"), LOCTEXT("ContinueHint", "Vuelve a la partida"), PrimaryStyle, ArenaGlass::Ice,
					[Weak]() { if (Weak.IsValid()) { Weak->HidePauseMenu(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Settings", "AJUSTES"), LOCTEXT("SettingsHint", "Vídeo, audio y controles"), GlassStyle, FLinearColor::White,
					[Weak]() { if (Weak.IsValid()) { Weak->HidePauseMenu(); Weak->OpenSettings(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Leave", "ABANDONAR PARTIDA"), LOCTEXT("LeaveHint", "Sales al lobby"), DangerStyle, ArenaGlass::Coral,
					[Weak]() { if (Weak.IsValid()) { Weak->LeaveMatch(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 18, 0, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("EscHint", "ESC para volver")).Font(Bold(11)).ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.40f))
			]
		]
		// the social panel, right
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(0, 50, 50, 50)
		[
			SAssignNew(Panel, SBox).WidthOverride(400.0f)
			[
				MakeGlass(
					SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(24, 22))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(LOCTEXT("SocialSmall", "EN LÍNEA")).Font(Bold(11)).ColorAndOpacity(ArenaGlass::Mint)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(LOCTEXT("Social", "SOCIAL")).Font(Bold(26)).ColorAndOpacity(ArenaGlass::Ink)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 16)
						[
							SNew(STextBlock).Text(LOCTEXT("SocialSub", "Tus amigos de Epic")).Font(Bold(12)).ColorAndOpacity(ArenaGlass::Dim)
						]
						+ SVerticalBox::Slot().FillHeight(1.0f)
						[
							SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
							+ SScrollBox::Slot()
							[
								SAssignNew(FriendsBox, SVerticalBox)
							]
						]
					],
					PanelRadius, &PanelSheen, &PanelEdge, 32.0f)
			]
		]
	];
	SetCanTick(true);
	ApplyReveal();
	RebuildFriends();
}

void SArenaPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Reveal < 1.0f)
	{
		Reveal = FMath::Min(Reveal + InDeltaTime / RevealSeconds, 1.0f);
		ApplyReveal();
	}
}

void SArenaPauseMenu::ApplyReveal()
{
	// Ease out: the dim fades in while the buttons slide in from the left and the panel from the right
	const float Eased = 1.0f - FMath::Pow(1.0f - Reveal, 3.0f);
	if (Backdrop.IsValid())
	{
		Backdrop->SetRenderOpacity(Eased);
	}
	if (Menu.IsValid())
	{
		Menu->SetRenderOpacity(Eased);
		Menu->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(-48.0f * (1.0f - Eased), 0.0f))));
	}
	if (Panel.IsValid())
	{
		Panel->SetRenderOpacity(Eased);
		Panel->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(64.0f * (1.0f - Eased), 0.0f))));
	}
}

void SArenaPauseMenu::RebuildFriends()
{
	if (!FriendsBox.IsValid())
	{
		return;
	}
	FriendsBox->ClearChildren();
	AArenaPlayerController* PC = Controller.Get();
	UGameInstance* GameInstance = PC ? PC->GetGameInstance() : nullptr;
	UArenaFriendsSubsystem* Friends = GameInstance ? GameInstance->GetSubsystem<UArenaFriendsSubsystem>() : nullptr;
	if (!Friends || !Friends->IsAvailable())
	{
		FriendsBox->AddSlot().AutoHeight().Padding(0, 10)
		[
			SNew(STextBlock).Text(LOCTEXT("LANOnlyFriends", "Las partidas son solo por LAN. Para jugar con amigos, conectaos a la misma red local y buscad la partida desde el lobby.")).Font(Bold(13)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f)).AutoWrapText(true)
		];
		return;
	}
	if (Friends->GetFriends().IsEmpty())
	{
		FriendsBox->AddSlot().AutoHeight().Padding(0, 10)
		[
			SNew(STextBlock).Text(LOCTEXT("NoFriends", "Todavía no tienes amigos añadidos.")).Font(Bold(13)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f))
		];
		return;
	}

	// the ones who are online first
	TArray<FArenaFriendInfo> Sorted = Friends->GetFriends();
	Sorted.StableSort([](const FArenaFriendInfo& A, const FArenaFriendInfo& B) { return A.bOnline && !B.bOnline; });
	for (const FArenaFriendInfo& Friend : Sorted)
	{
		const FString NetId = Friend.NetId;
		TWeakObjectPtr<AArenaPlayerController> Weak = Controller;
		const FLinearColor Dot = Friend.bOnline ? FLinearColor(0.30f, 0.90f, 0.50f) : FLinearColor(0.5f, 0.5f, 0.55f);
		FriendsBox->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SBorder).BorderImage(&RowBrush).Padding(FMargin(12, 9))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(SBox).WidthOverride(12.0f).HeightOverride(12.0f)
					[
						SNew(SImage).Image(&DotBrush).ColorAndOpacity(Dot)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Friend.Name)).Font(Bold(15)).ColorAndOpacity(FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Friend.Status)).Font(Bold(11)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.5f))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton).ButtonStyle(&SmallStyle).Visibility(Friend.bOnline ? EVisibility::Visible : EVisibility::Collapsed)
					.ContentPadding(FMargin(12, 5))
					.OnClicked_Lambda([Weak, NetId]()
					{
						if (AArenaPlayerController* Player = Weak.Get())
						{
							if (UGameInstance* Instance = Player->GetGameInstance())
							{
								if (UArenaFriendsSubsystem* Subsystem = Instance->GetSubsystem<UArenaFriendsSubsystem>())
								{
									Subsystem->InviteToLobby(NetId);
								}
							}
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock).Text(LOCTEXT("InviteFriend", "INVITAR")).Font(Bold(12)).ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE

#include "ArenaPauseMenu.h"

#include "ArenaFriendsSubsystem.h"
#include "ArenaPlayerController.h"
#include "Brushes/SlateColorBrush.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
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

	FButtonStyle MakeStyle(const FLinearColor& Tint, float Radius)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(Tint.R, Tint.G, Tint.B, 0.42f), Radius, FLinearColor(Tint.R, Tint.G, Tint.B, 0.70f), 1.5f));
		Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(Tint.R, Tint.G, Tint.B, 0.62f), Radius, FLinearColor(1, 1, 1, 0.90f), 1.5f));
		Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(Tint.R, Tint.G, Tint.B, 0.78f), Radius, FLinearColor(1, 1, 1, 0.95f), 1.5f));
		Style.SetNormalPadding(FMargin(0));
		Style.SetPressedPadding(FMargin(0));
		return Style;
	}
}

TSharedRef<SWidget> SArenaPauseMenu::MakeButton(const FText& Label, const FLinearColor& Tint, TFunction<void()> OnClick)
{
	const bool bDanger = Tint.R > Tint.B;
	return SNew(SBox).WidthOverride(300.0f).HeightOverride(54.0f)
	[
		SNew(SButton)
		.ButtonStyle(bDanger ? &DangerStyle : &ButtonStyle)
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(Label).Font(Bold(17)).ColorAndOpacity(FLinearColor::White)
		]
	];
}

void SArenaPauseMenu::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	ButtonStyle = MakeStyle(FLinearColor(0.20f, 0.55f, 1.0f), 16.0f);
	DangerStyle = MakeStyle(FLinearColor(0.92f, 0.10f, 0.12f), 16.0f);
	SmallStyle = MakeStyle(FLinearColor(0.30f, 0.85f, 0.55f), 11.0f);

	TWeakObjectPtr<AArenaPlayerController> Weak = Controller;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f))
		]
		// the buttons, centre left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(90, 0, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
			[
				SNew(STextBlock).Text(LOCTEXT("Pause", "PAUSA")).Font(Bold(40)).ColorAndOpacity(FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Continue", "CONTINUAR"), FLinearColor(0.20f, 0.55f, 1.0f), [Weak]() { if (Weak.IsValid()) { Weak->HidePauseMenu(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Settings", "AJUSTES"), FLinearColor(0.20f, 0.55f, 1.0f), [Weak]() { if (Weak.IsValid()) { Weak->HidePauseMenu(); Weak->OpenSettings(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
			[
				MakeButton(LOCTEXT("Leave", "ABANDONAR PARTIDA"), FLinearColor(0.95f, 0.28f, 0.25f), [Weak]() { if (Weak.IsValid()) { Weak->LeaveMatch(); } })
			]
		]
		// the social panel, right
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Fill).Padding(0, 50, 50, 50)
		[
			SNew(SBox).WidthOverride(400.0f)
			[
				SNew(SBorder).BorderImage(&PanelBrush).Padding(FMargin(22, 18))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(LOCTEXT("Social", "SOCIAL")).Font(Bold(22)).ColorAndOpacity(FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 14)
					[
						SNew(STextBlock).Text(LOCTEXT("SocialSub", "Tus amigos de Epic")).Font(Bold(12)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.55f))
					]
					+ SVerticalBox::Slot().FillHeight(1.0f)
					[
						SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed)
						+ SScrollBox::Slot()
						[
							SAssignNew(FriendsBox, SVerticalBox)
						]
					]
				]
			]
		]
	];
	RebuildFriends();
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

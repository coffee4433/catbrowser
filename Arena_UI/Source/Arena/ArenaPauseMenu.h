// The menu ESC opens inside a match: liquid glass over the blurred match, the buttons to go on, open the settings or leave
// the match back to the lobby on the left and the social panel (friends, who is online, invite) on the right. The game does
// not pause (it is multiplayer), so the menu only takes the input. It slides in when opened.

#pragma once

#include "CoreMinimal.h"
#include "ArenaGlassStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class AArenaPlayerController;
class SVerticalBox;

class ARENA_API SArenaPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaPauseMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AArenaPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Fills the friends list again (the friends of Epic changed, or the menu just opened) */
	void RebuildFriends();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> MakeButton(const FText& Label, const FText& Hint, const FButtonStyle& Style, const FLinearColor& Tint, TFunction<void()> OnClick);
	/** Blur of what is behind, the sheen, the content (a glass surface) and the light along the edge, in that order */
	TSharedRef<SWidget> MakeGlass(TSharedRef<SWidget> Content, float Radius, const FSlateBrush* SheenBrush, const FSlateBrush* EdgeBrush, float Blur);
	TSharedRef<SWidget> MakeLight(const FLinearColor& Color, float Size);
	void ApplyReveal();

	TWeakObjectPtr<AArenaPlayerController> Controller;
	TSharedPtr<SVerticalBox> FriendsBox;
	TSharedPtr<SWidget> Backdrop;
	TSharedPtr<SWidget> Menu;
	TSharedPtr<SWidget> Panel;
	float Reveal = 0.0f;

	FButtonStyle PrimaryStyle;
	FButtonStyle GlassStyle;
	FButtonStyle DangerStyle;
	FButtonStyle SmallStyle;
	FSlateColorBrush DimBrush = FSlateColorBrush(FLinearColor::White);
	FSlateRoundedBoxBrush LightBrush = FSlateRoundedBoxBrush(FLinearColor::White);
	FSlateRoundedBoxBrush PanelBrush = ArenaGlass::Surface(0.09f, 26.0f, 0.42f, FLinearColor::White, 1.3f);
	FSlateRoundedBoxBrush PanelEdge = ArenaGlass::EdgeGlow(26.0f, 0.09f);
	FSlateBrush PanelSheen;
	FSlateRoundedBoxBrush ButtonEdge = ArenaGlass::EdgeGlow(18.0f, 0.12f);
	FSlateBrush ButtonSheen;
	FSlateRoundedBoxBrush RowBrush = ArenaGlass::Surface(0.07f, 14.0f, 0.16f);
	FSlateRoundedBoxBrush DotBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.0f);
};

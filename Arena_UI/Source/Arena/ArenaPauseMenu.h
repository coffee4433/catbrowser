// The menu ESC opens inside a match: the social panel on the right (friends, who is online, invite) and the buttons to go on, open the
// settings or leave the match back to the lobby. The game does not pause (it is multiplayer), so the menu only takes the input.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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

private:
	TSharedRef<SWidget> MakeButton(const FText& Label, const FLinearColor& Tint, TFunction<void()> OnClick);

	TWeakObjectPtr<AArenaPlayerController> Controller;
	TSharedPtr<SVerticalBox> FriendsBox;

	FButtonStyle ButtonStyle;
	FButtonStyle DangerStyle;
	FButtonStyle SmallStyle;
	FSlateRoundedBoxBrush PanelBrush = FSlateRoundedBoxBrush(FLinearColor(0.02f, 0.03f, 0.06f, 0.88f), 24.0f);
	FSlateRoundedBoxBrush RowBrush = FSlateRoundedBoxBrush(FLinearColor(1.0f, 1.0f, 1.0f, 0.06f), 14.0f);
	FSlateRoundedBoxBrush DotBrush = FSlateRoundedBoxBrush(FLinearColor::White, 6.0f);
};

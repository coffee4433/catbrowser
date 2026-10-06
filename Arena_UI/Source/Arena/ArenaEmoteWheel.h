// Emote selector shown while B is held: glass disc with the emotes of the character around it

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArenaEmoteWheel.generated.h"

class UFortnitePortingCharacterComponent;
class UFortnitePortingEmoteData;
class UTextBlock;
class UFontFace;

/** Forwards the hover / click of one emote slot (dynamic delegates carry no payload) */
UCLASS()
class UArenaEmoteSlotHandler : public UObject
{
	GENERATED_BODY()

public:
	int32 Index = INDEX_NONE;
	TWeakObjectPtr<class UArenaEmoteWheel> Owner;

	UFUNCTION() void HandleClicked();
	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleUnhovered();
};

UCLASS()
class ARENA_API UArenaEmoteWheel : public UUserWidget
{
	GENERATED_BODY()

public:
	UArenaEmoteWheel(const FObjectInitializer& ObjectInitializer);

	/** Shows the wheel for this character (the emotes come from its cosmetics component) */
	static UArenaEmoteWheel* Show(APlayerController* PC, UFortnitePortingCharacterComponent* Cosmetics, UUserWidget* ReturnFocus = nullptr);

	/** Hides the wheel and gives the game input back */
	void Hide();

	void PlayEmote(int32 Index);
	void HoverEmote(int32 Index);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Build();

	/** The lobby, when the wheel is opened over it: the wheel then leaves the input mode alone and gives the focus back */
	TWeakObjectPtr<UUserWidget> ReturnFocus;

	UPROPERTY(Transient) TObjectPtr<UFortnitePortingCharacterComponent> Cosmetics;
	UPROPERTY(Transient) TArray<TObjectPtr<UArenaEmoteSlotHandler>> Handlers;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UFontFace> BodyFontFace;
};

// Glass button with an animated hover: the rim lights up, thickens and breathes, the glass brightens and the button
// lifts a little. It takes its look from the same FButtonStyle the other glass widgets use (ArenaGlass::ButtonStyle).

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "ArenaGlassStyle.h"
#include "ArenaGlassButton.generated.h"

UCLASS()
class ARENA_API UArenaGlassButton : public UButton
{
	GENERATED_BODY()

public:
	/** Takes fill, radius, rim and tint from the Normal brush of a style made by ArenaGlass::ButtonStyle */
	void SetGlassFromStyle(const FButtonStyle& InStyle);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

private:
	UFUNCTION() void HandleHover();
	UFUNCTION() void HandleUnhover();

	void StartAnimation();
	bool Animate(float DeltaTime);
	void ApplyLook();

	float BaseFill = 0.1f;
	float BaseRadius = 14.0f;
	float BaseRim = 0.3f;
	FLinearColor BaseTint = FLinearColor::White;
	float Hover = 0.0f;
	float Time = 0.0f;
	bool bHovered = false;
	bool bBound = false;
	FTSTicker::FDelegateHandle Ticker;
};

namespace ArenaGlass
{
	/** SetStyle for glass buttons: keeps the hover animation when the button is a UArenaGlassButton */
	inline void Style(UButton* Button, const FButtonStyle& InStyle)
	{
		if (UArenaGlassButton* Glass = Cast<UArenaGlassButton>(Button))
		{
			Glass->SetGlassFromStyle(InStyle);
		}
		else if (Button)
		{
			Button->SetStyle(InStyle);
		}
	}
}

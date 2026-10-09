// The sight of the weapons: a small cross while the gun is in the hands and, when the player holds the aim button, the scope of the
// sniper (a black screen with a round window and the crosshair lines) or a tighter cross for the other guns.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UFortnitePortingCharacterComponent;

class SFortnitePortingReticle : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFortnitePortingReticle) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFortnitePortingCharacterComponent>, Component)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(100.0f, 100.0f); }

private:
	TWeakObjectPtr<UFortnitePortingCharacterComponent> Component;
};

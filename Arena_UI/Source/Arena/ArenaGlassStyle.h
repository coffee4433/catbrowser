// Liquid glass look shared by the in-game menus (social panel, settings): translucent white fill, thin bright rim and
// tinted accents, the same recipe as the launcher.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"

namespace ArenaGlass
{
	/** Radius for the blur widget behind a glass panel whose border radius is 22 (see arena.BlurRadius) */
	float BlurRadius();

	/** Corner radius for the blur widget that sits behind a glass surface with the given border radius */
	inline float BlurCorner(float SurfaceRadius) { return SurfaceRadius * BlurRadius() / 22.0f; }

	const FLinearColor Ink(1.0f, 1.0f, 1.0f, 0.96f);
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.60f);
	const FLinearColor Mint(0.37f, 0.92f, 0.83f);
	const FLinearColor Ice(0.55f, 0.80f, 1.0f);
	const FLinearColor Coral(1.0f, 0.48f, 0.52f);
	const FLinearColor Violet(0.70f, 0.58f, 1.0f);
	const FLinearColor Amber(1.0f, 0.82f, 0.35f);

	/** Rounded translucent surface. A tint colors both the fill and the rim. */
	inline FSlateRoundedBoxBrush Surface(float Fill, float Radius, float Rim, const FLinearColor& Tint = FLinearColor::White, float Border = 1.0f)
	{
		return FSlateRoundedBoxBrush(FLinearColor(Tint.R, Tint.G, Tint.B, Fill), Radius, FLinearColor(Tint.R, Tint.G, Tint.B, Rim), Border);
	}

	inline FButtonStyle ButtonStyle(float Fill, float Radius, float Rim, const FLinearColor& Tint = FLinearColor::White)
	{
		const float NormalFill = FMath::Max(Fill, 0.28f);
		FButtonStyle Style;
		Style.SetNormal(Surface(NormalFill, Radius, Rim, Tint));
		Style.SetHovered(Surface(FMath::Clamp(NormalFill * 1.6f + 0.08f, 0.50f, 0.82f), Radius, FMath::Min(Rim + 0.25f, 0.95f), Tint));
		Style.SetPressed(Surface(FMath::Max(NormalFill * 0.85f, 0.40f), Radius, Rim, Tint));
		Style.SetDisabled(Surface(0.14f, Radius, 0.10f, FLinearColor::White));
		Style.SetNormalPadding(FMargin(0.0f));
		Style.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
		return Style;
	}
}

// Liquid glass look shared by the in-game menus (lobby, settings, pause, emotes, inventory): a translucent fill over the
// blur of what is behind, a crisp bright rim, a soft light along the inside of the edge and a specular sheen that runs
// down from the top. A tint colors the fill and the rim together, the same recipe as the launcher.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"

class UTexture2D;
class UWidget;
class UWidgetTree;

namespace ArenaGlass
{
	/** Radius for the blur widget behind a glass panel whose border radius is 22 (see arena.BlurRadius) */
	float BlurRadius();

	/** Corner radius for the blur widget that sits behind a glass surface with the given border radius */
	inline float BlurCorner(float SurfaceRadius) { return SurfaceRadius * BlurRadius() / 22.0f; }

	/** Vertical white gradient the sheen is made of (loaded once and kept alive); null when the asset is missing */
	UTexture2D* SheenTexture();

	const FLinearColor Ink(1.0f, 1.0f, 1.0f, 0.96f);
	const FLinearColor Dim(1.0f, 1.0f, 1.0f, 0.60f);
	const FLinearColor Mint(0.37f, 0.92f, 0.83f);
	const FLinearColor Ice(0.55f, 0.80f, 1.0f);
	const FLinearColor Coral(1.0f, 0.48f, 0.52f);
	const FLinearColor Violet(0.70f, 0.58f, 1.0f);
	const FLinearColor Amber(1.0f, 0.82f, 0.35f);
	/** Deep navy for the dims and backdrops the glass sits on */
	const FLinearColor Deep(0.015f, 0.022f, 0.06f);

	/** Rounded translucent surface. A tint colors both the fill and the rim. */
	inline FSlateRoundedBoxBrush Surface(float Fill, float Radius, float Rim, const FLinearColor& Tint = FLinearColor::White, float Border = 1.0f)
	{
		return FSlateRoundedBoxBrush(FLinearColor(Tint.R, Tint.G, Tint.B, Fill), Radius, FLinearColor(Tint.R, Tint.G, Tint.B, Rim), Border);
	}

	/** Soft light along the inside of the edge: no fill, a wide faint rim. Drawn over a Surface it gives the glass thickness. */
	inline FSlateRoundedBoxBrush EdgeGlow(float Radius, float Strength = 0.10f, const FLinearColor& Tint = FLinearColor::White, float Width = 7.0f)
	{
		return FSlateRoundedBoxBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), Radius, FLinearColor(Tint.R, Tint.G, Tint.B, Strength), Width);
	}

	/** Specular sheen: the gradient cropped to the rounded shape, bright at the top and gone by the middle */
	FSlateBrush Sheen(float Radius, float Strength = 0.14f, const FLinearColor& Tint = FLinearColor::White);

	/** The color a glass surface lights up to when hovered: ice white with a touch of its own tint */
	inline FLinearColor HoverLight(const FLinearColor& Tint)
	{
		return FMath::Lerp(FLinearColor(0.82f, 0.95f, 1.0f), FLinearColor(Tint.R, Tint.G, Tint.B, 1.0f), 0.35f);
	}

	inline FButtonStyle ButtonStyle(float Fill, float Radius, float Rim, const FLinearColor& Tint = FLinearColor::White)
	{
		const float NormalFill = FMath::Max(Fill, 0.28f);
		const FLinearColor HoverRim = FMath::Lerp(FLinearColor(Tint.R, Tint.G, Tint.B, 1.0f), HoverLight(Tint), 0.7f);
		FButtonStyle Style;
		Style.SetNormal(Surface(NormalFill, Radius, Rim, Tint));
		Style.SetHovered(FSlateRoundedBoxBrush(
			FLinearColor(Tint.R, Tint.G, Tint.B, FMath::Clamp(NormalFill * 1.6f + 0.08f, 0.50f, 0.82f)), Radius,
			FLinearColor(HoverRim.R, HoverRim.G, HoverRim.B, FMath::Min(Rim + 0.35f, 0.98f)), 1.6f));
		Style.SetPressed(Surface(FMath::Max(NormalFill * 0.85f, 0.40f), Radius, Rim, Tint));
		Style.SetDisabled(Surface(0.14f, Radius, 0.10f, FLinearColor::White));
		Style.SetNormalPadding(FMargin(0.0f));
		Style.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
		return Style;
	}

	/** UMG: puts Content (normally a Surface border) between the sheen under it and the edge light over it, in an overlay */
	UWidget* Layered(UWidgetTree* Tree, UWidget* Content, float Radius, const FLinearColor& Tint = FLinearColor::White, float SheenStrength = 0.14f, float GlowStrength = 0.10f);
}

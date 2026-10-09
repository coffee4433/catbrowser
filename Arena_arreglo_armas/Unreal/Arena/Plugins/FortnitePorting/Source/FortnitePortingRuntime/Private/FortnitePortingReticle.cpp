#include "FortnitePortingReticle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SFortnitePortingReticle::Construct(const FArguments& InArgs)
{
	Component = InArgs._Component;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SFortnitePortingReticle::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UFortnitePortingCharacterComponent* Owner = Component.Get();
	const UFortnitePortingWeaponData* Weapon = Owner ? Owner->GetCurrentWeapon() : nullptr;
	if (!Owner || !Weapon || Owner->bCombatBlocked || Weapon->Range < 1000.0f)
	{
		return LayerId;      // no gun in the hands (or building, or a melee weapon): no sight
	}

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Centre = Size * 0.5f;
	const float Alpha = Owner->GetAimAlpha();
	const bool bSniper = Weapon->WeaponType == TEXT("Sniper");
	static const FSlateColorBrush White(FLinearColor::White);

	auto Box = [&](const FVector2D& Position, const FVector2D& Extent, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(Extent, FSlateLayoutTransform(Position)), &White, ESlateDrawEffect::None, Color);
	};

	// scoped sniper: a black screen with a round window and fine lines
	if (bSniper && Alpha > 0.02f)
	{
		const float Visible = FMath::Clamp((Alpha - 0.35f) / 0.65f, 0.0f, 1.0f);
		const float HoleRadius = FMath::Min(Size.X, Size.Y) * 0.44f;
		const float HalfDiagonal = Size.Size() * 0.5f + 8.0f;
		const float Outline = HalfDiagonal - HoleRadius;
		// A rounded box fills itself with the tint it is drawn with (not with the brush colour): a white tint filled the window of the
		// scope with white. The window stays transparent with a clear tint; the black ring (and its fade in) lives in the brush.
		const FSlateRoundedBoxBrush Ring(FLinearColor(0, 0, 0, 0), HalfDiagonal, FLinearColor(0.0f, 0.0f, 0.0f, Visible), Outline);
		const FLinearColor Tint(0.0f, 0.0f, 0.0f, 0.0f);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2D(HalfDiagonal * 2.0f), FSlateLayoutTransform(Centre - FVector2D(HalfDiagonal))), &Ring, ESlateDrawEffect::None, Tint);

		const FLinearColor Line(0.02f, 0.02f, 0.02f, 0.9f * Visible);
		Box(FVector2D(Centre.X - HoleRadius, Centre.Y - 0.75f), FVector2D(HoleRadius * 2.0f, 1.5f), Line, LayerId + 1);
		Box(FVector2D(Centre.X - 0.75f, Centre.Y - HoleRadius), FVector2D(1.5f, HoleRadius * 2.0f), Line, LayerId + 1);
		const FLinearColor Dot(1.0f, 0.15f, 0.12f, Visible);
		Box(Centre - FVector2D(2.0f), FVector2D(4.0f), Dot, LayerId + 2);
		return LayerId + 3;
	}

	// every other gun: a small cross that closes in while aiming
	const float Gap = FMath::Lerp(11.0f, 4.0f, Alpha);
	const float Length = FMath::Lerp(9.0f, 6.0f, Alpha);
	const float Thick = 2.0f;
	const FLinearColor Shadow(0.0f, 0.0f, 0.0f, 0.55f);
	const FLinearColor Mark(1.0f, 1.0f, 1.0f, 0.95f);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const float Grow = Pass == 0 ? 1.0f : 0.0f;       // the shadow is a little bigger than the mark
		const FLinearColor& Color = Pass == 0 ? Shadow : Mark;
		const int32 Layer = LayerId + Pass;
		Box(FVector2D(Centre.X - Gap - Length - Grow, Centre.Y - Thick * 0.5f - Grow), FVector2D(Length + Grow * 2.0f, Thick + Grow * 2.0f), Color, Layer);
		Box(FVector2D(Centre.X + Gap - Grow, Centre.Y - Thick * 0.5f - Grow), FVector2D(Length + Grow * 2.0f, Thick + Grow * 2.0f), Color, Layer);
		Box(FVector2D(Centre.X - Thick * 0.5f - Grow, Centre.Y - Gap - Length - Grow), FVector2D(Thick + Grow * 2.0f, Length + Grow * 2.0f), Color, Layer);
		Box(FVector2D(Centre.X - Thick * 0.5f - Grow, Centre.Y + Gap - Grow), FVector2D(Thick + Grow * 2.0f, Length + Grow * 2.0f), Color, Layer);
	}
	Box(Centre - FVector2D(1.0f), FVector2D(2.0f), Mark, LayerId + 2);
	return LayerId + 3;
}

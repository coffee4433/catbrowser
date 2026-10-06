#include "ArenaGlassButton.h"

void UArenaGlassButton::SetGlassFromStyle(const FButtonStyle& InStyle)
{
	const FSlateBrush& Normal = InStyle.Normal;
	const FLinearColor Fill = Normal.TintColor.GetSpecifiedColor();
	BaseFill = Fill.A;
	BaseTint = FLinearColor(Fill.R, Fill.G, Fill.B, 1.0f);
	BaseRim = Normal.OutlineSettings.Color.GetSpecifiedColor().A;
	BaseRadius = Normal.OutlineSettings.CornerRadii.X;

	if (!bBound)
	{
		bBound = true;
		OnHovered.AddUniqueDynamic(this, &UArenaGlassButton::HandleHover);
		OnUnhovered.AddUniqueDynamic(this, &UArenaGlassButton::HandleUnhover);
	}
	ApplyLook();
}

void UArenaGlassButton::HandleHover()
{
	bHovered = true;
	StartAnimation();
}

void UArenaGlassButton::HandleUnhover()
{
	bHovered = false;
	StartAnimation();
}

void UArenaGlassButton::StartAnimation()
{
	if (Ticker.IsValid())
	{
		return;
	}
	const TWeakObjectPtr<UArenaGlassButton> Weak(this);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float DeltaTime)
	{
		UArenaGlassButton* Self = Weak.Get();
		return Self != nullptr && Self->Animate(DeltaTime);
	}));
}

bool UArenaGlassButton::Animate(float DeltaTime)
{
	Hover = FMath::FInterpTo(Hover, bHovered ? 1.0f : 0.0f, DeltaTime, 11.0f);
	Time += DeltaTime;
	if (!bHovered && Hover < 0.003f)
	{
		Hover = 0.0f;
		ApplyLook();
		SetRenderScale(FVector2D::UnitVector);
		Ticker.Reset();
		return false;
	}
	ApplyLook();
	return true;
}

void UArenaGlassButton::ApplyLook()
{
	// While hovered the rim breathes; the glass warms up toward a bright ice color
	const float Breath = Hover * 0.10f * FMath::Sin(Time * 6.0f);
	const float Fill = BaseFill + Hover * (0.09f + BaseFill * 0.8f);
	const float Rim = FMath::Clamp(BaseRim + Hover * (0.98f - BaseRim) + Breath, 0.0f, 1.0f);
	const float Width = 1.0f + Hover * 1.1f;
	const FLinearColor RimTint = FMath::Lerp(BaseTint, FLinearColor(0.78f, 0.95f, 1.0f), Hover * 0.65f);

	auto Glass = [&](float FillAlpha, float RimAlpha, const FLinearColor& Fillcolor, const FLinearColor& Rimcolor, float Border)
	{
		return FSlateRoundedBoxBrush(FLinearColor(Fillcolor.R, Fillcolor.G, Fillcolor.B, FillAlpha), BaseRadius, FLinearColor(Rimcolor.R, Rimcolor.G, Rimcolor.B, RimAlpha), Border);
	};

	FButtonStyle Look;
	Look.SetNormal(Glass(Fill, Rim, BaseTint, RimTint, Width));
	Look.SetHovered(Glass(Fill, Rim, BaseTint, RimTint, Width));
	Look.SetPressed(Glass(Fill * 0.65f, Rim, BaseTint, RimTint, Width));
	Look.SetDisabled(Glass(0.03f, 0.10f, FLinearColor::White, FLinearColor::White, 1.0f));
	Look.SetNormalPadding(FMargin(0.0f));
	Look.SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
	SetStyle(Look);
	SetRenderScale(FVector2D(1.0f + 0.035f * Hover, 1.0f + 0.035f * Hover));
}

void UArenaGlassButton::ReleaseSlateResources(bool bReleaseChildren)
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Ticker.Reset();
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UArenaGlassButton::BeginDestroy()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Super::BeginDestroy();
}

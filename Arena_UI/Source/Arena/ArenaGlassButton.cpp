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
		OnPressed.AddUniqueDynamic(this, &UArenaGlassButton::HandlePress);
		OnReleased.AddUniqueDynamic(this, &UArenaGlassButton::HandleRelease);
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

void UArenaGlassButton::HandlePress()
{
	bPressed = true;
	StartAnimation();
}

void UArenaGlassButton::HandleRelease()
{
	bPressed = false;
	Flash = 1.0f;
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
	Press = FMath::FInterpTo(Press, bPressed ? 1.0f : 0.0f, DeltaTime, 22.0f);
	Flash = FMath::Max(Flash - DeltaTime * 3.5f, 0.0f);
	Time += DeltaTime;
	if (!bHovered && !bPressed && Hover < 0.003f && Press < 0.003f && Flash <= 0.0f)
	{
		Hover = 0.0f;
		Press = 0.0f;
		ApplyLook();
		SetRenderScale(FVector2D::UnitVector);
		SetRenderTranslation(FVector2D::ZeroVector);
		Ticker.Reset();
		return false;
	}
	ApplyLook();
	return true;
}

void UArenaGlassButton::ApplyLook()
{
	// While hovered the rim breathes and the glass warms up toward ice white; a press sinks it and the release flashes
	const float Breath = Hover * 0.08f * FMath::Sin(Time * 5.0f);
	const float Glow = FMath::Clamp(Hover + Flash * 0.5f, 0.0f, 1.0f);
	const float Fill = FMath::Clamp(BaseFill + Hover * (0.10f + BaseFill * 0.8f) + Flash * 0.14f - Press * 0.06f, 0.0f, 1.0f);
	const float Rim = FMath::Clamp(BaseRim + Glow * (0.98f - BaseRim) + Breath, 0.0f, 1.0f);
	const float Width = 1.0f + Glow * 1.3f;
	const FLinearColor Light = ArenaGlass::HoverLight(BaseTint);
	const FLinearColor RimTint = FMath::Lerp(BaseTint, Light, Glow * 0.7f);
	const FLinearColor FillTint = FMath::Lerp(BaseTint, Light, Hover * 0.25f);

	auto Glass = [&](float FillAlpha, float RimAlpha, const FLinearColor& Fillcolor, const FLinearColor& Rimcolor, float Border)
	{
		return FSlateRoundedBoxBrush(FLinearColor(Fillcolor.R, Fillcolor.G, Fillcolor.B, FillAlpha), BaseRadius, FLinearColor(Rimcolor.R, Rimcolor.G, Rimcolor.B, RimAlpha), Border);
	};

	FButtonStyle Look;
	Look.SetNormal(Glass(Fill, Rim, FillTint, RimTint, Width));
	Look.SetHovered(Glass(Fill, Rim, FillTint, RimTint, Width));
	Look.SetPressed(Glass(Fill * 0.75f, Rim, FillTint, RimTint, Width));
	Look.SetDisabled(Glass(0.03f, 0.10f, FLinearColor::White, FLinearColor::White, 1.0f));
	Look.SetNormalPadding(FMargin(0.0f));
	Look.SetPressedPadding(FMargin(0.0f));
	SetStyle(Look);

	const float Scale = 1.0f + 0.035f * Hover - 0.045f * Press;
	SetRenderScale(FVector2D(Scale, Scale));
	SetRenderTranslation(FVector2D(0.0f, -2.0f * Hover * (1.0f - Press)));
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

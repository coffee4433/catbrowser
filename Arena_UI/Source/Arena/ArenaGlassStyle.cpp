#include "ArenaGlassStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "UObject/StrongObjectPtr.h"

// Corner radius handed to UBackgroundBlur. A rounded box brush of radius 22 and a blur of radius 11 draw the same corner (the
// blur scales it with the UI by itself). The blur and the glass border must round
// their corners the same way; this is exposed as a cvar so the match can be tuned while looking at it.
static TAutoConsoleVariable<float> CVarArenaBlurRadius(
	TEXT("arena.BlurRadius"), 11.0f,
	TEXT("Corner radius of the blur behind the in-game glass panels (Slate units, multiplied by the UI scale)."));

float ArenaGlass::BlurRadius()
{
	return CVarArenaBlurRadius.GetValueOnGameThread();
}

UTexture2D* ArenaGlass::SheenTexture()
{
	static TStrongObjectPtr<UTexture2D> Texture;
	if (!Texture.IsValid())
	{
		Texture.Reset(LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/Fortnite/Textures/T_UI_GradientV.T_UI_GradientV")));
	}
	return Texture.Get();
}

FSlateBrush ArenaGlass::Sheen(float Radius, float Strength, const FLinearColor& Tint)
{
	UTexture2D* Texture = SheenTexture();
	if (!Texture)
	{
		return Surface(0.0f, Radius, 0.0f);
	}
	FSlateRoundedBoxBrush Brush(Texture->GetFName(), FLinearColor(Tint.R, Tint.G, Tint.B, Strength), FVector4(Radius, Radius, Radius, Radius), FVector2D(64.0f, 64.0f));
	Brush.SetResourceObject(Texture);
	return Brush;
}

UWidget* ArenaGlass::Layered(UWidgetTree* Tree, UWidget* Content, float Radius, const FLinearColor& Tint, float SheenStrength, float GlowStrength)
{
	UOverlay* Layers = Tree->ConstructWidget<UOverlay>();
	auto Fill = [Layers](UWidget* Widget)
	{
		UOverlaySlot* LayerSlot = Layers->AddChildToOverlay(Widget);
		LayerSlot->SetHorizontalAlignment(HAlign_Fill);
		LayerSlot->SetVerticalAlignment(VAlign_Fill);
	};

	UImage* SheenImage = Tree->ConstructWidget<UImage>();
	SheenImage->SetBrush(Sheen(Radius, SheenStrength));
	SheenImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fill(SheenImage);

	Fill(Content);

	UBorder* Edge = Tree->ConstructWidget<UBorder>();
	Edge->SetBrush(EdgeGlow(Radius, GlowStrength, Tint));
	Edge->SetPadding(FMargin(0.0f));
	Edge->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fill(Edge);
	return Layers;
}

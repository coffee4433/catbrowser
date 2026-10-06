#include "ArenaVitalsComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"

// ------------------------------------------------------------------------------------------------ the HUD

class SArenaVitalsHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SArenaVitalsHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UArenaVitalsComponent>, Vitals)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Vitals = InArgs._Vitals;
		PanelBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, 20.0f);
		PanelEdgeBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0, 0, 0, 0), 20.0f, FLinearColor::White, 1.6f);
		BarBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, 9.0f);
		BarEdgeBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0, 0, 0, 0), 9.0f, FLinearColor::White, 1.5f);
		if (const UArenaVitalsComponent* V = Vitals.Get())
		{
			ShownHealth = TrailHealth = LastHealth = V->GetHealth();
			ShownShield = TrailShield = LastShield = V->GetShield();
		}
		SetCanTick(false);
		RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SArenaVitalsHud::Animate));
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(PanelWidth, PanelHeight + 30.0f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const UArenaVitalsComponent* V = Vitals.Get();
		if (!V)
		{
			return Layer;
		}
		const float Alpha = V->IsDead() ? 0.35f : 1.0f;
		const FVector2f Top(0.0f, 30.0f);        // room above the panel for the floating numbers

		// panel
		const FLinearColor Flash = FMath::Max(HitFlash, HealFlash) > 0.0f ? (HitFlash >= HealFlash ? FLinearColor(1.0f, 0.15f, 0.12f) : FLinearColor(0.2f, 1.0f, 0.4f)) : FLinearColor::White;
		const float FlashAmount = FMath::Max(HitFlash, HealFlash);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(PanelWidth, PanelHeight), FSlateLayoutTransform(Top)), PanelBrush.Get(), ESlateDrawEffect::None, FLinearColor(0.02f, 0.03f, 0.05f, 0.78f * Alpha));
		FSlateDrawElement::MakeBox(Out, Layer + 1, Geometry.ToPaintGeometry(FVector2f(PanelWidth, PanelHeight), FSlateLayoutTransform(Top)), PanelEdgeBrush.Get(), ESlateDrawEffect::None,
			FMath::Lerp(FLinearColor(1, 1, 1, 0.16f * Alpha), FLinearColor(Flash.R, Flash.G, Flash.B, 0.9f), FlashAmount));

		DrawBar(Out, Layer + 2, Geometry, FVector2f(20.0f, Top.Y + 16.0f), ShownShield, TrailShield, ShieldHitFlash, ShieldHealFlash, FLinearColor(0.18f, 0.58f, 1.0f), V->GetShield(), TEXT("ESCUDO"), Alpha);
		DrawBar(Out, Layer + 2, Geometry, FVector2f(20.0f, Top.Y + 52.0f), ShownHealth, TrailHealth, HitFlash, HealFlash, FLinearColor(0.30f, 0.92f, 0.38f), V->GetHealth(), TEXT("VIDA"), Alpha);

		// floating numbers
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 17);
		for (const FPopup& Popup : Popups)
		{
			const float T = Popup.Age / 1.1f;
			const FLinearColor Color(Popup.Color.R, Popup.Color.G, Popup.Color.B, (1.0f - T) * Alpha);
			const FVector2f Where(Popup.X, Top.Y - 4.0f - 26.0f * T + Popup.Y);
			FSlateDrawElement::MakeText(Out, Layer + 6, Geometry.ToPaintGeometry(FVector2f(80, 24), FSlateLayoutTransform(Where + FVector2f(1, 1))), Popup.Text, Font, ESlateDrawEffect::None, FLinearColor(0, 0, 0, Color.A * 0.7f));
			FSlateDrawElement::MakeText(Out, Layer + 7, Geometry.ToPaintGeometry(FVector2f(80, 24), FSlateLayoutTransform(Where)), Popup.Text, Font, ESlateDrawEffect::None, Color);
		}
		return Layer + 8;
	}

private:

	struct FPopup
	{
		FString Text;
		FLinearColor Color;
		float X = 0.0f;
		float Y = 0.0f;
		float Age = 0.0f;
	};

	void DrawBar(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& At, float Shown, float Trail, float HitAmount, float HealAmount, const FLinearColor& Color, float Real, const TCHAR* Label, float Alpha) const
	{
		const float Width = PanelWidth - 40.0f;
		const float Height = 20.0f;
		const auto Paint = [&](const FVector2f& Size, const FVector2f& Offset) { return Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(At + Offset)); };

		// dark track
		FSlateDrawElement::MakeBox(Out, Layer, Paint(FVector2f(Width, Height), FVector2f::ZeroVector), BarBrush.Get(), ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.09f * Alpha));
		// the white trail left behind by lost points
		const float TrailWidth = FMath::Max(Width * Trail / 100.0f, 0.0f);
		if (TrailWidth > Width * Shown / 100.0f + 1.0f)
		{
			FSlateDrawElement::MakeBox(Out, Layer + 1, Paint(FVector2f(FMath::Max(TrailWidth, 14.0f), Height), FVector2f::ZeroVector), BarBrush.Get(), ESlateDrawEffect::None, FLinearColor(1.0f, 0.95f, 0.9f, 0.85f * Alpha));
		}
		// the bar itself: a body and a lighter strip on the top half for a glassy look
		const float FillWidth = Width * FMath::Clamp(Shown / 100.0f, 0.0f, 1.0f);
		if (FillWidth > 1.0f)
		{
			const float W = FMath::Max(FillWidth, 14.0f);
			FLinearColor Body = Color * (0.82f + 0.4f * HealAmount);
			Body.A = Alpha;
			FSlateDrawElement::MakeBox(Out, Layer + 2, Paint(FVector2f(W, Height), FVector2f::ZeroVector), BarBrush.Get(), ESlateDrawEffect::None, Body);
			FSlateDrawElement::MakeBox(Out, Layer + 3, Paint(FVector2f(W - 8.0f, Height * 0.38f), FVector2f(4.0f, 2.5f)), BarBrush.Get(), ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.28f * Alpha));
			// healing: a bright sweep that runs along the bar
			if (HealAmount > 0.0f)
			{
				const float Sweep = (1.0f - HealAmount) * (W - 24.0f);
				FSlateDrawElement::MakeBox(Out, Layer + 4, Paint(FVector2f(30.0f, Height), FVector2f(Sweep, 0.0f)), BarBrush.Get(), ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.65f * HealAmount * Alpha));
			}
		}
		// the edge: red when hit, bright when healed
		if (HitAmount > 0.0f || HealAmount > 0.0f)
		{
			const FLinearColor Edge = HitAmount >= HealAmount ? FLinearColor(1.0f, 0.2f, 0.15f, HitAmount) : FLinearColor(0.6f, 1.0f, 0.7f, HealAmount);
			FSlateDrawElement::MakeBox(Out, Layer + 5, Paint(FVector2f(Width, Height), FVector2f::ZeroVector), BarEdgeBrush.Get(), ESlateDrawEffect::None, Edge);
		}
		// label on the left, number on the right
		const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Bold", 10);
		const FSlateFontInfo Big = FCoreStyle::GetDefaultFontStyle("Bold", 13);
		const FString Number = FString::FromInt(FMath::CeilToInt(Real));
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f Size = FVector2f(Measure->Measure(Number, Big));
		FSlateDrawElement::MakeText(Out, Layer + 6, Paint(FVector2f(80, 16), FVector2f(12.0f, 4.0f)), Label, Small, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.72f * Alpha));
		FSlateDrawElement::MakeText(Out, Layer + 6, Paint(FVector2f(60, 18), FVector2f(Width - Size.X - 12.0f + 1.0f, 2.0f + 1.0f)), Number, Big, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f * Alpha));
		FSlateDrawElement::MakeText(Out, Layer + 7, Paint(FVector2f(60, 18), FVector2f(Width - Size.X - 12.0f, 2.0f)), Number, Big, ESlateDrawEffect::None, FLinearColor(1, 1, 1, Alpha));
	}

	EActiveTimerReturnType Animate(double Now, float Delta)
	{
		const UArenaVitalsComponent* V = Vitals.Get();
		if (!V)
		{
			return EActiveTimerReturnType::Continue;
		}
		Delta = FMath::Min(Delta, 0.1f);
		const float TargetHealth = V->GetHealth();
		const float TargetShield = V->GetShield();

		// what changed since the last frame decides the effect
		const auto Notice = [this](float Before, float After, float& Hit, float& Heal, float X, const FLinearColor& Color)
		{
			if (After < Before - 0.01f)
			{
				Hit = 1.0f;
				Popups.Add({ FString::Printf(TEXT("-%d"), FMath::RoundToInt(Before - After)), FLinearColor(1.0f, 0.3f, 0.25f), X, 0.0f, 0.0f });
			}
			else if (After > Before + 0.01f)
			{
				Heal = 1.0f;
				Popups.Add({ FString::Printf(TEXT("+%d"), FMath::RoundToInt(After - Before)), Color, X, 0.0f, 0.0f });
			}
		};
		Notice(LastShield, TargetShield, ShieldHitFlash, ShieldHealFlash, PanelWidth - 96.0f, FLinearColor(0.45f, 0.78f, 1.0f));
		Notice(LastHealth, TargetHealth, HitFlash, HealFlash, PanelWidth - 56.0f, FLinearColor(0.5f, 1.0f, 0.55f));
		LastHealth = TargetHealth;
		LastShield = TargetShield;

		// quick follow for the bar, slow follow for the trail (after a short pause)
		const auto Follow = [Delta](float& Shown, float& Trail, float& Pause, float Target)
		{
			Shown = FMath::FInterpTo(Shown, Target, Delta, 9.0f);
			if (Target < Trail)
			{
				Pause = FMath::Max(Pause, 0.0f);
				if (Pause <= 0.0f)
				{
					Trail = FMath::FInterpConstantTo(Trail, Target, Delta, 38.0f);
				}
			}
			else
			{
				Trail = Target;
			}
		};
		const bool bHealthDropped = TargetHealth < TrailHealth - 0.01f;
		const bool bShieldDropped = TargetShield < TrailShield - 0.01f;
		if (HitFlash >= 0.99f && bHealthDropped) HealthPause = 0.35f;
		if (ShieldHitFlash >= 0.99f && bShieldDropped) ShieldPause = 0.35f;
		HealthPause -= Delta;
		ShieldPause -= Delta;
		Follow(ShownHealth, TrailHealth, HealthPause, TargetHealth);
		Follow(ShownShield, TrailShield, ShieldPause, TargetShield);

		for (float* Value : { &HitFlash, &HealFlash, &ShieldHitFlash, &ShieldHealFlash })
		{
			*Value = FMath::Max(0.0f, *Value - Delta * 2.4f);
		}
		for (FPopup& Popup : Popups)
		{
			Popup.Age += Delta;
		}
		Popups.RemoveAll([](const FPopup& Popup) { return Popup.Age > 1.1f; });
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}

	static constexpr float PanelWidth = 390.0f;
	static constexpr float PanelHeight = 92.0f;

	TWeakObjectPtr<UArenaVitalsComponent> Vitals;
	TSharedPtr<FSlateRoundedBoxBrush> PanelBrush, PanelEdgeBrush, BarBrush, BarEdgeBrush;

	float ShownHealth = 100.0f, TrailHealth = 100.0f, LastHealth = 100.0f;
	float ShownShield = 100.0f, TrailShield = 100.0f, LastShield = 100.0f;
	float HitFlash = 0.0f, HealFlash = 0.0f, ShieldHitFlash = 0.0f, ShieldHealFlash = 0.0f;
	float HealthPause = 0.0f, ShieldPause = 0.0f;
	TArray<FPopup> Popups;
};

// ------------------------------------------------------------------------------------------------ the component

UArenaVitalsComponent::UArenaVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UArenaVitalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UArenaVitalsComponent, Health, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UArenaVitalsComponent, Shield, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UArenaVitalsComponent, bDead, COND_OwnerOnly);
}

float UArenaVitalsComponent::TakeHit(float Amount, AController* Instigator)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || Amount <= 0.0f)
	{
		return 0.0f;
	}
	if (Instigator)
	{
		LastDamager = Instigator;
	}
	float Remaining = Amount;
	const float Absorbed = FMath::Min(Shield, Remaining);
	Shield -= Absorbed;
	Remaining -= Absorbed;
	Health = FMath::Max(0.0f, Health - Remaining);
	if (Health <= 0.0f)
	{
		bDead = true;
		OnDied.Broadcast();
	}
	return Amount;
}

void UArenaVitalsComponent::Heal(float Amount, float Cap)
{
	if (GetOwner() && GetOwner()->HasAuthority() && !bDead)
	{
		Health = FMath::Max(Health, FMath::Min(Health + Amount, FMath::Min(Cap, MaxHealth)));
	}
}

void UArenaVitalsComponent::AddShield(float Amount, float Cap)
{
	if (GetOwner() && GetOwner()->HasAuthority() && !bDead)
	{
		Shield = FMath::Max(Shield, FMath::Min(Shield + Amount, FMath::Min(Cap, MaxShield)));
	}
}

void UArenaVitalsComponent::ResetVitals()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Health = MaxHealth;
		Shield = MaxShield;
		bDead = false;
	}
}

void UArenaVitalsComponent::BeginPlay()
{
	Super::BeginPlay();
	AddHud();
}

void UArenaVitalsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveHud();
	Super::EndPlay(EndPlayReason);
}

void UArenaVitalsComponent::AddHud()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (Hud.IsValid() || !PC || !PC->IsLocalController() || !GEngine || !GEngine->GameViewport || !GetWorld() || GetWorld()->GetMapName().Contains(TEXT("Lobby")))
	{
		return;
	}
	Hud = SNew(SArenaVitalsHud).Vitals(this);
	// bottom left corner, like the Fortnite one
	HudWrapper = SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(34.0f, 0.0f, 0.0f, 28.0f)).Visibility(EVisibility::HitTestInvisible)[Hud.ToSharedRef()];
	GEngine->GameViewport->AddViewportWidgetContent(HudWrapper.ToSharedRef(), 30);
}

void UArenaVitalsComponent::RemoveHud()
{
	if (HudWrapper.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudWrapper.ToSharedRef());
	}
	HudWrapper.Reset();
	Hud.Reset();
}

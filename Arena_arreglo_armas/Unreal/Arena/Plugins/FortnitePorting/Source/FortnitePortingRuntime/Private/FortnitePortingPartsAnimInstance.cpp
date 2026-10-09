#include "FortnitePortingPartsAnimInstance.h"

void UFortnitePortingPartsAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	NextBlink = FMath::FRandRange(0.5f, BlinkInterval.Y);
	BlinkTime = -1.0f;
	BlinkAlpha = 0.0f;
}

void UFortnitePortingPartsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!bBlink)
	{
		BlinkAlpha = 0.0f;
		return;
	}

	if (BlinkTime < 0.0f)
	{
		NextBlink -= DeltaSeconds;
		if (NextBlink <= 0.0f)
		{
			BlinkTime = 0.0f;
		}
		BlinkAlpha = 0.0f;
		return;
	}

	// Close fast, hold a moment, open a bit slower (ease in/out), like Fortnite's idle blink
	BlinkTime += DeltaSeconds;
	const float Close = FMath::Max(BlinkCloseTime, 0.01f);
	const float Hold = FMath::Max(BlinkHoldTime, 0.0f);
	const float Open = FMath::Max(BlinkOpenTime, 0.01f);
	if (BlinkTime < Close)
	{
		BlinkAlpha = FMath::InterpEaseIn(0.0f, 1.0f, BlinkTime / Close, 2.0f);
	}
	else if (BlinkTime < Close + Hold)
	{
		BlinkAlpha = 1.0f;
	}
	else if (BlinkTime < Close + Hold + Open)
	{
		BlinkAlpha = FMath::InterpEaseOut(1.0f, 0.0f, (BlinkTime - Close - Hold) / Open, 2.0f);
	}
	else
	{
		BlinkAlpha = 0.0f;
		BlinkTime = -1.0f;
		// Now and then a quick double blink
		NextBlink = FMath::FRand() < 0.15f ? 0.12f : FMath::FRandRange(BlinkInterval.X, BlinkInterval.Y);
	}
}

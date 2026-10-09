#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "FortnitePortingPartsAnimInstance.generated.h"

/**
 * Parent class of the generated ABP_<Skin>_Parts (head, face accessories, tails...).
 * Drives the eye blink: the generated graph adds Fortnite's blink eyelid offsets scaled by BlinkAlpha.
 */
UCLASS()
class FORTNITEPORTINGRUNTIME_API UFortnitePortingPartsAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 0 = eyes open, 1 = closed */
	UPROPERTY(BlueprintReadOnly, Category = "Face")
	float BlinkAlpha = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	bool bBlink = true;

	/** Seconds between blinks (random in this range) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FVector2D BlinkInterval = FVector2D(2.2f, 5.5f);

	/** Seconds to close, keep closed and open the eyes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	float BlinkCloseTime = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	float BlinkHoldTime = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	float BlinkOpenTime = 0.1f;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	float NextBlink = 3.0f;
	float BlinkTime = -1.0f;
};

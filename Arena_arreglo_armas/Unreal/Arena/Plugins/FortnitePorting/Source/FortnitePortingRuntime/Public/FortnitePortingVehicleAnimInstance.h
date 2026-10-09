#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "FortnitePortingVehicleAnimInstance.generated.h"

/**
 * Anim instance of a vehicle mesh with no authored graph: ref pose plus spinning wheels (tire_* bones) and
 * steering fork/wheels (wheel_steering_* bones). Driven by AFortnitePortingVehicle.
 */
UCLASS()
class FORTNITEPORTINGRUNTIME_API UFortnitePortingVehicleAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Accumulated wheel rotation in degrees */
	UPROPERTY(BlueprintReadWrite, Category = "Vehicle")
	float WheelAngle = 0.0f;

	/** Steering angle in degrees, positive to the right */
	UPROPERTY(BlueprintReadWrite, Category = "Vehicle")
	float SteerAngle = 0.0f;

	/** Mesh-space axis the wheels spin around (the vehicle's lateral axis as authored in the mesh) */
	UPROPERTY(BlueprintReadWrite, Category = "Vehicle")
	FVector WheelAxis = FVector(0.0f, 1.0f, 0.0f);

	/** Folds the kickstand away (scaled to nothing) while somebody rides */
	UPROPERTY(BlueprintReadWrite, Category = "Vehicle")
	bool bHideKickstand = false;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

struct FFortnitePortingVehicleAnimProxy : public FAnimInstanceProxy
{
	FFortnitePortingVehicleAnimProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	float WheelAngle = 0.0f;
	float SteerAngle = 0.0f;
	FVector WheelAxis = FVector(0.0f, 1.0f, 0.0f);
	bool bHideKickstand = false;
};

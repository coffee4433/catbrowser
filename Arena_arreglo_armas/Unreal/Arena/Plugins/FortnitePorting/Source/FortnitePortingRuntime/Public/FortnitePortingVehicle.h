#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputCoreTypes.h"
#include "FortnitePortingVehicle.generated.h"

class ACharacter;
class UBoxComponent;
class UTextRenderComponent;
class UCameraComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UFortnitePortingVehicleData;

/**
 * Parent of the generated BP_<Vehicle>: a simple arcade car (no physics setup required).
 * A Fortnite Porting character presses E next to it to drive, and E again to get out.
 */
UCLASS(Blueprintable)
class FORTNITEPORTINGRUNTIME_API AFortnitePortingVehicle : public APawn
{
	GENERATED_BODY()

public:
	AFortnitePortingVehicle();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	TObjectPtr<UFortnitePortingVehicleData> VehicleData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<USceneComponent> MeshPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<USkeletalMeshComponent> VehicleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UStaticMeshComponent> VehicleStaticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UCameraComponent> Camera;

	/** "[E] Subir" floating over the vehicle while the local player is close enough to get in */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vehicle")
	TObjectPtr<UTextRenderComponent> EnterPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey ThrottleKey = EKeys::W;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey ReverseKey = EKeys::S;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey LeftKey = EKeys::A;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey RightKey = EKeys::D;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey HandbrakeKey = EKeys::SpaceBar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle|Controls")
	FKey ExitKey = EKeys::E;

	/** How far from the vehicle bounds a character can be to get in */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle")
	float EnterDistance = 250.0f;

	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	void ApplyVehicleData();

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool CanEnter(const AActor* Character) const;

	/** Attaches the character to the driver seat and takes control. Called by the character component. */
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	bool SetDriver(ACharacter* Character);

	/** A second character sits behind the driver (when the vehicle has a passenger seat) */
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	bool SetPassenger(ACharacter* Character);

	/** Detaches the passenger and returns where they should stand */
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	FVector ReleasePassenger();

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	ACharacter* GetPassenger() const { return Passenger; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool HasPassengerSeat() const;

	/** Detaches the driver and returns where they should stand */
	UFUNCTION(BlueprintCallable, Category = "Vehicle")
	FVector ReleaseDriver();

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	ACharacter* GetDriver() const { return Driver; }

	/** Driver input of the last frame: steering -1..1, throttle -1..1 (negative reverses) and handbrake */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	float GetSteerInput() const { return SteerInput; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	float GetThrottleInput() const { return ThrottleInput; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool IsBraking() const { return bBrakeInput; }

	/** Forward speed in cm/s */
	UFUNCTION(BlueprintPure, Category = "Vehicle")
	float GetSpeed() const { return Speed; }

	/**
	 * Online, the driver's machine simulates the car (like the walking character it replaces) and streams its transform
	 * here; the server applies it and replicates it to everybody else.
	 */
	UFUNCTION(Server, Unreliable)
	void Server_UpdateTransform(FVector_NetQuantize100 Location, FRotator Rotation, float InSpeed);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void HandleDriving(float DeltaSeconds);
	void AttachDriver() const;
	void AttachPassenger() const;
	void UpdateEnterPrompt();
	void FitDriverToHandles(float DeltaSeconds);
	void UpdateWheels(float DeltaSeconds, float ForwardDistance);
	float DistanceToBounds(const FVector& Location) const;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Driver;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> Passenger;

	/** Where the driver ended up relative to the Driver socket once its hands met the grips (vehicle space) */
	FVector DriverFitOffset = FVector::ZeroVector;

	float Speed = 0.0f;
	float SteerInput = 0.0f;
	float ThrottleInput = 0.0f;
	bool bBrakeInput = false;
	float Lean = 0.0f;
	float VerticalSpeed = 0.0f;
	float TimeSinceEnter = 0.0f;
};

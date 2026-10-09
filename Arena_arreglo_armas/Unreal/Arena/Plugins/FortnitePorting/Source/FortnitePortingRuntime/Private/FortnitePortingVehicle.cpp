#include "FortnitePortingVehicle.h"

#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "FortnitePortingVehicleAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	// The box floats this far above the ground so horizontal sweeps never start inside the floor
	constexpr float GroundClearance = 10.0f;
}

AFortnitePortingVehicle::AFortnitePortingVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetBoxExtent(FVector(220.0f, 100.0f, 70.0f));
	Collision->SetCollisionProfileName(TEXT("Vehicle"));
	SetRootComponent(Collision);

	MeshPivot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshPivot"));
	MeshPivot->SetupAttachment(Collision);

	VehicleMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VehicleMesh"));
	VehicleMesh->SetupAttachment(MeshPivot);
	VehicleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	VehicleStaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VehicleStaticMesh"));
	VehicleStaticMesh->SetupAttachment(MeshPivot);
	VehicleStaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	EnterPrompt = CreateDefaultSubobject<UTextRenderComponent>(TEXT("EnterPrompt"));
	EnterPrompt->SetupAttachment(Collision);
	EnterPrompt->SetText(FText::FromString(TEXT("[E] Subir")));
	EnterPrompt->SetHorizontalAlignment(EHTA_Center);
	EnterPrompt->SetWorldSize(32.0f);
	EnterPrompt->SetTextRenderColor(FColor::White);
	EnterPrompt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EnterPrompt->SetVisibility(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Collision);
	CameraBoom->TargetArmLength = 700.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 0.0f, 180.0f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
}

void AFortnitePortingVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVehicleData();
}

void AFortnitePortingVehicle::BeginPlay()
{
	Super::BeginPlay();
	ApplyVehicleData();
}

void AFortnitePortingVehicle::ApplyVehicleData()
{
	MeshPivot->SetRelativeRotation(FRotator(0.0f, VehicleData ? VehicleData->MeshYawOffset : 0.0f, 0.0f));

	// Fit the collision box to whichever mesh the generated blueprint uses
	FBoxSphereBounds LocalBounds;
	if (const USkeletalMesh* SkeletalMesh = VehicleMesh->GetSkeletalMeshAsset())
	{
		LocalBounds = SkeletalMesh->GetBounds();
	}
	else if (const UStaticMesh* StaticMesh = VehicleStaticMesh->GetStaticMesh())
	{
		LocalBounds = StaticMesh->GetBounds();
	}
	else
	{
		return;
	}

	const FVector Extent = MeshPivot->GetRelativeRotation().RotateVector(LocalBounds.BoxExtent).GetAbs();
	const FVector Center = MeshPivot->GetRelativeRotation().RotateVector(LocalBounds.Origin);
	Collision->SetBoxExtent(FVector(Extent.X, Extent.Y, FMath::Max(Extent.Z * 0.5f, 30.0f)));

	// The box is centered on the actor; line the bottom of the mesh up with the bottom of the box
	const float BoxHalfHeight = Collision->GetUnscaledBoxExtent().Z;
	MeshPivot->SetRelativeLocation(FVector(-Center.X, -Center.Y, -BoxHalfHeight - GroundClearance - (Center.Z - Extent.Z)));

	// Vehicles with wheel bones get spinning wheels; others loop the mesh's own idle animation
	if (const USkeletalMesh* SkeletalMesh = VehicleMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& Ref = SkeletalMesh->GetRefSkeleton();
		bool bHasWheels = false;
		for (int32 Index = 0; Index < Ref.GetNum() && !bHasWheels; ++Index)
		{
			bHasWheels = Ref.GetBoneName(Index).ToString().StartsWith(TEXT("tire_"));
		}

		if (bHasWheels)
		{
			VehicleMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			VehicleMesh->SetAnimInstanceClass(UFortnitePortingVehicleAnimInstance::StaticClass());

		}
		else if (VehicleData && VehicleData->VehicleIdleAnimation)
		{
			VehicleMesh->PlayAnimation(VehicleData->VehicleIdleAnimation, true);
		}
	}
}

float AFortnitePortingVehicle::DistanceToBounds(const FVector& Location) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Location);
	const FVector Extent = Collision->GetUnscaledBoxExtent();
	const FVector Outside(FMath::Max(FMath::Abs(Local.X) - Extent.X, 0.0f), FMath::Max(FMath::Abs(Local.Y) - Extent.Y, 0.0f), FMath::Max(FMath::Abs(Local.Z) - Extent.Z * 3.0f, 0.0f));
	return Outside.Size();
}

bool AFortnitePortingVehicle::CanEnter(const AActor* Character) const
{
	if (Character == nullptr || Character == Driver || Character == Passenger)
	{
		return false;
	}

	const bool bFreeSeat = Driver == nullptr || (Passenger == nullptr && HasPassengerSeat());
	return bFreeSeat && DistanceToBounds(Character->GetActorLocation()) <= EnterDistance;
}

bool AFortnitePortingVehicle::HasPassengerSeat() const
{
	return VehicleMesh && VehicleMesh->GetSkeletalMeshAsset() && VehicleMesh->DoesSocketExist(TEXT("Passenger"));
}

void AFortnitePortingVehicle::AttachPassenger() const
{
	if (Passenger == nullptr)
	{
		return;
	}

	// Passenger poses share the rider convention of the driver ones: same offset from the seat socket as the driver ended up with
	Passenger->AttachToComponent(VehicleMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("Passenger"));
	FTransform Offset;
	Offset.SetLocation(DriverFitOffset);
	if (const USkeletalMeshComponent* Body = Passenger->GetMesh())
	{
		Offset.AddToTranslation(-Body->GetRelativeLocation());
	}
	Passenger->SetActorRelativeTransform(Offset);
}

bool AFortnitePortingVehicle::SetPassenger(ACharacter* Character)
{
	if (Character == nullptr || Passenger != nullptr || !HasPassengerSeat())
	{
		return false;
	}

	Passenger = Character;
	AttachPassenger();
	return true;
}

FVector AFortnitePortingVehicle::ReleasePassenger()
{
	ACharacter* Old = Passenger;
	Passenger = nullptr;

	const FVector Extent = Collision->GetUnscaledBoxExtent();
	FVector ExitLocation = GetActorLocation() + GetActorRightVector() * (Extent.Y + 90.0f) + FVector(0.0f, 0.0f, 60.0f);
	if (Old)
	{
		Old->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		const float Radius = Old->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = Old->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingVehiclePassengerExit), false, this);
		Params.AddIgnoredActor(Old);
		if (GetWorld()->OverlapBlockingTestByChannel(ExitLocation, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		{
			ExitLocation = GetActorLocation() - GetActorRightVector() * (Extent.Y + 90.0f) + FVector(0.0f, 0.0f, 60.0f);
		}
	}

	return ExitLocation;
}

void AFortnitePortingVehicle::AttachDriver() const
{
	if (Driver == nullptr)
	{
		return;
	}

	USceneComponent* SeatParent = VehicleMesh->GetSkeletalMeshAsset() ? static_cast<USceneComponent*>(VehicleMesh.Get()) : static_cast<USceneComponent*>(VehicleStaticMesh.Get());
	FName Socket = NAME_None;
	if (VehicleData)
	{
		for (const FName Candidate : VehicleData->SeatSockets)
		{
			if (SeatParent->DoesSocketExist(Candidate))
			{
				Socket = Candidate;
				break;
			}
		}
	}

	Driver->AttachToComponent(SeatParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);

	// The seat is where the body mesh origin (feet/root bone) goes, the capsule sits above it
	FTransform Offset = VehicleData ? VehicleData->DriverOffset : FTransform::Identity;
	if (const USkeletalMeshComponent* Body = Driver->GetMesh())
	{
		Offset.AddToTranslation(-Body->GetRelativeLocation());
	}
	Driver->SetActorRelativeTransform(Offset);
}

bool AFortnitePortingVehicle::SetDriver(ACharacter* Character)
{
	if (Character == nullptr || Driver != nullptr)
	{
		return false;
	}

	Driver = Character;
	TimeSinceEnter = 0.0f;
	Speed = 0.0f;
	AttachDriver();

	// Possession is server-only; the other machines just mirror who sits in the car
	if (HasAuthority())
	{
		if (AController* DriverController = Character->GetController())
		{
			const FRotator ViewRotation = DriverController->GetControlRotation();
			DriverController->Possess(this);
			DriverController->SetControlRotation(ViewRotation);
		}
	}

	return true;
}

FVector AFortnitePortingVehicle::ReleaseDriver()
{
	ACharacter* OldDriver = Driver;
	Driver = nullptr;
	Speed = 0.0f;

	const FVector Extent = Collision->GetUnscaledBoxExtent();
	FVector ExitLocation = GetActorLocation() - GetActorRightVector() * (Extent.Y + 90.0f) + FVector(0.0f, 0.0f, 60.0f);

	if (OldDriver)
	{
		OldDriver->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (AController* DriverController = HasAuthority() ? GetController() : nullptr)
		{
			const FRotator ViewRotation = DriverController->GetControlRotation();
			DriverController->Possess(OldDriver);
			DriverController->SetControlRotation(ViewRotation);
		}

		// Prefer the left side, fall back to the right if something is in the way
		const float Radius = OldDriver->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float HalfHeight = OldDriver->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingVehicleExit), false, this);
		Params.AddIgnoredActor(OldDriver);
		if (GetWorld()->OverlapBlockingTestByChannel(ExitLocation, FQuat::Identity, ECC_Pawn, Shape, Params))
		{
			ExitLocation = GetActorLocation() + GetActorRightVector() * (Extent.Y + 90.0f) + FVector(0.0f, 0.0f, 60.0f);
		}
	}

	return ExitLocation;
}

void AFortnitePortingVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeSinceEnter += DeltaSeconds;
	const FVector Before = GetActorLocation();
	HandleDriving(DeltaSeconds);
	UpdateWheels(DeltaSeconds, FVector::DotProduct(GetActorLocation() - Before, GetActorForwardVector()));

	// The seated pose blends in over the first moments: keep correcting until it settles
	if (Driver != nullptr && TimeSinceEnter > 0.1f && TimeSinceEnter < 1.6f)
	{
		FitDriverToHandles(DeltaSeconds);
	}

	// The passenger sits where the driver ended up relative to its seat
	if (Passenger != nullptr && Passenger->GetAttachParentActor() == this && !DriverFitOffset.IsNearlyZero())
	{
		FTransform Offset;
		Offset.SetLocation(DriverFitOffset);
		if (const USkeletalMeshComponent* Body = Passenger->GetMesh())
		{
			Offset.AddToTranslation(-Body->GetRelativeLocation());
		}
		Passenger->SetActorRelativeTransform(Offset);
	}
	UpdateEnterPrompt();
}

void AFortnitePortingVehicle::FitDriverToHandles(float DeltaSeconds)
{
	// The driving poses are authored for the original rider: move this body (any size) so its hands land on the grips
	// the vehicle declares, instead of trusting a fixed seat offset
	const USkeletalMeshComponent* Body = Driver ? Driver->GetMesh() : nullptr;
	if (Body == nullptr || !VehicleMesh->DoesSocketExist(TEXT("steering_wheel_hand_l")) || !VehicleMesh->DoesSocketExist(TEXT("steering_wheel_hand_r"))
		|| !Body->DoesSocketExist(TEXT("hand_l")) || !Body->DoesSocketExist(TEXT("hand_r")))
	{
		return;
	}

	const FVector Target = (VehicleMesh->GetSocketLocation(TEXT("steering_wheel_hand_l")) + VehicleMesh->GetSocketLocation(TEXT("steering_wheel_hand_r"))) * 0.5f;
	const FVector Current = (Body->GetSocketLocation(TEXT("hand_l")) + Body->GetSocketLocation(TEXT("hand_r"))) * 0.5f;

	// Only along the vehicle's length and height: bodies are narrower than the grips, the rider stays centered
	FVector Local = GetActorTransform().InverseTransformVectorNoScale(Target - Current);
	Local.Y = 0.0f;
	const FVector Delta = GetActorTransform().TransformVectorNoScale(Local).GetClampedToMaxSize(150.0f);
	Driver->AddActorWorldOffset(Delta * FMath::Clamp(DeltaSeconds * 10.0f, 0.0f, 1.0f));

	// Where the driver's origin sits relative to the Driver socket now, for the passenger to reuse
	if (VehicleMesh->DoesSocketExist(TEXT("Driver")))
	{
		const FTransform Seat = VehicleMesh->GetSocketTransform(TEXT("Driver"));
		DriverFitOffset = Seat.InverseTransformPositionNoScale(Body->GetComponentLocation());
	}
}

void AFortnitePortingVehicle::UpdateWheels(float DeltaSeconds, float ForwardDistance)
{
	UFortnitePortingVehicleAnimInstance* Wheels = Cast<UFortnitePortingVehicleAnimInstance>(VehicleMesh->GetAnimInstance());
	if (Wheels == nullptr || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Wheels spin around the lateral axis of the mesh as authored (Y, or X when authored sideways)
	Wheels->WheelAxis = FRotator(0.0f, -(VehicleData ? VehicleData->MeshYawOffset : 0.0f), 0.0f).RotateVector(FVector::RightVector);

	Wheels->bHideKickstand = Driver != nullptr;

	// Distance rolled / wheel radius; the speed is measured so passengers' and remote machines' wheels turn too
	constexpr float WheelRadius = 33.0f;
	// Spinning as fast as the car travels turns the wheels ~70 degrees per frame at top speed, which matches the angle
	// between spokes and makes them look still: cap the visual rate so they always read as turning
	constexpr float MaxSpinPerSecond = 900.0f;
	const float SpinDelta = FMath::Clamp(FMath::RadiansToDegrees(ForwardDistance / WheelRadius), -MaxSpinPerSecond * DeltaSeconds, MaxSpinPerSecond * DeltaSeconds);
	Wheels->WheelAngle = FMath::Fmod(Wheels->WheelAngle + SpinDelta, 360.0f);

	const float TargetSteer = SteerInput * 28.0f;
	Wheels->SteerAngle = FMath::FInterpTo(Wheels->SteerAngle, TargetSteer, DeltaSeconds, 10.0f);
}

void AFortnitePortingVehicle::UpdateEnterPrompt()
{
	if (EnterPrompt == nullptr || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	const APlayerController* Local = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	const APawn* LocalPawn = Local ? Local->GetPawn() : nullptr;
	const bool bShow = LocalPawn && LocalPawn != this && CanEnter(LocalPawn);
	EnterPrompt->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}

	// Above the vehicle, turned toward the player's camera
	const FVector Extent = Collision->GetScaledBoxExtent();
	EnterPrompt->SetWorldLocation(GetActorLocation() + FVector(0.0f, 0.0f, Extent.Z + 130.0f));
	if (const APlayerCameraManager* Cameras = Local->PlayerCameraManager)
	{
		const FVector ToCamera = Cameras->GetCameraLocation() - EnterPrompt->GetComponentLocation();
		EnterPrompt->SetWorldRotation(FRotator(0.0f, ToCamera.Rotation().Yaw, 0.0f));
	}
}

void AFortnitePortingVehicle::HandleDriving(float DeltaSeconds)
{
	APlayerController* DriverController = Cast<APlayerController>(GetController());
	const bool bHasDriver = Driver != nullptr && DriverController != nullptr && DriverController->IsLocalController();

	// Online, only the driver's machine simulates a driven car; everybody else receives its transform. An empty car
	// is simulated by the server alone (it settles on the ground) and replicated.
	if (GetNetMode() != NM_Standalone && !bHasDriver && (Driver != nullptr || !HasAuthority()))
	{
		return;
	}

	float Throttle = 0.0f;
	float Steer = 0.0f;
	bool bHandbrake = false;
	if (bHasDriver)
	{
		Throttle = (DriverController->IsInputKeyDown(ThrottleKey) ? 1.0f : 0.0f) - (DriverController->IsInputKeyDown(ReverseKey) ? 1.0f : 0.0f);
		Steer = (DriverController->IsInputKeyDown(RightKey) ? 1.0f : 0.0f) - (DriverController->IsInputKeyDown(LeftKey) ? 1.0f : 0.0f);
		bHandbrake = DriverController->IsInputKeyDown(HandbrakeKey);

		float MouseX = 0.0f;
		float MouseY = 0.0f;
		DriverController->GetInputMouseDelta(MouseX, MouseY);
		AddControllerYawInput(MouseX);
		AddControllerPitchInput(-MouseY);

		// Ignore the same E press that got us in
		if (TimeSinceEnter > 0.5f && DriverController->WasInputKeyJustPressed(ExitKey))
		{
			if (UFortnitePortingCharacterComponent* Component = Driver->FindComponentByClass<UFortnitePortingCharacterComponent>())
			{
				Component->RequestExitVehicle();
				return;
			}
		}
	}

	SteerInput = Steer;
	ThrottleInput = Throttle;
	bBrakeInput = bHandbrake || (Throttle < 0.0f && Speed > 100.0f);

	const float MaxSpeed = VehicleData ? VehicleData->MaxSpeed : 2500.0f;
	const float Acceleration = VehicleData ? VehicleData->Acceleration : 1100.0f;
	const float Brake = VehicleData ? VehicleData->BrakeDeceleration : 2800.0f;
	const float TurnRate = VehicleData ? VehicleData->TurnRate : 75.0f;

	if (bHandbrake)
	{
		Speed = FMath::FInterpConstantTo(Speed, 0.0f, DeltaSeconds, Brake * 1.5f);
	}
	else if (Throttle > 0.0f)
	{
		Speed += (Speed < 0.0f ? Brake : Acceleration) * DeltaSeconds;
	}
	else if (Throttle < 0.0f)
	{
		Speed -= (Speed > 0.0f ? Brake : Acceleration * 0.6f) * DeltaSeconds;
	}
	else
	{
		Speed = FMath::FInterpConstantTo(Speed, 0.0f, DeltaSeconds, 600.0f);
	}
	Speed = FMath::Clamp(Speed, -MaxSpeed * 0.35f, MaxSpeed);

	// Steering needs movement, and flips when reversing like a real car
	const float SteerFactor = FMath::Clamp(FMath::Abs(Speed) / 500.0f, 0.0f, 1.0f) * FMath::Sign(Speed);
	AddActorWorldRotation(FRotator(0.0f, Steer * TurnRate * SteerFactor * DeltaSeconds, 0.0f));

	// Two-wheelers lean into the turn
	if (VehicleData && VehicleData->MaxLeanAngle > 0.0f && MeshPivot)
	{
		const float TargetLean = Steer * SteerFactor * VehicleData->MaxLeanAngle;
		Lean = FMath::FInterpTo(Lean, TargetLean, DeltaSeconds, 5.0f);
		// Roll around the vehicle's forward axis whichever way the mesh is authored
		MeshPivot->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Lean)) * FRotator(0.0f, VehicleData->MeshYawOffset, 0.0f).Quaternion());
	}

	FHitResult Hit;
	AddActorWorldOffset(GetActorForwardVector() * Speed * DeltaSeconds, true, &Hit);
	if (Hit.bBlockingHit)
	{
		Speed *= -0.2f;
	}

	// Stick to the ground and follow its slope
	const float HalfHeight = Collision->GetScaledBoxExtent().Z;
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, HalfHeight + 50.0f);
	const FVector End = GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight + 5000.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FortnitePortingVehicleGround), false, this);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}

	FHitResult Ground;
	if (GetWorld()->LineTraceSingleByChannel(Ground, Start, End, ECC_Visibility, Params))
	{
		const float TargetZ = Ground.ImpactPoint.Z + HalfHeight + GroundClearance;
		FVector Location = GetActorLocation();
		if (Location.Z - TargetZ > 20.0f)
		{
			VerticalSpeed -= 980.0f * DeltaSeconds;
			Location.Z = FMath::Max(TargetZ, Location.Z + VerticalSpeed * DeltaSeconds);
		}
		else
		{
			VerticalSpeed = 0.0f;
			Location.Z = TargetZ;
		}
		SetActorLocation(Location);

		const FRotator Current = GetActorRotation();
		const FRotator Aligned = FRotationMatrix::MakeFromZX(Ground.ImpactNormal, GetActorForwardVector()).Rotator();
		SetActorRotation(FMath::RInterpTo(Current, FRotator(Aligned.Pitch, Current.Yaw, Aligned.Roll), DeltaSeconds, 6.0f));
	}

	// A client driving online streams the result to the server (the host's own car is replicated by the engine)
	if (bHasDriver && !HasAuthority())
	{
		Server_UpdateTransform(GetActorLocation(), GetActorRotation(), Speed);
	}
}

void AFortnitePortingVehicle::Server_UpdateTransform_Implementation(FVector_NetQuantize100 Location, FRotator Rotation, float InSpeed)
{
	if (Driver == nullptr)
	{
		return;
	}

	// Plausibility check: a car cannot cover this much ground between two updates
	if (FVector::DistSquared(Location, GetActorLocation()) > FMath::Square(1500.0f))
	{
		return;
	}

	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	Speed = InSpeed;
}

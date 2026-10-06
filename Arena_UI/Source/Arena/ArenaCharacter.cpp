// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaCharacter.h"
#include "ArenaVitalsComponent.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Arena.h"
#include "ArenaGameMode.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"

AArenaCharacter::AArenaCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Crouch setup - agacharse con Control Izquierdo
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCharacterMovement()->CrouchedHalfHeight = 60.f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 250.f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	// Fortnite framing: close over-the-shoulder camera, character left of the crosshair
	CameraBoom->TargetArmLength = 260.0f;
	CameraBoom->SocketOffset = FVector(0.0f, 55.0f, 75.0f);
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(80.0f);

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AArenaCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AArenaCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AArenaCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AArenaCharacter::Look);

		// Crouching - Control Izquierdo para agacharse/levantarse
		if (CrouchAction)
		{
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &AArenaCharacter::DoCrouchStart);
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &AArenaCharacter::DoCrouchEnd);
		}
	}
	else
	{
		UE_LOG(LogArena, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AArenaCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AArenaCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AArenaCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AArenaCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AArenaCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AArenaCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}

void AArenaCharacter::DoCrouchStart()
{
	// Control Izquierdo alterna: una pulsación agacha, la siguiente levanta (como Fortnite)
	if (bIsCrouched || (GetCharacterMovement() && GetCharacterMovement()->bWantsToCrouch))
	{
		UnCrouch();
	}
	else if (CanCrouch())
	{
		Crouch();
	}
}

void AArenaCharacter::DoCrouchEnd()
{
	// Soltar la tecla ya no levanta: el agachado es de tipo alternar
}

// ── Health / death (server authoritative) ──────────────────────────────────────

void AArenaCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Health = MaxHealth;
	}
}

void AArenaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AArenaCharacter, Health);
	DOREPLIFETIME(AArenaCharacter, bDead);
}

float AArenaCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bDead || Applied <= 0.0f)
	{
		return Applied;
	}

	// a player (100 health + 100 shield) takes the hit in the vitals of its controller, which also run the kill and the respawn
	if (AController* Own = GetController())
	{
		if (UArenaVitalsComponent* Vitals = Own->FindComponentByClass<UArenaVitalsComponent>())
		{
			const float Taken = Vitals->TakeHit(Applied, EventInstigator);
			if (Vitals->IsDead())
			{
				bDead = true;
				OnRep_Dead();
				DetachFromControllerPendingDestroy();
				SetLifeSpan(6.0f);
			}
			return Taken;
		}
	}

	Health = FMath::Clamp(Health - Applied, 0.0f, MaxHealth);
	OnRep_Health();

	if (Health <= 0.0f)
	{
		HandleDeath(EventInstigator);
	}
	return Applied;
}

void AArenaCharacter::HandleDeath(AController* Killer)
{
	AController* Victim = GetController();
	bDead = true;
	OnRep_Dead();

	// Frees the controller so the game mode can respawn it, and removes the body after a while
	DetachFromControllerPendingDestroy();
	SetLifeSpan(6.0f);

	if (AArenaGameMode* GameMode = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GameMode->OnPlayerKilled(Victim, Killer);
	}
}

void AArenaCharacter::OnRep_Health()
{
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void AArenaCharacter::OnRep_Dead()
{
	if (bDead)
	{
		EnterRagdoll();
	}
}

void AArenaCharacter::EnterRagdoll()
{
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetCollisionProfileName(TEXT("Ragdoll"));
		Body->SetSimulatePhysics(true);
	}
}

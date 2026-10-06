// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "ArenaCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AArenaCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Crouch Input Action (Left Control) */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* CrouchAction;

public:

	/** Constructor */
	AArenaCharacter();	

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Starting and maximum health */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Health")
	float MaxHealth = 100.0f;

	/** Replicated to everybody (health bars above heads); only the server changes it */
	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category="Health")
	float Health = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category="Health")
	bool bDead = false;

	UFUNCTION(BlueprintPure, Category="Health")
	bool IsDead() const { return bDead; }

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, NewHealth, float, NewMaxHealth);

	/** Fired on every machine whenever the health changes (HUD hook) */
	UPROPERTY(BlueprintAssignable, Category="Health")
	FOnHealthChanged OnHealthChanged;

protected:

	virtual void BeginPlay() override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Handles crouch pressed (agacharse con Control Izquierdo) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoCrouchStart();

	/** Handles crouch released (levantarse) */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoCrouchEnd();

private:

	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Dead();

	void HandleDeath(AController* Killer);
	void EnterRagdoll();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};


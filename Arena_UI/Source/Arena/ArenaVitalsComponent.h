// Health and shield of a player (100 and 100 at the start of a match), with the modern bottom left HUD that shows them.
// Lives on the player controller so it works with any skin; the pawn forwards the damage it takes (ACombatCharacter::TakeDamage).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArenaVitalsComponent.generated.h"

class SWidget;

UCLASS(ClassGroup = (Arena), meta = (BlueprintSpawnableComponent))
class ARENA_API UArenaVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UArenaVitalsComponent();

	static constexpr float MaxHealth = 100.0f;
	static constexpr float MaxShield = 100.0f;

	float GetHealth() const { return Health; }
	float GetShield() const { return Shield; }
	bool IsDead() const { return bDead; }

	/** Server: the shield takes the hit first, then the health. Returns how much damage was taken */
	float TakeHit(float Amount, AController* Instigator = nullptr);

	/** Who dealt the last hit (the killer when the health reaches zero) */
	AController* GetLastDamager() const { return LastDamager.Get(); }

	/** Server: heals up to Cap (a bandage stops at 75 in the game, potions go on until 100) */
	void Heal(float Amount, float Cap = MaxHealth);

	/** Server: adds shield up to Cap (mini shields stop at 50, the big one at 100) */
	void AddShield(float Amount, float Cap = MaxShield);

	/** Server: full health and shield, alive again */
	void ResetVitals();

	/** Broadcast on the server when the health reaches zero */
	FSimpleMulticastDelegate OnDied;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Console: damage the pawn yourself, for testing the bar */
	void DebugHurt(float Amount) { TakeHit(Amount); }

private:

	void AddHud();
	void RemoveHud();

	UPROPERTY(Replicated)
	float Health = MaxHealth;

	UPROPERTY(Replicated)
	float Shield = MaxShield;

	UPROPERTY(Replicated)
	bool bDead = false;

	TWeakObjectPtr<AController> LastDamager;

	TSharedPtr<SWidget> Hud;
	TSharedPtr<SWidget> HudWrapper;
};

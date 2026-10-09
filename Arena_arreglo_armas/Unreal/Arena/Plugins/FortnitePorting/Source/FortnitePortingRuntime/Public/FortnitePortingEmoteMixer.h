#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FortnitePortingEmoteMixer.generated.h"

class UFortnitePortingCharacterComponent;

/**
 * Decides which emote music this machine hears, so several dancers never pile up into noise.
 *
 *  - The local player's own emote is kept at full volume.
 *  - One other player's music is also audible: the closest one, quieter the farther it is and ducked while a local emote plays.
 *  - Every change is a fade, never a cut: slow when music comes in, quicker when it goes out.
 */
UCLASS()
class FORTNITEPORTINGRUNTIME_API UFortnitePortingEmoteMixer : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** An emote with music started playing on this machine */
	void Register(UFortnitePortingCharacterComponent* Component);

	/** Its music stopped */
	void Unregister(UFortnitePortingCharacterComponent* Component);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UFortnitePortingEmoteMixer, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && !Active.IsEmpty(); }

	/** Within this distance (cm) another player's music is at full volume */
	static constexpr float NearDistance = 1000.0f;

	/** Beyond this distance it is silent */
	static constexpr float FarDistance = 5000.0f;

	/** Seconds a song takes to fade in when it becomes the one that is heard */
	static constexpr float FadeInTime = 1.5f;

	/** Seconds it takes to fade out when another song takes over */
	static constexpr float FadeOutTime = 0.5f;

private:
	void UpdateGains();
	float DistanceGain(const UFortnitePortingCharacterComponent* Component, const FVector& Listener) const;
	bool GetListenerLocation(FVector& OutLocation) const;

	TArray<TWeakObjectPtr<UFortnitePortingCharacterComponent>> Active;
	TWeakObjectPtr<UFortnitePortingCharacterComponent> RemoteWinner;
	float TimeSinceUpdate = 0.0f;
};

#include "FortnitePortingEmoteMixer.h"

#include "FortnitePortingCharacterComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float UpdateInterval = 0.1f;

	// A challenger must be clearly louder than the song that is being heard before it takes over (no flip-flopping)
	constexpr float SwitchMargin = 1.25f;
	constexpr float SwitchOffset = 0.05f;
}

void UFortnitePortingEmoteMixer::Register(UFortnitePortingCharacterComponent* Component)
{
	Active.AddUnique(Component);
	TimeSinceUpdate = UpdateInterval; // decide on the next tick
}

void UFortnitePortingEmoteMixer::Unregister(UFortnitePortingCharacterComponent* Component)
{
	Active.Remove(Component);
	if (RemoteWinner == Component)
	{
		RemoteWinner.Reset();
	}
	TimeSinceUpdate = UpdateInterval; // whoever was muted is brought in on the next tick
}

void UFortnitePortingEmoteMixer::Tick(float DeltaTime)
{
	TimeSinceUpdate += DeltaTime;
	if (TimeSinceUpdate < UpdateInterval)
	{
		return;
	}

	TimeSinceUpdate = 0.0f;
	UpdateGains();
}

bool UFortnitePortingEmoteMixer::GetListenerLocation(FVector& OutLocation) const
{
	if (const APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0))
	{
		OutLocation = Controller->GetFocalLocation();
		return true;
	}
	return false;
}

float UFortnitePortingEmoteMixer::DistanceGain(const UFortnitePortingCharacterComponent* Component, const FVector& Listener) const
{
	const AActor* Owner = Component ? Component->GetOwner() : nullptr;
	if (Owner == nullptr)
	{
		return 0.0f;
	}

	const float Distance = static_cast<float>(FVector::Dist(Owner->GetActorLocation(), Listener));
	const float Linear = FMath::Clamp(1.0f - (Distance - NearDistance) / (FarDistance - NearDistance), 0.0f, 1.0f);
	return Linear * Linear; // loudness drops faster than distance, like a real source
}

void UFortnitePortingEmoteMixer::UpdateGains()
{
	Active.RemoveAll([](const TWeakObjectPtr<UFortnitePortingCharacterComponent>& Weak) { return !Weak.IsValid(); });
	if (Active.IsEmpty())
	{
		return;
	}

	bool bLocalPlaying = false;
	for (const TWeakObjectPtr<UFortnitePortingCharacterComponent>& Weak : Active)
	{
		bLocalPlaying |= Weak->IsEmoteLocal();
	}

	FVector Listener = FVector::ZeroVector;
	const bool bHasListener = GetListenerLocation(Listener);

	// Keep one nearby remote emote audible even while the local player's own emote is playing.
	if (bHasListener)
	{
		UFortnitePortingCharacterComponent* Best = nullptr;
		float BestGain = 0.0f;
		for (const TWeakObjectPtr<UFortnitePortingCharacterComponent>& Weak : Active)
		{
			const float Gain = DistanceGain(Weak.Get(), Listener);
			if (Gain > BestGain)
			{
				BestGain = Gain;
				Best = Weak.Get();
			}
		}

		if (const UFortnitePortingCharacterComponent* Current = RemoteWinner.Get(); Current != nullptr && Best != Current)
		{
			if (BestGain <= DistanceGain(Current, Listener) * SwitchMargin + SwitchOffset)
			{
				Best = RemoteWinner.Get();
			}
		}
		RemoteWinner = Best;
	}
	else
	{
		RemoteWinner.Reset();
	}

	for (const TWeakObjectPtr<UFortnitePortingCharacterComponent>& Weak : Active)
	{
		UFortnitePortingCharacterComponent* Component = Weak.Get();
		float Target = 0.0f;
		if (Component->IsEmoteLocal())
		{
			Target = 1.0f;
		}
		else if (Component == RemoteWinner.Get())
		{
			Target = DistanceGain(Component, Listener) * (bLocalPlaying ? 0.6f : 1.0f);
		}

		const float Current = Component->GetEmoteMusicGain();
		float Fade = 0.2f; // small adjustments (the dancer walks closer or away) glide
		if (FMath::Abs(Target - Current) >= 0.15f)
		{
			// Your own song comes in at once; somebody else's fades in slowly, and goes out quickly
			Fade = Target > Current ? (Component->IsEmoteLocal() ? 0.15f : FadeInTime) : FadeOutTime;
		}
		Component->SetEmoteMusicGain(Target, Fade);
	}
}

#pragma once

#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"

namespace ArenaUISounds
{
	inline USoundBase* LoadCachedSound(TStrongObjectPtr<USoundBase>& Cache, const TCHAR* AssetPath)
	{
		if (!Cache.IsValid())
		{
			Cache.Reset(LoadObject<USoundBase>(nullptr, AssetPath));
			if (!Cache.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("Could not load Arena UI sound: %s"), AssetPath);
			}
		}
		return Cache.Get();
	}

	inline void PlayHover(const UObject* WorldContextObject)
	{
		static TStrongObjectPtr<USoundBase> Sound;
		if (USoundBase* Asset = LoadCachedSound(Sound, TEXT("/Game/Free_UI/WAV/Abstract2.Abstract2")))
		{
			UGameplayStatics::PlaySound2D(WorldContextObject, Asset, 0.10f);
		}
	}

	inline void PlayNotification(const UObject* WorldContextObject)
	{
		static TStrongObjectPtr<USoundBase> Sound;
		if (USoundBase* Asset = LoadCachedSound(Sound, TEXT("/Game/Arena/UI/Sounds/S_AlertNotification.S_AlertNotification")))
		{
			UGameplayStatics::PlaySound2D(WorldContextObject, Asset, 0.30f);
		}
	}
}

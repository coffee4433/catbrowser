#include "FortnitePortingCosmeticData.h"

#include "Animation/AnimMontage.h"

FName FortnitePortingProfiles::GetMediumProfile(FName Profile)
{
	FString Gender, Size;
	if (!Profile.ToString().Split(TEXT("_"), &Gender, &Size))
	{
		return DefaultProfile;
	}

	return FName(*(Gender + TEXT("_Medium")));
}

UAnimMontage* UFortnitePortingCosmeticData::FindForProfile(const TMap<FName, TObjectPtr<UAnimMontage>>& Montages, FName Profile)
{
	for (const FName Candidate : { Profile, FortnitePortingProfiles::GetMediumProfile(Profile), FortnitePortingProfiles::DefaultProfile })
	{
		if (const TObjectPtr<UAnimMontage>* Found = Montages.Find(Candidate); Found && *Found)
		{
			return *Found;
		}
	}

	for (const auto& Pair : Montages)
	{
		if (Pair.Value)
		{
			return Pair.Value;
		}
	}

	return nullptr;
}

UFortnitePortingGliderData::UFortnitePortingGliderData()
{
	Glider.Socket = NAME_None;
}

void UFortnitePortingWeaponData::ApplyTypeDefaults()
{
	struct FStats { const TCHAR* Type; bool bAutomatic; float FireInterval; float Damage; int32 Magazine; float Reload; };
	static const FStats Table[] =
	{
		{ TEXT("Rifle"), true, 0.15f, 30.0f, 30, 2.2f },
		{ TEXT("SMG"), true, 0.08f, 17.0f, 30, 2.0f },
		{ TEXT("Shotgun"), false, 0.9f, 90.0f, 5, 4.5f },
		{ TEXT("Pistol"), false, 0.2f, 24.0f, 16, 1.4f },
		{ TEXT("Sniper"), false, 1.5f, 110.0f, 1, 2.8f },
		{ TEXT("Launcher"), false, 1.2f, 100.0f, 1, 3.0f },
		{ TEXT("Bow"), false, 1.0f, 80.0f, 0, 0.0f },
		{ TEXT("Melee"), false, 0.6f, 40.0f, 0, 0.0f },
		{ TEXT("Consumable"), false, 1.0f, 0.0f, 0, 0.0f }
	};

	for (const FStats& Stats : Table)
	{
		if (WeaponType == FName(Stats.Type))
		{
			bAutomatic = Stats.bAutomatic;
			FireInterval = Stats.FireInterval;
			Damage = Stats.Damage;
			MagazineSize = Stats.Magazine;
			ReloadTime = Stats.Reload;
			Range = WeaponType == TEXT("Melee") ? 250.0f : 15000.0f;
			return;
		}
	}
}

void UFortnitePortingEmoteData::AnalyzeMusicLoop(const UAnimMontage* Montage)
{
	bLoopAnalyzed = true;
	bLoopsWithMusic = false;
	LoopSection = NAME_None;
	MasterSound = INDEX_NONE;
	DancePlayRate = 1.0f;

	// Silent gestures play once. Dances with music repeat until the player moves, music and animation together.
	if (Montage == nullptr || Sounds.IsEmpty() || Montage->CompositeSections.IsEmpty())
	{
		return;
	}

	// The section to repeat: one that already links to itself, else one named "loop", else the last one
	const TArray<FCompositeSection>& Sections = Montage->CompositeSections;
	int32 LoopIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Sections.Num() && LoopIndex == INDEX_NONE; ++Index)
	{
		if (Sections[Index].NextSectionName == Sections[Index].SectionName)
		{
			LoopIndex = Index;
		}
	}
	for (int32 Index = 0; Index < Sections.Num() && LoopIndex == INDEX_NONE; ++Index)
	{
		if (Sections[Index].SectionName.ToString().Contains(TEXT("loop"), ESearchCase::IgnoreCase))
		{
			LoopIndex = Index;
		}
	}
	if (LoopIndex == INDEX_NONE)
	{
		LoopIndex = Sections.Num() - 1;
	}

	float SectionStart = 0.0f;
	float SectionEnd = 0.0f;
	Montage->GetSectionStartAndEndTime(LoopIndex, SectionStart, SectionEnd);
	const float LapLength = SectionEnd - SectionStart;
	if (LapLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	LoopSection = Sections[LoopIndex].SectionName;
	bLoopsWithMusic = true;

	// The music sets the pace: the longest track
	float MasterDuration = 0.0f;
	for (int32 SoundIndex = 0; SoundIndex < Sounds.Num(); ++SoundIndex)
	{
		if (const USoundWave* Wave = Cast<USoundWave>(Sounds[SoundIndex].Sound))
		{
			if (Wave->Duration > MasterDuration)
			{
				MasterDuration = Wave->Duration;
				MasterSound = SoundIndex;
			}
		}
	}

	// k laps of the dance = m loops of the music, within a few percent of normal speed
	if (MasterSound != INDEX_NONE)
	{
		float BestError = 0.07f;
		for (int32 Laps = 1; Laps <= 4; ++Laps)
		{
			for (int32 Loops = 1; Loops <= 4; ++Loops)
			{
				const float Rate = (Laps * LapLength) / (Loops * MasterDuration);
				if (FMath::Abs(Rate - 1.0f) < BestError)
				{
					BestError = FMath::Abs(Rate - 1.0f);
					DancePlayRate = Rate;
				}
			}
		}
	}
}

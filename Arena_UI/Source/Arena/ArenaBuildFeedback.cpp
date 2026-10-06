#include "ArenaBuildFeedback.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace ArenaBuildFeedback
{
	USoundBase* LoadSound(const TCHAR* Name)
	{
		static TMap<FString, TWeakObjectPtr<USoundBase>> Cache;
		const FString Key(Name);
		if (const TWeakObjectPtr<USoundBase>* Found = Cache.Find(Key))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Arena/Sounds/Build/%s.%s"), Name, Name));
		Cache.Add(Key, Sound);
		return Sound;
	}

	/** The Fortnite cues are mastered loud: every building sound is played at this fraction of its volume */
	constexpr float BuildVolume = 0.3f;

	void PlayAt(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Volume)
	{
		if (USoundBase* Sound = LoadSound(Name))
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, Volume * BuildVolume);
		}
	}

	void Play2D(const UObject* WorldContext, const TCHAR* Name, float Volume)
	{
		if (USoundBase* Sound = LoadSound(Name))
		{
			UGameplayStatics::PlaySound2D(WorldContext, Sound, Volume * BuildVolume);
		}
	}

	void PlayBuilt(const UObject* WorldContext, EArenaBuildMaterial Material, const FVector& Location)
	{
		const TCHAR* Family = Material == EArenaBuildMaterial::Brick ? TEXT("Stone") : (Material == EArenaBuildMaterial::Metal ? TEXT("Metal") : TEXT("Wood"));
		PlayAt(WorldContext, *FString::Printf(TEXT("Fort_Construction_Done_%s_%02d"), Family, FMath::RandRange(1, 3)), Location, 0.9f);
	}

	void PlayAnimation(APawn* Pawn, EAnim Kind)
	{
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		UAnimInstance* Instance = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (!Instance)
		{
			return;
		}
		// the animations were imported once per Fortnite skeleton
		FString Skeleton = TEXT("Male");
		if (const USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset())
		{
			if (const USkeleton* Asset = SkeletalMesh->GetSkeleton())
			{
				const FString Name = Asset->GetName();
				Skeleton = Name.Contains(TEXT("Female_Large")) ? TEXT("FemaleLarge") : (Name.Contains(TEXT("Female")) ? TEXT("Female") : TEXT("Male"));
			}
		}
		const TCHAR* Base = Kind == EAnim::Place ? TEXT("M_BluePrint_Place") : (Kind == EAnim::Edit ? TEXT("BluePrint_Edit_ConfirmCheck") : TEXT("Blueprint_Equip"));
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Arena/Anim/Build/%s/%s.%s"), *Skeleton, Base, Base));
		if (!Sequence)
		{
			return;
		}
		Instance->PlaySlotAnimationAsDynamicMontage(Sequence, FName(TEXT("UpperBody")), 0.06f, 0.18f, Kind == EAnim::Place ? 1.6f : 1.0f);
	}
}

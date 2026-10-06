// The sounds and the animations of building, all taken from Fortnite (FortnitePorting exports): the wood/stone/metal "construction done" thunks,
// the blueprint tool cues of the edit mode and the character's own building animations.

#pragma once

#include "CoreMinimal.h"
#include "ArenaBuildTypes.h"

class APawn;
class UAnimSequence;
class USkeletalMeshComponent;
class USoundBase;

namespace ArenaBuildFeedback
{
	enum class EAnim : uint8
	{
		Place,
		Edit,
		Equip,
	};

	/** A sound from /Game/Arena/Sounds/Build by name (nullptr when it was not imported) */
	ARENA_API USoundBase* LoadSound(const TCHAR* Name);

	/** Plays a build sound inside the world (3D) or on the player's ears (2D) */
	ARENA_API void PlayAt(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Volume = 1.0f);
	ARENA_API void Play2D(const UObject* WorldContext, const TCHAR* Name, float Volume = 1.0f);

	/** The sound of a freshly built piece: the construction thunk of its material, one of three */
	ARENA_API void PlayBuilt(const UObject* WorldContext, EArenaBuildMaterial Material, const FVector& Location);

	/** The character's building animation, on the upper body so the legs keep running. Works on every skin that shares the Fortnite skeleton */
	ARENA_API void PlayAnimation(APawn* Pawn, EAnim Kind);
}

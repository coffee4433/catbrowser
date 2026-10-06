// Remembers the outfit picked in the lobby locker

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ArenaLockerSave.generated.h"

UCLASS()
class ARENA_API UArenaLockerSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Character blueprint class worn by the player */
	UPROPERTY(SaveGame)
	FSoftClassPath Outfit;

	/** Option picked in each style channel of the outfit (empty: the outfit's own look) */
	UPROPERTY(SaveGame)
	TArray<int32> OutfitStyles;

	/** Pickaxe and glider data assets picked in the locker */
	UPROPERTY(SaveGame)
	FSoftObjectPath Pickaxe;

	UPROPERTY(SaveGame)
	FSoftObjectPath Glider;
};

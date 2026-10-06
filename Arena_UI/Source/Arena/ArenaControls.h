// The keys of the game the player can change in the settings: movement, jump and everything about building.
// Saved in GameUserSettings.ini; the building keys apply at once, the movement keys patch the input mapping context.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class APlayerController;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;

namespace ArenaControls
{
	enum class EAction : uint8
	{
		Forward, Back, Left, Right, Jump,
		Build, Wall, Floor, Stair, Roof, Edit, Rotate,
		Interact, Emotes, Pickaxe, Inventory,
		Reload,
		Count
	};

	struct FActionInfo
	{
		const TCHAR* Id;
		FText Label;
		FText Group;
		FKey Default;
	};

	ARENA_API const FActionInfo& Info(EAction Action);

	ARENA_API FKey Get(EAction Action);

	/** Changes a key (and saves it). When another action had that key, the two swap so nothing is left without a key */
	ARENA_API void Set(EAction Action, const FKey& Key);

	ARENA_API void ResetAll();

	/** Short, upper case and in Spanish: G, ESPACIO, CLIC IZQ. */
	ARENA_API FText KeyName(const FKey& Key);

	ARENA_API bool WasPressed(const APlayerController* PC, EAction Action);

	/** Puts the saved movement / jump keys into the input mapping contexts the controller uses */
	ARENA_API void ApplyToInputContexts(UEnhancedInputLocalPlayerSubsystem* Subsystem, const TArray<UInputMappingContext*>& Contexts);

	DECLARE_MULTICAST_DELEGATE(FOnControlsChanged);
	ARENA_API FOnControlsChanged& OnChanged();
}

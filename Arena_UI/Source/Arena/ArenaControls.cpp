#include "ArenaControls.h"

#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "ArenaControls"

namespace ArenaControls
{
	namespace
	{
		const TCHAR* Section = TEXT("ArenaControls");

		const FActionInfo& InfoAt(int32 Index)
		{
			static const TArray<FActionInfo> Table =
			{
				{ TEXT("Forward"),  LOCTEXT("Forward", "Avanzar"),                  LOCTEXT("GMove", "MOVIMIENTO"),    EKeys::W },
				{ TEXT("Back"),     LOCTEXT("Back", "Retroceder"),                 LOCTEXT("GMove", "MOVIMIENTO"),    EKeys::S },
				{ TEXT("Left"),     LOCTEXT("Left", "Izquierda"),                  LOCTEXT("GMove", "MOVIMIENTO"),    EKeys::A },
				{ TEXT("Right"),    LOCTEXT("Right", "Derecha"),                   LOCTEXT("GMove", "MOVIMIENTO"),    EKeys::D },
				{ TEXT("Jump"),     LOCTEXT("Jump", "Saltar"),                     LOCTEXT("GMove", "MOVIMIENTO"),    EKeys::SpaceBar },
				{ TEXT("Build"),    LOCTEXT("Build", "Modo construcción"),         LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::Q },
				{ TEXT("Wall"),     LOCTEXT("Wall", "Pared"),                      LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::Z },
				{ TEXT("Floor"),    LOCTEXT("Floor", "Suelo"),                     LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::X },
				{ TEXT("Stair"),    LOCTEXT("Stair", "Escalera"),                  LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::C },
				{ TEXT("Roof"),     LOCTEXT("Roof", "Tejado"),                     LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::V },
				{ TEXT("Edit"),     LOCTEXT("Edit", "Editar construcción"),        LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::G },
				{ TEXT("Rotate"),   LOCTEXT("Rotate", "Girar pieza"),              LOCTEXT("GBuild", "CONSTRUCCIÓN"), EKeys::R },
				{ TEXT("Interact"), LOCTEXT("Interact", "Interactuar (puertas)"),  LOCTEXT("GOther", "OTROS"),        EKeys::E },
				{ TEXT("Emotes"),   LOCTEXT("Emotes", "Rueda de gestos"),          LOCTEXT("GOther", "OTROS"),        EKeys::B },
				{ TEXT("Pickaxe"),  LOCTEXT("Pickaxe", "Sacar o guardar el pico"), LOCTEXT("GOther", "OTROS"),        EKeys::One },
				{ TEXT("Inventory"), LOCTEXT("Inventory", "Abrir inventario"),    LOCTEXT("GOther", "OTROS"),        EKeys::Tab },
				{ TEXT("Reload"),   LOCTEXT("Reload", "Recargar arma"),           LOCTEXT("GOther", "OTROS"),        EKeys::R },
			};
			return Table[Index];
		}

		TArray<FKey>& Cache()
		{
			static TArray<FKey> Keys;
			if (Keys.IsEmpty())
			{
				for (int32 Index = 0; Index < static_cast<int32>(EAction::Count); ++Index)
				{
					FString Saved;
					FKey Key = InfoAt(Index).Default;
					if (GConfig && GConfig->GetString(Section, InfoAt(Index).Id, Saved, GGameUserSettingsIni) && !Saved.IsEmpty())
					{
						const FKey Loaded(*Saved);
						if (Loaded.IsValid())
						{
							Key = Loaded;
						}
					}
					Keys.Add(Key);
				}
			}
			return Keys;
		}

		void Save()
		{
			if (!GConfig)
			{
				return;
			}
			for (int32 Index = 0; Index < static_cast<int32>(EAction::Count); ++Index)
			{
				GConfig->SetString(Section, InfoAt(Index).Id, *Cache()[Index].ToString(), GGameUserSettingsIni);
			}
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}

	const FActionInfo& Info(EAction Action) { return InfoAt(static_cast<int32>(Action)); }

	FKey Get(EAction Action) { return Cache()[static_cast<int32>(Action)]; }

	void Set(EAction Action, const FKey& Key)
	{
		TArray<FKey>& Keys = Cache();
		const int32 Index = static_cast<int32>(Action);
		if (!Key.IsValid() || Keys[Index] == Key)
		{
			return;
		}
		for (int32 Other = 0; Other < Keys.Num(); ++Other)
		{
			if (Other != Index && Keys[Other] == Key)
			{
				Keys[Other] = Keys[Index];      // swap: the action that lost its key gets the old one
			}
		}
		Keys[Index] = Key;
		Save();
		OnChanged().Broadcast();
	}

	void ResetAll()
	{
		TArray<FKey>& Keys = Cache();
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			Keys[Index] = InfoAt(Index).Default;
		}
		Save();
		OnChanged().Broadcast();
	}

	FText KeyName(const FKey& Key)
	{
		if (Key == EKeys::SpaceBar) { return LOCTEXT("KSpace", "ESPACIO"); }
		if (Key == EKeys::LeftMouseButton) { return LOCTEXT("KLmb", "CLIC IZQ."); }
		if (Key == EKeys::RightMouseButton) { return LOCTEXT("KRmb", "CLIC DER."); }
		if (Key == EKeys::MiddleMouseButton) { return LOCTEXT("KMmb", "CLIC CENTRAL"); }
		if (Key == EKeys::LeftShift) { return LOCTEXT("KLShift", "MAYÚS IZQ."); }
		if (Key == EKeys::RightShift) { return LOCTEXT("KRShift", "MAYÚS DER."); }
		if (Key == EKeys::LeftControl) { return LOCTEXT("KLCtrl", "CTRL IZQ."); }
		if (Key == EKeys::LeftAlt) { return LOCTEXT("KLAlt", "ALT IZQ."); }
		if (Key == EKeys::CapsLock) { return LOCTEXT("KCaps", "BLOQ MAYÚS"); }
		FString Name = Key.GetDisplayName(false).ToString();
		if (Name.IsEmpty())
		{
			Name = Key.ToString();
		}
		return FText::FromString(Name.ToUpper());
	}

	bool WasPressed(const APlayerController* PC, EAction Action)
	{
		return PC && PC->WasInputKeyJustPressed(Get(Action));
	}

	void ApplyToInputContexts(UEnhancedInputLocalPlayerSubsystem* Subsystem, const TArray<UInputMappingContext*>& Contexts)
	{
		// The mappings of the movement contexts are found once by their original key (W, A, S, D, space) and remembered by position,
		// so changing a key again later still knows which entry it is
		struct FSlot
		{
			TWeakObjectPtr<UInputMappingContext> Context;
			int32 Index;
			EAction Action;
		};
		static TArray<FSlot> Slots;
		static bool bScanned = false;
		if (!bScanned)
		{
			for (UInputMappingContext* Context : Contexts)
			{
				if (!Context)
				{
					continue;
				}
				bScanned = true;
				const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
				for (int32 Index = 0; Index < Mappings.Num(); ++Index)
				{
					const FEnhancedActionKeyMapping& Mapping = Mappings[Index];
					const FString ActionName = Mapping.Action ? Mapping.Action->GetName() : FString();
					EAction Found = EAction::Count;
					if (ActionName == TEXT("IA_Move"))
					{
						if (Mapping.Key == EKeys::W) { Found = EAction::Forward; }
						else if (Mapping.Key == EKeys::S) { Found = EAction::Back; }
						else if (Mapping.Key == EKeys::A) { Found = EAction::Left; }
						else if (Mapping.Key == EKeys::D) { Found = EAction::Right; }
					}
					else if (ActionName == TEXT("IA_Jump") && Mapping.Key == EKeys::SpaceBar)
					{
						Found = EAction::Jump;
					}
					if (Found != EAction::Count)
					{
						Slots.Add({ Context, Index, Found });
					}
				}
			}
		}

		for (const FSlot& Slot : Slots)
		{
			if (UInputMappingContext* Context = Slot.Context.Get())
			{
				// the modifiers (swizzle, negate) stay as they are: only the key changes
				TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(Context->GetMappings());
				if (Mappings.IsValidIndex(Slot.Index))
				{
					Mappings[Slot.Index].Key = Get(Slot.Action);
				}
			}
		}
		if (Subsystem)
		{
			Subsystem->RequestRebuildControlMappings();
		}
	}

	FOnControlsChanged& OnChanged()
	{
		static FOnControlsChanged Delegate;
		return Delegate;
	}
}

#undef LOCTEXT_NAMESPACE

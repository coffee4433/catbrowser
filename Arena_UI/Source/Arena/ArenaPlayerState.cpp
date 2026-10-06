// LAN multiplayer: replicated per-player state implementation

#include "ArenaPlayerState.h"
#include "ArenaGameMode.h"
#include "FortnitePortingCharacterComponent.h"
#include "FortnitePortingCosmeticData.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Arena.h"

AArenaPlayerState::AArenaPlayerState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	SetActorTickEnabled(false);
	SkinIndex = 0;
	bIsReady = false;
	Kills = 0;
	Deaths = 0;
}

void AArenaPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	if (AArenaPlayerState* NewPlayerState = Cast<AArenaPlayerState>(PlayerState))
	{
		NewPlayerState->SkinIndex = SkinIndex;
		NewPlayerState->SkinClassPath = SkinClassPath;
		NewPlayerState->SkinStyles = SkinStyles;
		NewPlayerState->PickaxePath = PickaxePath;
		NewPlayerState->GliderPath = GliderPath;
	}
}

void AArenaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AArenaPlayerState, SkinIndex);
	DOREPLIFETIME(AArenaPlayerState, bIsReady);
	DOREPLIFETIME(AArenaPlayerState, Kills);
	DOREPLIFETIME(AArenaPlayerState, Deaths);
	DOREPLIFETIME(AArenaPlayerState, SkinClassPath);
	DOREPLIFETIME(AArenaPlayerState, SkinStyles);
	DOREPLIFETIME(AArenaPlayerState, PickaxePath);
	DOREPLIFETIME(AArenaPlayerState, GliderPath);
	DOREPLIFETIME(AArenaPlayerState, bLobbyIdle);
}

// ─── Helper ────────────────────────────────────────────────────────────────

UFortnitePortingCharacterComponent* AArenaPlayerState::GetCosmeticComponent() const
{
	APawn* Pawn = GetPawn();
	return Pawn ? Pawn->FindComponentByClass<UFortnitePortingCharacterComponent>() : nullptr;
}

// ─── Server RPCs ───────────────────────────────────────────────────────────

void AArenaPlayerState::Server_SetSkinIndex_Implementation(int32 NewIndex)
{
	if (NewIndex >= 0)
	{
		SkinIndex = NewIndex;
		OnRep_SkinIndex();

		UE_LOG(LogArena, Log, TEXT("Player %s changed skin to index %d"), *GetPlayerName(), NewIndex);
	}
}

void AArenaPlayerState::Server_SetSkinStyles_Implementation(const TArray<int32>& Styles)
{
	SkinStyles = Styles;
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->SetStyleSelection(SkinStyles);
	}
}

void AArenaPlayerState::Server_SetReady_Implementation(bool bReady)
{
	bIsReady = bReady;
	OnRep_bIsReady();

	UE_LOG(LogArena, Log, TEXT("Player %s is %s"), *GetPlayerName(), bReady ? TEXT("READY") : TEXT("NOT READY"));
}

void AArenaPlayerState::Server_RequestEquipSkin_Implementation(const FString& ClassPath)
{
	if (ClassPath.IsEmpty())
	{
		return;
	}

	// Store the skin path for replication. A new skin starts with its own look: the client sends its styles right after.
	SkinClassPath = ClassPath;
	SkinStyles.Reset();

	// Ask the GameMode to do the authoritative pawn swap
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (PC == nullptr)
	{
		return;
	}

	if (AArenaGameMode* GM = GetWorld()->GetAuthGameMode<AArenaGameMode>())
	{
		GM->ServerEquipSkin(PC, ClassPath);
	}
}

void AArenaPlayerState::Server_SetPickaxe_Implementation(const FString& AssetPath)
{
	PickaxePath = AssetPath;

	// Apply immediately to the current pawn on the server
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		if (UFortnitePortingPickaxeData* Data = LoadObject<UFortnitePortingPickaxeData>(nullptr, *AssetPath))
		{
			Cosmetics->SetPickaxe(Data);
		}
	}

	UE_LOG(LogArena, Log, TEXT("Player %s set pickaxe: %s"), *GetPlayerName(), *AssetPath);
}

void AArenaPlayerState::Server_SetGlider_Implementation(const FString& AssetPath)
{
	GliderPath = AssetPath;

	// Apply immediately to the current pawn on the server
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		if (UFortnitePortingGliderData* Data = LoadObject<UFortnitePortingGliderData>(nullptr, *AssetPath))
		{
			Cosmetics->SetGlider(Data);
		}
	}

	UE_LOG(LogArena, Log, TEXT("Player %s set glider: %s"), *GetPlayerName(), *AssetPath);
}

void AArenaPlayerState::Server_SetLobbyIdle_Implementation(bool bIdle)
{
	bLobbyIdle = bIdle;
	SetActorTickEnabled(bIdle);
	ApplyLobbyIdleToPawn();
	ForceNetUpdate();
}

void AArenaPlayerState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLobbyIdle && GetPawn() != LobbyIdlePawn.Get())
	{
		ApplyLobbyIdleToPawn();
	}
}

void AArenaPlayerState::ApplyLobbyIdleToPawn()
{
	APawn* Pawn = GetPawn();
	UFortnitePortingCharacterComponent* Cosmetics = Pawn
		? Pawn->FindComponentByClass<UFortnitePortingCharacterComponent>()
		: nullptr;
	if (Cosmetics == nullptr)
	{
		return;
	}

	if (bLobbyIdle)
	{
		Cosmetics->PlayLobbyIdle();
		LobbyIdlePawn = Pawn;
	}
	else
	{
		Cosmetics->StopLobbyIdle();
		LobbyIdlePawn.Reset();
	}
}

// ─── Server → Multicast RPCs for cosmetic actions ──────────────────────────

void AArenaPlayerState::Server_PlayEmote_Implementation(const FString& EmoteAssetPath)
{
	// Validate the asset exists
	UFortnitePortingEmoteData* EmoteData = LoadObject<UFortnitePortingEmoteData>(nullptr, *EmoteAssetPath);
	if (EmoteData == nullptr)
	{
		UE_LOG(LogArena, Warning, TEXT("Server_PlayEmote: Failed to load emote %s"), *EmoteAssetPath);
		return;
	}

	// Broadcast to ALL clients (including the server/listen-server local player)
	Multicast_PlayEmote(EmoteAssetPath);
}

void AArenaPlayerState::Server_StopEmote_Implementation()
{
	Multicast_StopEmote();
}

void AArenaPlayerState::Server_DeployGlider_Implementation()
{
	Multicast_DeployGlider();
}

void AArenaPlayerState::Server_StopGlider_Implementation()
{
	Multicast_StopGlider();
}

void AArenaPlayerState::Server_EquipPickaxe_Implementation()
{
	Multicast_EquipPickaxe();
}

void AArenaPlayerState::Server_UnequipPickaxe_Implementation()
{
	Multicast_UnequipPickaxe();
}

void AArenaPlayerState::Server_SwingPickaxe_Implementation()
{
	Multicast_SwingPickaxe();
}

// ─── Multicast RPCs ────────────────────────────────────────────────────────

void AArenaPlayerState::Multicast_PlayEmote_Implementation(const FString& EmoteAssetPath)
{
	UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent();
	if (Cosmetics == nullptr)
	{
		return;
	}

	UFortnitePortingEmoteData* EmoteData = LoadObject<UFortnitePortingEmoteData>(nullptr, *EmoteAssetPath);
	if (EmoteData)
	{
		Cosmetics->PlayEmote(EmoteData);
	}
}

void AArenaPlayerState::Multicast_StopEmote_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->StopEmote();
	}
}

void AArenaPlayerState::Multicast_DeployGlider_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->DeployGlider();
	}
}

void AArenaPlayerState::Multicast_StopGlider_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->StopGliding();
	}
}

void AArenaPlayerState::Multicast_EquipPickaxe_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->EquipPickaxe();
	}
}

void AArenaPlayerState::Multicast_UnequipPickaxe_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->UnequipPickaxe();
	}
}

void AArenaPlayerState::Multicast_SwingPickaxe_Implementation()
{
	if (UFortnitePortingCharacterComponent* Cosmetics = GetCosmeticComponent())
	{
		Cosmetics->SwingPickaxe();
	}
}

// ─── OnRep callbacks ──────────────────────────────────────────────────────

void AArenaPlayerState::OnRep_SkinIndex()
{
	OnSkinChanged.Broadcast(this, SkinIndex);
}

void AArenaPlayerState::OnRep_bIsReady()
{
	OnReadyChanged.Broadcast(this, bIsReady);
}

void AArenaPlayerState::OnRep_bLobbyIdle()
{
	SetActorTickEnabled(bLobbyIdle);
	ApplyLobbyIdleToPawn();
}

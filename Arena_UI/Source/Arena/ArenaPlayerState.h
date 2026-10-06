// LAN multiplayer: replicated per-player state (name, skin, ready status, cosmetics)

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ArenaPlayerState.generated.h"

/**
 * Replicated player state for LAN multiplayer.
 * Each connected player gets one of these, automatically replicated to all clients.
 * Tracks the player's display name, chosen skin, cosmetic items, and ready status.
 */
UCLASS()
class ARENA_API AArenaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AArenaPlayerState();

	virtual void CopyProperties(APlayerState* PlayerState) override;

	/** Index of the skin this player has equipped (matches the locker list) */
	UPROPERTY(ReplicatedUsing = OnRep_SkinIndex, BlueprintReadOnly, Category = "Player")
	int32 SkinIndex;

	/** Whether this player is ready to start the match */
	UPROPERTY(ReplicatedUsing = OnRep_bIsReady, BlueprintReadOnly, Category = "Player")
	bool bIsReady;

	/** Kill count for combat modes */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player")
	int32 Kills;

	/** Death count for combat modes */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player")
	int32 Deaths;

	// ─── Replicated cosmetic identifiers ───────────────────────────
	// These are class/asset paths stored as strings so all clients know what
	// each player has equipped, even though the actual equipping only happens
	// on the server or the owning client.

	/** Blueprint class path of the currently equipped skin pawn (e.g. "/Game/FortnitePorting/Characters/Renegade_Raider/Blueprints/BP_Renegade_Raider.BP_Renegade_Raider_C") */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player|Cosmetics")
	FString SkinClassPath;

	/** Option picked in each style channel of the equipped skin (re-applied when the pawn is swapped or respawned) */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player|Cosmetics")
	TArray<int32> SkinStyles;

	/** Asset path of the equipped pickaxe data asset */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player|Cosmetics")
	FString PickaxePath;

	/** Asset path of the equipped glider data asset */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Player|Cosmetics")
	FString GliderPath;

	/** Whether this player is displaying the lobby idle pose with their pickaxe put away. */
	UPROPERTY(ReplicatedUsing = OnRep_bLobbyIdle, BlueprintReadOnly, Category = "Player|Lobby")
	bool bLobbyIdle = false;

	// ─── Delegates ─────────────────────────────────────────────────

	/** Fired when the skin selection changes */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSkinChanged, AArenaPlayerState*, PlayerState, int32, NewSkinIndex);

	UPROPERTY(BlueprintAssignable, Category = "Player")
	FOnSkinChanged OnSkinChanged;

	/** Fired when the ready status changes */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnReadyChanged, AArenaPlayerState*, PlayerState, bool, bReady);

	UPROPERTY(BlueprintAssignable, Category = "Player")
	FOnReadyChanged OnReadyChanged;

	// ─── Server RPCs (client → server) ─────────────────────────────

	/** Client requests a skin change: the server validates and spawns the new pawn */
	UFUNCTION(Server, Reliable, Category = "Player")
	void Server_SetSkinIndex(int32 NewIndex);

	/** Client picks the style options of the worn skin: stored here and put on the current pawn */
	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_SetSkinStyles(const TArray<int32>& Styles);

	/** Client requests a skin change by class path (for the pawn swap on server) */
	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_RequestEquipSkin(const FString& ClassPath);

	/** Client requests ready toggle */
	UFUNCTION(Server, Reliable, Category = "Player")
	void Server_SetReady(bool bReady);

	/** Client tells the server which pickaxe it chose */
	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_SetPickaxe(const FString& AssetPath);

	/** Client tells the server which glider it chose */
	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_SetGlider(const FString& AssetPath);

	UFUNCTION(Server, Reliable, Category = "Player|Lobby")
	void Server_SetLobbyIdle(bool bIdle);

	// ─── Multicast RPCs (server → all clients) ────────────────────

	/** Tells all clients to play an emote on this player's pawn */
	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_PlayEmote(const FString& EmoteAssetPath);

	/** Tells all clients to stop the current emote */
	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_StopEmote();

	/** Tells all clients to deploy/stop the glider on this player's pawn */
	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_DeployGlider();

	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_StopGlider();

	/** Tells all clients to toggle pickaxe visibility */
	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_EquipPickaxe();

	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_UnequipPickaxe();

	UFUNCTION(NetMulticast, Reliable, Category = "Player|Cosmetics")
	void Multicast_SwingPickaxe();

	// ─── Server RPCs for cosmetic actions (client → server → multicast) ─

	/** Client requests an emote play — the server validates and multicasts */
	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_PlayEmote(const FString& EmoteAssetPath);

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_StopEmote();

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_DeployGlider();

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_StopGlider();

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_EquipPickaxe();

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_UnequipPickaxe();

	UFUNCTION(Server, Reliable, Category = "Player|Cosmetics")
	void Server_SwingPickaxe();

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void OnRep_SkinIndex();

	UFUNCTION()
	void OnRep_bIsReady();

	UFUNCTION()
	void OnRep_bLobbyIdle();

	void ApplyLobbyIdleToPawn();

	/** Helper: finds the FortnitePortingCharacterComponent on the pawn owned by this player state */
	class UFortnitePortingCharacterComponent* GetCosmeticComponent() const;

	TWeakObjectPtr<APawn> LobbyIdlePawn;
};

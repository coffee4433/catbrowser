// LAN multiplayer: replicated game state tracking match status and connected players

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ArenaGameState.generated.h"

/** Match phases managed by the server */
UENUM(BlueprintType)
enum class EArenaMatchState : uint8
{
	/** Players are in the lobby, waiting to start */
	WaitingForPlayers,

	/** The match is in progress */
	InProgress,

	/** The match has ended */
	GameOver
};

/**
 * Replicated game state for LAN multiplayer.
 * Tracks the current match phase, server name, player count, and map.
 * All clients receive updates automatically via property replication.
 */
UCLASS()
class ARENA_API AArenaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AArenaGameState();

	/** Current phase of the match */
	UPROPERTY(ReplicatedUsing = OnRep_MatchState, BlueprintReadOnly, Category = "Match")
	EArenaMatchState MatchState;

	/** Display name of this server in the LAN browser */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	FString ServerName;

	/** Maximum number of players allowed */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	int32 MaxPlayers;

	/** Fired on every client when the match state changes */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchStateChanged, EArenaMatchState, NewState);

	UPROPERTY(BlueprintAssignable, Category = "Match")
	FOnMatchStateChanged OnMatchStateChanged;

	/** Server-only: advance the match state */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Match")
	void SetMatchState(EArenaMatchState NewState);

	/** Convenience: number of connected players (PlayerArray is already replicated by GameStateBase) */
	UFUNCTION(BlueprintPure, Category = "Match")
	int32 GetNumPlayers() const;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_MatchState();
};

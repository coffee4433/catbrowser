// LAN multiplayer: replicated game state implementation

#include "ArenaGameState.h"
#include "Net/UnrealNetwork.h"
#include "Arena.h"

AArenaGameState::AArenaGameState()
{
	MatchState = EArenaMatchState::WaitingForPlayers;
	MaxPlayers = 8;
	ServerName = TEXT("Arena LAN Server");
}

void AArenaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AArenaGameState, MatchState);
	DOREPLIFETIME(AArenaGameState, ServerName);
	DOREPLIFETIME(AArenaGameState, MaxPlayers);
}

void AArenaGameState::SetMatchState(EArenaMatchState NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	if (MatchState != NewState)
	{
		MatchState = NewState;
		OnRep_MatchState();

		UE_LOG(LogArena, Log, TEXT("Match state changed to %d"), static_cast<int32>(NewState));
	}
}

int32 AArenaGameState::GetNumPlayers() const
{
	return PlayerArray.Num();
}

void AArenaGameState::OnRep_MatchState()
{
	OnMatchStateChanged.Broadcast(MatchState);
}

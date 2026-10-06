// In-game Epic Online Services friends: list, requests, search by name, presence and lobby invites

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePresenceInterface.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"
#include "ArenaFriendsSubsystem.generated.h"

/** An accepted friend */
USTRUCT(BlueprintType)
struct FArenaFriendInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString Name;

	/** Opaque id used to act on this friend (invite, remove...) */
	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString NetId;

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	bool bOnline = false;

	/** Presence text shown under the name ("En el lobby", "En partida", "Desconectado") */
	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString Status;

	/** LAN session advertised by an online friend, if they are currently in a lobby */
	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString LobbyName;

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString AvatarUrl;
};

struct FArenaChatMessageInfo
{
	FString Body;
	bool bMine = false;
	FString Timestamp;
};

/** A friend request somebody sent to the local player */
USTRUCT(BlueprintType)
struct FArenaFriendRequestInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString NetId;
};

/** A friend asking the local player to join their lobby */
USTRUCT(BlueprintType)
struct FArenaLobbyInviteInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString SenderName;

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString SenderNetId;

	UPROPERTY(BlueprintReadOnly, Category = "Friends")
	FString LobbyName;

	bool bFromLauncher = false;
};

/** Fired whenever the friends, requests, invites or login state change */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArenaFriendsChanged);

/** Short user-facing result of an action ("Petición enviada a X", "No se encontró al jugador") */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnArenaFriendsMessage, const FString&, Message);

/** Exposes launcher social data in-game and retains the EOS adapter for builds that enable it. */
UCLASS()
class ARENA_API UArenaFriendsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True when the launcher social bridge or Epic friends service is available */
	UFUNCTION(BlueprintPure, Category = "Friends")
	bool IsAvailable() const;

	/** Re-reads friends and requests from the active social provider */
	UFUNCTION(BlueprintCallable, Category = "Friends")
	void RefreshFriends();

	/** Looks the player up by display name and sends a friend request */
	UFUNCTION(BlueprintCallable, Category = "Friends")
	void SendFriendRequestByName(const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category = "Friends")
	void AcceptRequest(const FString& NetId);

	UFUNCTION(BlueprintCallable, Category = "Friends")
	void DeclineRequest(const FString& NetId);

	UFUNCTION(BlueprintCallable, Category = "Friends")
	void RemoveFriend(const FString& NetId);

	/** Invites a friend to the current LAN session, creating one on the current map if necessary. */
	UFUNCTION(BlueprintCallable, Category = "Friends")
	void InviteToLobby(const FString& NetId);

	/** Opens a direct chat with an accepted Arena friend through the launcher */
	void OpenGameChat(const FString& NetId);

	/** Sends a direct chat message through the authenticated launcher */
	void SendGameChatMessage(const FString& NetId, const FString& Body);

	/** Controls unread-message handling while the in-game chat view is active */
	void SetGameChatOpen(bool bOpen);

	/** Accepts a launcher invite and returns its target LAN lobby name, or empty for EOS invites. */
	FString AcceptLobbyInvite(const FString& SenderNetId);

	/** Declines a launcher invite or removes a native EOS invite from the local pending list. */
	bool DeclineLobbyInvite(const FString& SenderNetId);

	/** Text other players see under this player's name ("En el lobby", "En partida") */
	UFUNCTION(BlueprintCallable, Category = "Friends")
	void SetMyPresence(const FString& Status);

	const TArray<FArenaFriendInfo>& GetFriends() const { return Friends; }
	const TArray<FArenaFriendRequestInfo>& GetRequests() const { return Requests; }
	const TArray<FArenaLobbyInviteInfo>& GetLobbyInvites() const { return LobbyInvites; }
	const TArray<FArenaChatMessageInfo>& GetChatMessages() const { return ChatMessages; }
	const FString& GetChatFriendId() const { return ChatFriendId; }
	const FString& GetChatFriendName() const { return ChatFriendName; }

	UPROPERTY(BlueprintAssignable, Category = "Friends")
	FOnArenaFriendsChanged OnFriendsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Friends")
	FOnArenaFriendsMessage OnFriendsMessage;

private:
	IOnlineFriendsPtr GetFriendsInterface() const;
	IOnlinePresencePtr GetPresenceInterface() const;
	IOnlineSessionPtr GetSessionInterface() const;
	IOnlineIdentityPtr GetIdentityInterface() const;

	FUniqueNetIdPtr MakeNetId(const FString& NetId) const;
	void Notify(const FString& Message) const;
	void RebuildLists();

	void HandleLoginStatusChanged(int32 LocalUserNum, ELoginStatus::Type OldStatus, ELoginStatus::Type NewStatus, const FUniqueNetId& UserId);
	void HandleFriendsChange();
	void HandlePresenceReceived(const FUniqueNetId& UserId, const TSharedRef<FOnlineUserPresence>& Presence);
	void HandleReadFriendsComplete(int32 LocalUserNum, bool bWasSuccessful, const FString& ListName, const FString& ErrorStr);
	void HandleSessionInviteReceived(const FUniqueNetId& UserId, const FUniqueNetId& FromId, const FString& AppId, const FOnlineSessionSearchResult& InviteResult);
	void HandleSessionInviteAccepted(const bool bWasSuccessful, const int32 ControllerId, FUniqueNetIdPtr UserId, const FOnlineSessionSearchResult& InviteResult);
	UFUNCTION()
	void HandleInviteHostReady(bool bSuccess);
	void QueueLauncherLobbyInvite(const FString& NetId);
	bool PollLauncherSocial(float DeltaSeconds);
	bool WriteLauncherCommand(const TSharedRef<FJsonObject>& Command) const;

	TArray<FArenaFriendInfo> Friends;
	TArray<FArenaFriendRequestInfo> Requests;
	TArray<FArenaLobbyInviteInfo> LobbyInvites;
	TArray<FArenaChatMessageInfo> ChatMessages;
	FString ChatFriendId;
	FString ChatFriendName;
	FString PendingLobbyInviteNetId;

	/** Invite payloads kept until the player accepts or ignores them, keyed by sender id */
	TMap<FString, FOnlineSessionSearchResult> PendingInvites;

	FString MyPresenceStatus;
	bool bLauncherAuthenticated = false;
	bool bLauncherSnapshotLoaded = false;
	FTSTicker::FDelegateHandle LauncherSocialTicker;
	TMap<FString, FString> LauncherInviteLobbies;
	TMap<FString, FString> LauncherFriendshipIds;
	FString LastLauncherSnapshot;

	FDelegateHandle LoginStatusHandle;
	FDelegateHandle FriendsChangeHandle;
	FDelegateHandle PresenceHandle;
	FDelegateHandle InviteReceivedHandle;
	FDelegateHandle InviteAcceptedHandle;
};

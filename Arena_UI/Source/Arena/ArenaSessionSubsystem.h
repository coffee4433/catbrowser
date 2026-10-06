// Epic Online Services (EOS) & LAN session management subsystem

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "OnlineSessionSettings.h"
#include "ArenaSessionSubsystem.generated.h"

/** One server found during a search */
USTRUCT(BlueprintType)
struct FArenaServerInfo
{
	GENERATED_BODY()

	/** Display name of the server */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString ServerName;

	/** Current number of players on the server */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 CurrentPlayers = 0;

	/** Maximum number of players allowed */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 MaxPlayers = 0;

	/** Ping in milliseconds */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	int32 PingMs = 0;

	/** Map currently being played */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	FString MapName;

	/** True when the server was found through the LAN (Null) subsystem instead of EOS */
	UPROPERTY(BlueprintReadOnly, Category = "Session")
	bool bIsLAN = false;

	/** Internal index into the search results of the matching subsystem (for joining) */
	int32 SearchResultIndex = INDEX_NONE;
};

/** Fired when a session search completes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServersFound, const TArray<FArenaServerInfo>&, Servers);

/** Fired when hosting or joining completes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionReady, bool, bSuccess);

/** Fired when online authentication completes */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionLoginComplete, bool, bSuccess, const FString&, Error);

/**
 * Game instance subsystem providing LAN
 * session management (host, find, join, login).
 *
 * Usage from the lobby:
 *   GetSubsystem<UArenaSessionSubsystem>()->HostSession(...)
 *   GetSubsystem<UArenaSessionSubsystem>()->FindSessions()
 *   GetSubsystem<UArenaSessionSubsystem>()->JoinSession(Index)
 */
UCLASS()
class ARENA_API UArenaSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Authentication ────────────────────────────────────────────────

	/** Attempts login using Epic Online Services (default: accountportal) */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void Login(const FString& InType = TEXT("accountportal"), const FString& InId = TEXT(""), const FString& InToken = TEXT(""));

	/** Whether the local player is currently logged in */
	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsLoggedIn() const;

	/** Current player display name (online identity or PC name) */
	UFUNCTION(BlueprintPure, Category = "Session")
	FString GetPlayerNickname() const;

	/** Whether the active online subsystem is EOS */
	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsUsingEOS() const;

	// ── Host ──────────────────────────────────────────────────────────

	/** Creates an online/LAN session and, on success, starts listening on the map */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void HostSession(const FString& InServerName, const FString& InMapPath, int32 InMaxPlayers = 8);

	// ── Find ──────────────────────────────────────────────────────────

	/** Searches for online/LAN sessions; results arrive via OnServersFound */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void FindSessions();

	/** Cancels an in-progress search */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void CancelSearch();

	// ── Join ──────────────────────────────────────────────────────────

	/** Joins the server at the given index from the last search results */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void JoinSession(int32 ServerIndex);

	/** Finds and joins a LAN session by its advertised name without asking for an IP address. */
	void JoinLANSessionByName(const FString& ServerName);

	/** Joins a server directly by IP[:port] (LAN, VPN such as Radmin/Hamachi, or port-forwarded host). */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void ConnectToAddress(const FString& Address);

	/** Joins the EOS lobby a friend invited us to (leaves the current session first) */
	void JoinInvite(const FOnlineSessionSearchResult& InviteResult);

	// ── Leave ─────────────────────────────────────────────────────────

	/** Destroys the current session and returns to the main menu */
	UFUNCTION(BlueprintCallable, Category = "Session")
	void LeaveSession();

	// ── Events ────────────────────────────────────────────────────────

	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnServersFound OnServersFound;

	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionReady OnHostReady;

	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionReady OnJoinReady;

	UPROPERTY(BlueprintAssignable, Category = "Session")
	FOnSessionLoginComplete OnLoginComplete;

	// ── Queries ───────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsSearching() const { return bIsSearching; }

	UFUNCTION(BlueprintPure, Category = "Session")
	bool IsInSession() const;

	/** Returns the currently announced LAN lobby, independent of the online subsystem's session bookkeeping. */
	FString GetCurrentLobbyName() const;

	/** Appends a diagnostic line to Saved/Logs/Arena.log, including in Shipping builds. */
	static void WriteRuntimeLog(const FString& Message);

private:
	/** bLAN selects OnlineSubsystemNull (UDP beacons, no login); otherwise the default platform service (EOS) */
	IOnlineSessionPtr GetSessionInterface(bool bLAN) const;
	/** Interface that owns the live session (see bActiveSessionIsLAN) */
	IOnlineSessionPtr GetActiveSessionInterface() const { return GetSessionInterface(bActiveSessionIsLAN); }
	IOnlineIdentityPtr GetIdentityInterface() const;

	void OnLoginTimeout();
	void FinishSearch();
	void OnFindSessionsCompleteLAN(bool bWasSuccessful);
	void OnFindSessionsCompleteEOS(bool bWasSuccessful);
	void AppendSearchResults(const TSharedPtr<FOnlineSessionSearch>& Search, bool bLAN);

	void OnLoginCompleteCallback(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void OnCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void OnFindSessionsComplete(bool bWasSuccessful);
	void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void OnDestroyBeforeHostComplete(FName SessionName, bool bWasSuccessful);
	void CreateSessionInternal();

	TSharedPtr<FOnlineSessionSearch> LanSearch;
	TSharedPtr<FOnlineSessionSearch> EosSearch;
	TArray<FArenaServerInfo> CachedServers;
	TSet<FString> SeenServers;
	int32 PendingSearches = 0;

	FString PendingMapPath;
	FString PendingServerName;
	FString PendingLANJoinServerName;
	int32 PendingMaxPlayers = 8;
	int32 PendingJoinIndex = INDEX_NONE;
	FString PendingJoinServerName;
	bool PendingJoinIsLAN = false;
	bool bIsSearching = false;
	bool bActiveSessionIsLAN = false;
	bool bHostWithLAN = false;
	bool bLoginPending = false;
	FTimerHandle LoginTimeoutHandle;

	FDelegateHandle LoginHandle;
	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle FindHandleLAN;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;
};

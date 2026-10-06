// Epic Online Services (EOS) & LAN session management subsystem implementation

#include "ArenaSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "EOSSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Arena.h"
#include "ArenaPlayerController.h"

#include "Online/OnlineSessionNames.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#ifndef SEARCH_LOBBIES
#define SEARCH_LOBBIES FName(TEXT("LOBBYSEARCH"))
#endif

// Custom session setting keys
static const FName SETTING_SERVER_NAME = FName(TEXT("SERVERNAME"));
static const FName SETTING_MAP_NAME = FName(TEXT("MAPNAME"));

namespace
{
	FString MaskEOSIdentifier(const FString& Identifier)
	{
		if (Identifier.IsEmpty())
		{
			return TEXT("<empty>");
		}

		if (Identifier.Len() <= 10)
		{
			return TEXT("<configured>");
		}

		return FString::Printf(TEXT("%s...%s"), *Identifier.Left(6), *Identifier.Right(4));
	}

	void LogEOSRuntimeConfiguration()
	{
		const FEOSSettings Settings = UEOSSettings::GetSettings();
		const FString ScopeList = Settings.AuthScopeFlags.IsEmpty()
			? TEXT("<none>")
			: FString::Join(Settings.AuthScopeFlags, TEXT(", "));

		UE_LOG(LogArena, Warning,
			TEXT("EOS diagnostics: runtime config DefaultArtifactName=%s bUseEAS=%d bUseEOSConnect=%d AuthScopeFlags=[%s] LogFile=Saved/Logs/Arena.log"),
			*Settings.DefaultArtifactName,
			Settings.bUseEAS,
			Settings.bUseEOSConnect,
			*ScopeList);

		FEOSArtifactSettings Artifact;
		if (UEOSSettings::GetSelectedArtifactSettings(Artifact))
		{
			UE_LOG(LogArena, Warning,
				TEXT("EOS diagnostics: selected artifact=%s ClientId=%s ProductId=%s SandboxId=%s DeploymentId=%s (secret and encryption key intentionally omitted)"),
				*Artifact.ArtifactName,
				*MaskEOSIdentifier(Artifact.ClientId),
				*MaskEOSIdentifier(Artifact.ProductId),
				*MaskEOSIdentifier(Artifact.SandboxId),
				*MaskEOSIdentifier(Artifact.DeploymentId));
		}
		else
		{
			UE_LOG(LogArena, Error,
				TEXT("EOS diagnostics: no selected artifact settings were resolved for DefaultArtifactName=%s"),
				*Settings.DefaultArtifactName);
		}
	}

	/**
	 * Tells the launcher which server the player is in (<Saved>/ArenaGameBridge/CurrentLobby.json), so friends can be invited to it.
	 * The root-level copy keeps compatibility with older launcher builds. An empty name removes both markers.
	 */
	void SetCurrentLobby(const FString& ServerName)
	{
		const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		const FString BridgeDir = SavedDir / TEXT("ArenaGameBridge");
		const FString FilePaths[] = {
			SavedDir / TEXT("CurrentLobby.json"),
			BridgeDir / TEXT("CurrentLobby.json")
		};
		if (ServerName.IsEmpty())
		{
			for (const FString& FilePath : FilePaths)
			{
				if (FPaths::FileExists(FilePath) && !IFileManager::Get().Delete(*FilePath, false, true, true))
				{
					UE_LOG(LogArena, Warning, TEXT("Could not remove the current lobby marker at %s"), *FilePath);
				}
			}
			UArenaSessionSubsystem::WriteRuntimeLog(TEXT("LAN lobby marker cleared."));
			return;
		}

		if (!IFileManager::Get().MakeDirectory(*BridgeDir, true))
		{
			UE_LOG(LogArena, Error, TEXT("Could not create the launcher bridge directory at %s"), *BridgeDir);
			return;
		}

		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("serverName"), ServerName);
		Json->SetStringField(TEXT("updatedAt"), FDateTime::UtcNow().ToIso8601());
		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		if (!FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
		{
			UE_LOG(LogArena, Error, TEXT("Could not serialize the current lobby marker"));
			return;
		}

		bool bPublishedAllMarkers = true;
		for (const FString& FilePath : FilePaths)
		{
			const FString TemporaryPath = FilePath + TEXT(".tmp");
			if (!FFileHelper::SaveStringToFile(Output, *TemporaryPath)
				|| !IFileManager::Get().Move(*FilePath, *TemporaryPath, true, true, false, true))
			{
				bPublishedAllMarkers = false;
				IFileManager::Get().Delete(*TemporaryPath);
				UE_LOG(LogArena, Warning, TEXT("Could not publish the current lobby marker at %s"), *FilePath);
				UArenaSessionSubsystem::WriteRuntimeLog(FString::Printf(TEXT("Could not publish LAN lobby marker at %s."), *FilePath));
			}
		}
		if (bPublishedAllMarkers)
		{
			UArenaSessionSubsystem::WriteRuntimeLog(FString::Printf(TEXT("LAN lobby marker published: %s"), *ServerName));
		}
	}

	FString ReadCurrentLobby()
	{
		const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		const FString FilePaths[] = {
			SavedDir / TEXT("ArenaGameBridge") / TEXT("CurrentLobby.json"),
			SavedDir / TEXT("CurrentLobby.json")
		};

		for (const FString& FilePath : FilePaths)
		{
			FString JsonContent;
			if (!FFileHelper::LoadFileToString(JsonContent, *FilePath) || JsonContent.IsEmpty())
			{
				continue;
			}

			TSharedPtr<FJsonObject> Json;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
			FString ServerName;
			if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid()
				&& Json->TryGetStringField(TEXT("serverName"), ServerName))
			{
				ServerName.TrimStartAndEndInline();
				if (!ServerName.IsEmpty())
				{
					return ServerName.Left(80);
				}
			}
		}

		return FString();
	}
}

void UArenaSessionSubsystem::WriteRuntimeLog(const FString& Message)
{
	const FString LogDirectory = FPaths::ProjectSavedDir() / TEXT("Logs");
	const FString LogFilePath = LogDirectory / TEXT("Arena.log");
	if (!IFileManager::Get().MakeDirectory(*LogDirectory, true))
	{
		return;
	}

	const FString Line = FString::Printf(TEXT("[%s] [Arena] %s\r\n"), *FDateTime::UtcNow().ToIso8601(), *Message);
	FFileHelper::SaveStringToFile(Line, *LogFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(), FILEWRITE_Append);
}

void UArenaSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	const FName SubsystemName = OnlineSub ? OnlineSub->GetSubsystemName() : FName(TEXT("None"));
	WriteRuntimeLog(FString::Printf(TEXT("Session subsystem initialized; online subsystem=%s"), *SubsystemName.ToString()));
	UE_LOG(LogArena, Log, TEXT("ArenaSessionSubsystem initialized with OnlineSubsystem: %s"), *SubsystemName.ToString());
	if (SubsystemName == FName(TEXT("EOS")))
	{
		LogEOSRuntimeConfiguration();
	}

	// Testing aid: -ArenaEmoteTest=Index[,StopAfterSeconds] makes this instance dance as soon as it has a character
	FString EmoteTestValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("ArenaEmoteTest="), EmoteTestValue))
	{
		FString IndexText, StopText;
		if (!EmoteTestValue.Split(TEXT(","), &IndexText, &StopText))
		{
			IndexText = EmoteTestValue;
		}
		AArenaPlayerController::RunEmoteTest(FCString::Atoi(*IndexText), FCString::Atof(*StopText));
	}

	// If using EOS, attempt silent auto-login (persistent auth from a previous Epic login)
	if (OnlineSub && SubsystemName == FName(TEXT("EOS")))
	{
		IOnlineIdentityPtr Identity = OnlineSub->GetIdentityInterface();
		if (Identity.IsValid())
		{
			LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
				FOnLoginCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnLoginCompleteCallback));

			// Only a login with a token saved by a previous Epic login: AutoLogin would open the browser at every
			// launch when there is none. The player signs in explicitly (friends panel, host / join) otherwise.
			UE_LOG(LogArena, Log, TEXT("ArenaSessionSubsystem: Trying EOS persistent login..."));
			Identity->Login(0, FOnlineAccountCredentials(TEXT("persistentauth"), FString(), FString()));
		}
	}
}

void UArenaSessionSubsystem::Deinitialize()
{
	SetCurrentLobby(FString());

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginTimeoutHandle);
	}

	for (const bool bLAN : { false, true })
	{
		IOnlineSessionPtr SessionInterface = GetSessionInterface(bLAN);
		if (!SessionInterface.IsValid())
		{
			continue;
		}
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(bLAN ? FindHandleLAN : FindHandle);
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	IOnlineIdentityPtr Identity = GetIdentityInterface();
	if (Identity.IsValid() && LoginHandle.IsValid())
	{
		Identity->ClearOnLoginCompleteDelegate_Handle(0, LoginHandle);
	}

	Super::Deinitialize();
}

// ── Authentication ────────────────────────────────────────────────────────────

void UArenaSessionSubsystem::Login(const FString& InType, const FString& InId, const FString& InToken)
{
	if (!IsUsingEOS())
	{
		const FString Error = TEXT("Epic account login is unavailable in LAN-only mode");
		UE_LOG(LogArena, Warning, TEXT("Login requested while LAN-only networking is active"));
		OnLoginComplete.Broadcast(false, Error);
		return;
	}

	IOnlineIdentityPtr Identity = GetIdentityInterface();
	if (!Identity.IsValid())
	{
		UE_LOG(LogArena, Warning, TEXT("Login: No identity interface available"));
		OnLoginComplete.Broadcast(false, TEXT("No identity interface available"));
		return;
	}

	if (Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		UE_LOG(LogArena, Log, TEXT("Login: User already logged in"));
		OnLoginComplete.Broadcast(true, FString());
		return;
	}

	if (!LoginHandle.IsValid())
	{
		LoginHandle = Identity->AddOnLoginCompleteDelegate_Handle(0,
			FOnLoginCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnLoginCompleteCallback));
	}

	FOnlineAccountCredentials Credentials;
	Credentials.Type = InType.IsEmpty() ? TEXT("accountportal") : InType;
	Credentials.Id = InId;
	Credentials.Token = InToken;

	UE_LOG(LogArena, Log, TEXT("Login: Requesting login with type '%s'"), *Credentials.Type);
	const FEOSSettings EOSSettings = UEOSSettings::GetSettings();
	const FString ScopeList = EOSSettings.AuthScopeFlags.IsEmpty()
		? TEXT("<none>")
		: FString::Join(EOSSettings.AuthScopeFlags, TEXT(", "));
	UE_LOG(LogArena, Warning,
		TEXT("EOS diagnostics: starting login type=%s scopes=[%s]"),
		*Credentials.Type,
		*ScopeList);

	// The Epic account portal waits for the player to authorize in the browser and can stay pending forever.
	// Give up waiting after a while so Host/Join fall back to LAN instead of hanging; if the login still
	// completes later the player simply becomes available for online sessions.
	bLoginPending = true;
	if (UWorld* World = GetWorld())
	{
		const float TimeoutSeconds = Credentials.Type == TEXT("accountportal") ? 120.0f : 20.0f;
		World->GetTimerManager().SetTimer(LoginTimeoutHandle, this, &UArenaSessionSubsystem::OnLoginTimeout, TimeoutSeconds, false);
	}

	if (!Identity->Login(0, Credentials))
	{
		UE_LOG(LogArena, Warning, TEXT("Login: Identity->Login call failed"));
		bLoginPending = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(LoginTimeoutHandle);
		}
		OnLoginComplete.Broadcast(false, TEXT("Login call failed"));
	}
}

void UArenaSessionSubsystem::OnLoginTimeout()
{
	if (!bLoginPending)
	{
		return;
	}

	bLoginPending = false;
	UE_LOG(LogArena, Warning, TEXT("EOS login still pending after timeout, continuing in LAN mode"));
	OnLoginComplete.Broadcast(false, TEXT("Login timed out"));
}

void UArenaSessionSubsystem::OnLoginCompleteCallback(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	if (bWasSuccessful)
	{
		const FString Nickname = GetPlayerNickname();
		UE_LOG(LogArena, Log, TEXT("EOS Login successful! Welcome %s (UserId: %s)"), *Nickname, *UserId.ToDebugString());
	}
	else
	{
		UE_LOG(LogArena, Warning, TEXT("EOS Login failed for user %d: %s"), LocalUserNum, *Error);
		UE_LOG(LogArena, Warning,
			TEXT("EOS diagnostics: inspect the LogEOSSDK entries immediately before this callback for the backend OAuth ErrorCode and ErrorMessage."));
	}

	// A failure after the timeout already reported it; do not report it twice
	if (!bLoginPending && !bWasSuccessful)
	{
		return;
	}

	bLoginPending = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LoginTimeoutHandle);
	}
	OnLoginComplete.Broadcast(bWasSuccessful, Error);
}

bool UArenaSessionSubsystem::IsLoggedIn() const
{
	IOnlineIdentityPtr Identity = GetIdentityInterface();
	if (Identity.IsValid())
	{
		return Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn;
	}
	return false;
}

FString UArenaSessionSubsystem::GetPlayerNickname() const
{
	const FString SavedDir = FPaths::ProjectSavedDir();
	const FString SessionPaths[] = {
		FPaths::Combine(SavedDir, TEXT("Config"), TEXT("LauncherSession.json")),
		FPaths::Combine(SavedDir, TEXT("LauncherSession.json"))
	};
	for (const FString& SessionPath : SessionPaths)
	{
		FString JsonContent;
		if (!FFileHelper::LoadFileToString(JsonContent, *SessionPath) || JsonContent.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> SessionJson;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
		if (FJsonSerializer::Deserialize(Reader, SessionJson) && SessionJson.IsValid())
		{
			FString LauncherName;
			if (SessionJson->TryGetStringField(TEXT("username"), LauncherName))
			{
				LauncherName.TrimStartAndEndInline();
				if (!LauncherName.IsEmpty() && !LauncherName.Equals(TEXT("Jugador"), ESearchCase::IgnoreCase))
				{
					return LauncherName.Left(24);
				}
			}
		}
	}

	IOnlineIdentityPtr Identity = GetIdentityInterface();
	if (Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn)
	{
		FString Nick = Identity->GetPlayerNickname(0);
		if (!Nick.IsEmpty())
		{
			return Nick;
		}
	}

	return FPlatformProcess::ComputerName();
}

bool UArenaSessionSubsystem::IsUsingEOS() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub && OnlineSub->GetSubsystemName() == FName(TEXT("EOS"));
}

// ── Host ──────────────────────────────────────────────────────────────────────

void UArenaSessionSubsystem::HostSession(const FString& InServerName, const FString& InMapPath, int32 InMaxPlayers)
{
	PendingMapPath = InMapPath;
	PendingServerName = InServerName;
	PendingMaxPlayers = InMaxPlayers;

	// Game sessions are LAN-only; no Epic login is needed to host or join.
	bHostWithLAN = true;

	if (!GetSessionInterface(bHostWithLAN).IsValid())
	{
		UE_LOG(LogArena, Error, TEXT("HostSession: No session interface available (LAN=%d)"), bHostWithLAN);
		OnHostReady.Broadcast(false);
		return;
	}

	// Destroy any leftover session of either kind first, then create the new one in the callback
	for (const bool bLAN : { false, true })
	{
		IOnlineSessionPtr Existing = GetSessionInterface(bLAN);
		if (Existing.IsValid() && Existing->GetNamedSession(NAME_GameSession))
		{
			bActiveSessionIsLAN = bLAN;
			DestroyHandle = Existing->AddOnDestroySessionCompleteDelegate_Handle(
				FOnDestroySessionCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnDestroyBeforeHostComplete));
			Existing->DestroySession(NAME_GameSession);
			return;
		}
	}

	CreateSessionInternal();
}

void UArenaSessionSubsystem::OnDestroyBeforeHostComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetActiveSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	// There may still be a leftover session on the other interface, so run the whole check again
	HostSession(PendingServerName, PendingMapPath, PendingMaxPlayers);
}

void UArenaSessionSubsystem::CreateSessionInternal()
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface(bHostWithLAN);
	if (!SessionInterface.IsValid())
	{
		OnHostReady.Broadcast(false);
		return;
	}

	FOnlineSessionSettings SessionSettings;
	if (!bHostWithLAN)
	{
		UE_LOG(LogArena, Log, TEXT("Hosting session using Epic Online Services (EOS)..."));
		SessionSettings.bIsLANMatch = false;
		SessionSettings.bShouldAdvertise = true;
		SessionSettings.bUsesPresence = true;
		SessionSettings.bUseLobbiesIfAvailable = true;
		SessionSettings.bAllowJoinInProgress = true;
		SessionSettings.bAllowJoinViaPresence = true;
		SessionSettings.bAllowInvites = true;
	}
	else
	{
		UE_LOG(LogArena, Log, TEXT("Hosting session in LAN mode (EOS not logged in or disabled)..."));
		SessionSettings.bIsLANMatch = true;
		SessionSettings.bShouldAdvertise = true;
		SessionSettings.bUsesPresence = false;
		SessionSettings.bAllowJoinInProgress = true;
		SessionSettings.bAllowJoinViaPresence = false;
	}
	SessionSettings.NumPublicConnections = PendingMaxPlayers;

	// Custom settings for the server browser
	SessionSettings.Set(SETTING_SERVER_NAME, PendingServerName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SessionSettings.Set(SETTING_MAP_NAME, PendingMapPath, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	bActiveSessionIsLAN = bHostWithLAN;
	CreateHandle = SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnCreateSessionComplete));

	if (!SessionInterface->CreateSession(0, NAME_GameSession, SessionSettings))
	{
		UE_LOG(LogArena, Error, TEXT("HostSession: CreateSession call failed"));
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		OnHostReady.Broadcast(false);
	}
}

void UArenaSessionSubsystem::OnCreateSessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetActiveSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	}

	if (bWasSuccessful)
	{
		UE_LOG(LogArena, Log, TEXT("Session '%s' created successfully (%s)"), *PendingServerName, bActiveSessionIsLAN ? TEXT("LAN") : TEXT("EOS"));

		// Start listening for connections on the current world
		UWorld* World = GetGameInstance()->GetWorld();
		if (World && World->GetNetMode() == NM_Standalone)
		{
			FURL ListenURL;
			ListenURL.Port = 7777;
			if (!World->Listen(ListenURL))
			{
				UE_LOG(LogArena, Error, TEXT("Failed to start listen server"));
				SetCurrentLobby(FString());
				WriteRuntimeLog(TEXT("LAN session was created, but the listen server failed to start."));
				LeaveSession();
				OnHostReady.Broadcast(false);
				return;
			}
			UE_LOG(LogArena, Log, TEXT("Listen server started successfully on port %d"), ListenURL.Port);
		}
		SetCurrentLobby(PendingServerName);
		WriteRuntimeLog(FString::Printf(TEXT("LAN session created and announced: %s"), *PendingServerName));
	}
	else
	{
		WriteRuntimeLog(FString::Printf(TEXT("Session creation failed: %s"), *PendingServerName));
		UE_LOG(LogArena, Error, TEXT("Failed to create session"));
	}

	OnHostReady.Broadcast(bWasSuccessful);
}

// ── Find ──────────────────────────────────────────────────────────────────────

void UArenaSessionSubsystem::FindSessions()
{
	if (bIsSearching)
	{
		return;
	}

	CachedServers.Empty();
	SeenServers.Empty();
	LanSearch.Reset();
	EosSearch.Reset();
	PendingSearches = 0;
	bIsSearching = true;

	// LAN search always runs: it needs no login and also finds hosts that fell back to LAN
	if (IOnlineSessionPtr LanInterface = GetSessionInterface(true))
	{
		UE_LOG(LogArena, Log, TEXT("Searching for sessions on LAN..."));
		LanSearch = MakeShareable(new FOnlineSessionSearch());
		LanSearch->bIsLanQuery = true;
		LanSearch->MaxSearchResults = 20;

		FindHandleLAN = LanInterface->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnFindSessionsCompleteLAN));
		if (LanInterface->FindSessions(0, LanSearch.ToSharedRef()))
		{
			++PendingSearches;
		}
		else
		{
			UE_LOG(LogArena, Warning, TEXT("FindSessions: LAN FindSessions call failed"));
			LanInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindHandleLAN);
			LanSearch.Reset();
		}
	}

	// Online search only when EOS is available and the player is logged in
	if (IsUsingEOS() && IsLoggedIn())
	{
		if (IOnlineSessionPtr EosInterface = GetSessionInterface(false))
		{
			UE_LOG(LogArena, Log, TEXT("Searching for sessions via Epic Online Services..."));
			EosSearch = MakeShareable(new FOnlineSessionSearch());
			EosSearch->bIsLanQuery = false;
			EosSearch->MaxSearchResults = 50;
			EosSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);

			FindHandle = EosInterface->AddOnFindSessionsCompleteDelegate_Handle(
				FOnFindSessionsCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnFindSessionsCompleteEOS));
			if (EosInterface->FindSessions(0, EosSearch.ToSharedRef()))
			{
				++PendingSearches;
			}
			else
			{
				UE_LOG(LogArena, Warning, TEXT("FindSessions: EOS FindSessions call failed"));
				EosInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
				EosSearch.Reset();
			}
		}
	}

	if (PendingSearches == 0)
	{
		UE_LOG(LogArena, Error, TEXT("FindSessions: no search could be started"));
		FinishSearch();
	}
}

void UArenaSessionSubsystem::CancelSearch()
{
	if (!bIsSearching)
	{
		return;
	}

	if (IOnlineSessionPtr LanInterface = GetSessionInterface(true))
	{
		LanInterface->CancelFindSessions();
	}
	if (IOnlineSessionPtr EosInterface = GetSessionInterface(false))
	{
		EosInterface->CancelFindSessions();
	}
	bIsSearching = false;
}

void UArenaSessionSubsystem::OnFindSessionsCompleteLAN(bool bWasSuccessful)
{
	if (IOnlineSessionPtr LanInterface = GetSessionInterface(true))
	{
		LanInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindHandleLAN);
	}
	if (bWasSuccessful)
	{
		AppendSearchResults(LanSearch, true);
	}
	if (--PendingSearches <= 0)
	{
		FinishSearch();
	}
}

void UArenaSessionSubsystem::OnFindSessionsCompleteEOS(bool bWasSuccessful)
{
	if (IOnlineSessionPtr EosInterface = GetSessionInterface(false))
	{
		EosInterface->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	}
	if (bWasSuccessful)
	{
		AppendSearchResults(EosSearch, false);
	}
	if (--PendingSearches <= 0)
	{
		FinishSearch();
	}
}

void UArenaSessionSubsystem::AppendSearchResults(const TSharedPtr<FOnlineSessionSearch>& Search, bool bLAN)
{
	if (!Search.IsValid())
	{
		return;
	}

	for (int32 i = 0; i < Search->SearchResults.Num(); ++i)
	{
		const FOnlineSessionSearchResult& Result = Search->SearchResults[i];
		const FOnlineSessionSettings& Settings = Result.Session.SessionSettings;

		// Deduplicate: either by Session ID or Host User ID
		FString DedupeKey = Result.Session.GetSessionIdStr();
		if (DedupeKey.IsEmpty() || DedupeKey == TEXT("INVALID"))
		{
			DedupeKey = Result.Session.OwningUserName;
		}
		DedupeKey = (bLAN ? TEXT("LAN:") : TEXT("EOS:")) + DedupeKey;
		if (SeenServers.Contains(DedupeKey))
		{
			continue;
		}
		SeenServers.Add(DedupeKey);

		FArenaServerInfo Info;
		Info.bIsLAN = bLAN;
		Info.SearchResultIndex = i;
		Info.PingMs = Result.PingInMs;

		FString TempString;
		if (Settings.Get(SETTING_SERVER_NAME, TempString) && !TempString.IsEmpty())
		{
			Info.ServerName = TempString;
		}
		else if (!Result.Session.OwningUserName.IsEmpty())
		{
			Info.ServerName = Result.Session.OwningUserName;
		}
		else
		{
			Info.ServerName = TEXT("Arena Server");
		}

		if (Settings.Get(SETTING_MAP_NAME, TempString))
		{
			Info.MapName = TempString;
		}

		Info.MaxPlayers = Settings.NumPublicConnections;
		Info.CurrentPlayers = FMath::Max(1, Info.MaxPlayers - Result.Session.NumOpenPublicConnections);

		CachedServers.Add(Info);
	}
}

void UArenaSessionSubsystem::FinishSearch()
{
	bIsSearching = false;
	UE_LOG(LogArena, Log, TEXT("Found %d unique servers"), CachedServers.Num());
	OnServersFound.Broadcast(CachedServers);

	if (!PendingLANJoinServerName.IsEmpty())
	{
		const FString TargetServerName = PendingLANJoinServerName;
		PendingLANJoinServerName.Reset();
		const int32 ServerIndex = CachedServers.IndexOfByPredicate([&TargetServerName](const FArenaServerInfo& Server)
		{
			return Server.bIsLAN && Server.ServerName.Equals(TargetServerName, ESearchCase::IgnoreCase);
		});
		if (ServerIndex != INDEX_NONE)
		{
			UE_LOG(LogArena, Log, TEXT("Joining invited LAN session '%s'"), *TargetServerName);
			JoinSession(ServerIndex);
		}
		else
		{
			UE_LOG(LogArena, Warning, TEXT("Could not find invited LAN session '%s'"), *TargetServerName);
			OnJoinReady.Broadcast(false);
		}
	}
}

// ── Join ──────────────────────────────────────────────────────────────────────

void UArenaSessionSubsystem::JoinSession(int32 ServerIndex)
{
	if (!CachedServers.IsValidIndex(ServerIndex))
	{
		UE_LOG(LogArena, Error, TEXT("JoinSession: Invalid server index %d"), ServerIndex);
		OnJoinReady.Broadcast(false);
		return;
	}

	const FArenaServerInfo& Info = CachedServers[ServerIndex];
	const TSharedPtr<FOnlineSessionSearch>& Search = Info.bIsLAN ? LanSearch : EosSearch;
	IOnlineSessionPtr SessionInterface = GetSessionInterface(Info.bIsLAN);
	if (!SessionInterface.IsValid() || !Search.IsValid() || !Search->SearchResults.IsValidIndex(Info.SearchResultIndex))
	{
		UE_LOG(LogArena, Error, TEXT("JoinSession: search result for index %d is no longer available"), ServerIndex);
		OnJoinReady.Broadcast(false);
		return;
	}

	PendingJoinIndex = Info.SearchResultIndex;
	PendingJoinServerName = Info.ServerName;
	PendingJoinIsLAN = Info.bIsLAN;
	bActiveSessionIsLAN = Info.bIsLAN;

	JoinHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnJoinSessionComplete));

	if (!SessionInterface->JoinSession(0, NAME_GameSession, Search->SearchResults[PendingJoinIndex]))
	{
		UE_LOG(LogArena, Error, TEXT("JoinSession: JoinSession call failed"));
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		PendingJoinIndex = INDEX_NONE;
		PendingJoinServerName.Reset();
		OnJoinReady.Broadcast(false);
	}
}

void UArenaSessionSubsystem::JoinLANSessionByName(const FString& ServerName)
{
	const FString Target = ServerName.TrimStartAndEnd();
	if (Target.IsEmpty())
	{
		UE_LOG(LogArena, Warning, TEXT("JoinLANSessionByName: empty server name"));
		OnJoinReady.Broadcast(false);
		return;
	}
	if (bIsSearching)
	{
		UE_LOG(LogArena, Warning, TEXT("JoinLANSessionByName: a server search is already in progress"));
		OnJoinReady.Broadcast(false);
		return;
	}

	PendingLANJoinServerName = Target;

	// Leave any existing session before searching; an online subsystem can't join a second named session.
	for (const bool bLAN : { false, true })
	{
		IOnlineSessionPtr Existing = GetSessionInterface(bLAN);
		if (!Existing.IsValid() || Existing->GetNamedSession(NAME_GameSession) == nullptr)
		{
			continue;
		}

		bActiveSessionIsLAN = bLAN;
		DestroyHandle = Existing->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateWeakLambda(this,
				[this, Existing, Target](FName /*SessionName*/, bool bWasSuccessful)
				{
					Existing->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
					if (!bWasSuccessful)
					{
						UE_LOG(LogArena, Warning, TEXT("JoinLANSessionByName: could not leave the current session"));
						PendingLANJoinServerName.Reset();
						OnJoinReady.Broadcast(false);
						return;
					}

					SetCurrentLobby(FString());
					JoinLANSessionByName(Target);
				}));
		if (!Existing->DestroySession(NAME_GameSession))
		{
			Existing->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			PendingLANJoinServerName.Reset();
			UE_LOG(LogArena, Warning, TEXT("JoinLANSessionByName: failed to start leaving the current session"));
			OnJoinReady.Broadcast(false);
		}
		return;
	}

	FindSessions();
}

void UArenaSessionSubsystem::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface(PendingJoinIsLAN);
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	}

	const bool bSuccess = (Result == EOnJoinSessionCompleteResult::Success);

	if (bSuccess && SessionInterface.IsValid())
	{
		// Get the server connect string (EOS P2P address or LAN IP)
		FString ConnectString;
		if (!SessionInterface->GetResolvedConnectString(NAME_GameSession, ConnectString) || ConnectString.IsEmpty())
		{
			// Fallback: resolve directly from the search result
			const TSharedPtr<FOnlineSessionSearch>& Search = PendingJoinIsLAN ? LanSearch : EosSearch;
			if (Search.IsValid() && Search->SearchResults.IsValidIndex(PendingJoinIndex))
			{
				SessionInterface->GetResolvedConnectString(Search->SearchResults[PendingJoinIndex], NAME_GamePort, ConnectString);
			}
		}

		if (!ConnectString.IsEmpty())
		{
			WriteRuntimeLog(FString::Printf(TEXT("Joined LAN session '%s'; resolved connection address."), *PendingJoinServerName));
			UE_LOG(LogArena, Log, TEXT("Joining server at connect string: %s"), *ConnectString);

			// Friends can be invited to the server the player just joined, too
			SetCurrentLobby(PendingJoinServerName);

			if (APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController())
			{
				PC->ClientTravel(ConnectString, TRAVEL_Absolute);
			}
		}
		else
		{
			UE_LOG(LogArena, Error, TEXT("JoinSession: Could not resolve connect string"));
			PendingJoinIndex = INDEX_NONE;
			PendingJoinServerName.Reset();
			OnJoinReady.Broadcast(false);
			return;
		}
	}
	else
	{
		UE_LOG(LogArena, Error, TEXT("JoinSession failed with result code %d"), static_cast<int32>(Result));
	}

	PendingJoinIndex = INDEX_NONE;
	PendingJoinServerName.Reset();
	OnJoinReady.Broadcast(bSuccess);
}

void UArenaSessionSubsystem::ConnectToAddress(const FString& Address)
{
	const FString Trimmed = Address.TrimStartAndEnd();
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (Trimmed.IsEmpty() || PC == nullptr)
	{
		OnJoinReady.Broadcast(false);
		return;
	}

	// Default to the game port when none was typed
	const FString Target = Trimmed.Contains(TEXT(":")) ? Trimmed : Trimmed + TEXT(":7777");

	UE_LOG(LogArena, Log, TEXT("ConnectToAddress: %s"), *Target);
	PC->ClientTravel(Target, TRAVEL_Absolute);
	OnJoinReady.Broadcast(true);
}

void UArenaSessionSubsystem::JoinInvite(const FOnlineSessionSearchResult& InviteResult)
{
	IOnlineSessionPtr SessionInterface = GetSessionInterface(false);
	if (!SessionInterface.IsValid())
	{
		OnJoinReady.Broadcast(false);
		return;
	}

	// A named session can only exist once: leave the current one, then join the invite
	if (SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
	{
		SessionInterface->DestroySession(NAME_GameSession,
			FOnDestroySessionCompleteDelegate::CreateWeakLambda(this, [this, InviteResult](FName /*SessionName*/, bool /*bWasSuccessful*/)
			{
				JoinInvite(InviteResult);
			}));
		return;
	}

	PendingJoinIndex = INDEX_NONE;
	PendingJoinIsLAN = false;
	bActiveSessionIsLAN = false;

	JoinHandle = SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnJoinSessionComplete));

	if (!SessionInterface->JoinSession(0, NAME_GameSession, InviteResult))
	{
		UE_LOG(LogArena, Error, TEXT("JoinInvite: JoinSession call failed"));
		SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		OnJoinReady.Broadcast(false);
	}
}

// ── Leave ─────────────────────────────────────────────────────────────────────

void UArenaSessionSubsystem::LeaveSession()
{
	for (const bool bLAN : { false, true })
	{
		IOnlineSessionPtr SessionInterface = GetSessionInterface(bLAN);
		if (SessionInterface.IsValid() && SessionInterface->GetNamedSession(NAME_GameSession))
		{
			bActiveSessionIsLAN = bLAN;
			DestroyHandle = SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
				FOnDestroySessionCompleteDelegate::CreateUObject(this, &UArenaSessionSubsystem::OnDestroySessionComplete));

			SessionInterface->DestroySession(NAME_GameSession);
			return;
		}
	}
}

void UArenaSessionSubsystem::OnDestroySessionComplete(FName SessionName, bool bWasSuccessful)
{
	IOnlineSessionPtr SessionInterface = GetActiveSessionInterface();
	if (SessionInterface.IsValid())
	{
		SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}

	UE_LOG(LogArena, Log, TEXT("Session destroyed: %s"), bWasSuccessful ? TEXT("success") : TEXT("failed"));
	SetCurrentLobby(FString());
}

// ── Queries ───────────────────────────────────────────────────────────────────

bool UArenaSessionSubsystem::IsInSession() const
{
	for (const bool bLAN : { false, true })
	{
		IOnlineSessionPtr SessionInterface = GetSessionInterface(bLAN);
		if (SessionInterface.IsValid() && SessionInterface->GetNamedSession(NAME_GameSession) != nullptr)
		{
			return true;
		}
	}
	return false;
}

FString UArenaSessionSubsystem::GetCurrentLobbyName() const
{
	const FString MarkerName = ReadCurrentLobby();
	if (!MarkerName.IsEmpty())
	{
		return MarkerName;
	}

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World == nullptr || World->GetNetMode() == NM_Standalone)
	{
		return FString();
	}

	const FName ServerNameKey(TEXT("SERVERNAME"));
	for (const bool bLAN : { true, false })
	{
		IOnlineSessionPtr SessionInterface = GetSessionInterface(bLAN);
		const FNamedOnlineSession* Session = SessionInterface.IsValid()
			? SessionInterface->GetNamedSession(NAME_GameSession)
			: nullptr;
		if (Session == nullptr)
		{
			continue;
		}

		FString ServerName;
		if (!Session->SessionSettings.Get(ServerNameKey, ServerName) || ServerName.IsEmpty())
		{
			ServerName = Session->OwningUserName;
		}
		if (!ServerName.IsEmpty())
		{
			SetCurrentLobby(ServerName);
			return ServerName.Left(80);
		}
	}

	return FString();
}

// ── Internals ─────────────────────────────────────────────────────────────────

IOnlineSessionPtr UArenaSessionSubsystem::GetSessionInterface(bool bLAN) const
{
	IOnlineSubsystem* OnlineSub = bLAN ? IOnlineSubsystem::Get(FName(TEXT("NULL"))) : IOnlineSubsystem::Get();
	if (!bLAN && OnlineSub && OnlineSub->GetSubsystemName() == FName(TEXT("NULL")))
	{
		return nullptr;
	}
	if (OnlineSub)
	{
		return OnlineSub->GetSessionInterface();
	}
	return nullptr;
}

IOnlineIdentityPtr UArenaSessionSubsystem::GetIdentityInterface() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		return OnlineSub->GetIdentityInterface();
	}
	return nullptr;
}

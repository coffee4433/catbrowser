// In-game Epic Online Services friends implementation

#include "ArenaFriendsSubsystem.h"
#include "ArenaSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineUserInterface.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Arena.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString FriendsListName()
	{
		return EFriendsLists::ToString(EFriendsLists::Default);
	}
}

void UArenaFriendsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub == nullptr || OnlineSub->GetSubsystemName() != FName(TEXT("EOS")))
	{
		UE_LOG(LogArena, Log, TEXT("ArenaFriendsSubsystem: reading friends from the Arena launcher"));
		PollLauncherSocial(0.0f);
		LauncherSocialTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UArenaFriendsSubsystem::PollLauncherSocial), 1.0f);
		return;
	}

	if (IOnlineIdentityPtr Identity = GetIdentityInterface())
	{
		LoginStatusHandle = Identity->AddOnLoginStatusChangedDelegate_Handle(0,
			FOnLoginStatusChangedDelegate::CreateUObject(this, &UArenaFriendsSubsystem::HandleLoginStatusChanged));
	}

	if (IOnlineFriendsPtr FriendsInterface = GetFriendsInterface())
	{
		FriendsChangeHandle = FriendsInterface->AddOnFriendsChangeDelegate_Handle(0,
			FOnFriendsChangeDelegate::CreateUObject(this, &UArenaFriendsSubsystem::HandleFriendsChange));
	}

	if (IOnlinePresencePtr PresenceInterface = GetPresenceInterface())
	{
		PresenceHandle = PresenceInterface->AddOnPresenceReceivedDelegate_Handle(
			FOnPresenceReceivedDelegate::CreateUObject(this, &UArenaFriendsSubsystem::HandlePresenceReceived));
	}

	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		InviteReceivedHandle = SessionInterface->AddOnSessionInviteReceivedDelegate_Handle(
			FOnSessionInviteReceivedDelegate::CreateUObject(this, &UArenaFriendsSubsystem::HandleSessionInviteReceived));
		InviteAcceptedHandle = SessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &UArenaFriendsSubsystem::HandleSessionInviteAccepted));
	}

	if (IsAvailable())
	{
		RefreshFriends();
	}
	PollLauncherSocial(0.0f);
	LauncherSocialTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UArenaFriendsSubsystem::PollLauncherSocial), 1.0f);
}

void UArenaFriendsSubsystem::Deinitialize()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->OnHostReady.RemoveDynamic(this, &UArenaFriendsSubsystem::HandleInviteHostReady);
		}
	}
	PendingLobbyInviteNetId.Reset();

	if (LauncherSocialTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LauncherSocialTicker);
	}

	if (IOnlineIdentityPtr Identity = GetIdentityInterface())
	{
		Identity->ClearOnLoginStatusChangedDelegate_Handle(0, LoginStatusHandle);
	}
	if (IOnlineFriendsPtr FriendsInterface = GetFriendsInterface())
	{
		FriendsInterface->ClearOnFriendsChangeDelegate_Handle(0, FriendsChangeHandle);
	}
	if (IOnlinePresencePtr PresenceInterface = GetPresenceInterface())
	{
		PresenceInterface->ClearOnPresenceReceivedDelegate_Handle(PresenceHandle);
	}
	if (IOnlineSessionPtr SessionInterface = GetSessionInterface())
	{
		SessionInterface->ClearOnSessionInviteReceivedDelegate_Handle(InviteReceivedHandle);
		SessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteAcceptedHandle);
	}

	Super::Deinitialize();
}

// ── Queries ───────────────────────────────────────────────────────────────────

bool UArenaFriendsSubsystem::IsAvailable() const
{
	if (bLauncherSnapshotLoaded)
	{
		return bLauncherAuthenticated;
	}

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub == nullptr || OnlineSub->GetSubsystemName() != FName(TEXT("EOS")))
	{
		return false;
	}

	IOnlineIdentityPtr Identity = OnlineSub->GetIdentityInterface();
	return Identity.IsValid() && Identity->GetLoginStatus(0) == ELoginStatus::LoggedIn;
}

// ── Actions ───────────────────────────────────────────────────────────────────

void UArenaFriendsSubsystem::RefreshFriends()
{
	if (bLauncherSnapshotLoaded)
	{
		PollLauncherSocial(0.0f);
		return;
	}

	IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
	if (!IsAvailable() || !FriendsInterface.IsValid())
	{
		Friends.Reset();
		Requests.Reset();
		OnFriendsChanged.Broadcast();
		return;
	}

	FriendsInterface->ReadFriendsList(0, FriendsListName(),
		FOnReadFriendsListComplete::CreateUObject(this, &UArenaFriendsSubsystem::HandleReadFriendsComplete));
}

void UArenaFriendsSubsystem::SendFriendRequestByName(const FString& DisplayName)
{
	const FString Name = DisplayName.TrimStartAndEnd();
	if (bLauncherSnapshotLoaded)
	{
		if (!bLauncherAuthenticated)
		{
			Notify(TEXT("Inicia sesión en el launcher de Arena para usar los amigos"));
			return;
		}
		if (Name.Len() < 2 || Name.Len() > 24)
		{
			Notify(TEXT("El nombre debe tener entre 2 y 24 caracteres"));
			return;
		}
		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("add"));
		Command->SetStringField(TEXT("name"), Name);
		Notify(WriteLauncherCommand(Command) ? TEXT("Solicitud enviada al launcher") : TEXT("No se pudo enviar la solicitud"));
		return;
	}

	if (!IsAvailable())
	{
		Notify(TEXT("Inicia sesión en el launcher de Arena para usar los amigos"));
		return;
	}
	if (Name.IsEmpty())
	{
		Notify(TEXT("Escribe el nombre de Epic del jugador"));
		return;
	}

	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	IOnlineUserPtr UserInterface = OnlineSub ? OnlineSub->GetUserInterface() : nullptr;
	FUniqueNetIdPtr LocalId = GetIdentityInterface()->GetUniquePlayerId(0);
	if (!UserInterface.IsValid() || !LocalId.IsValid())
	{
		Notify(TEXT("No se pudo contactar con Epic Games"));
		return;
	}

	UserInterface->QueryUserIdMapping(*LocalId, Name,
		IOnlineUser::FOnQueryUserMappingComplete::CreateWeakLambda(this,
			[this, Name](bool bWasSuccessful, const FUniqueNetId& /*UserId*/, const FString& /*DisplayNameOrEmail*/, const FUniqueNetId& FoundUserId, const FString& /*Error*/)
			{
				if (!bWasSuccessful || !FoundUserId.IsValid())
				{
					Notify(FString::Printf(TEXT("No se encontró a «%s»"), *Name));
					return;
				}

				IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
				if (!FriendsInterface.IsValid())
				{
					return;
				}

				FriendsInterface->SendInvite(0, FoundUserId, FriendsListName(),
					FOnSendInviteComplete::CreateWeakLambda(this,
						[this, Name](int32 /*LocalUserNum*/, bool bInviteSent, const FUniqueNetId& /*FriendId*/, const FString& /*ListName*/, const FString& /*ErrorStr*/)
						{
							Notify(bInviteSent
								? FString::Printf(TEXT("Petición de amistad enviada a %s"), *Name)
								: FString::Printf(TEXT("No se pudo enviar la petición a %s"), *Name));
							RefreshFriends();
						}));
			}));
}

void UArenaFriendsSubsystem::AcceptRequest(const FString& NetId)
{
	if (NetId.StartsWith(TEXT("launcher:")))
	{
		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("respond"));
		Command->SetStringField(TEXT("id"), NetId.RightChop(9));
		Command->SetBoolField(TEXT("accept"), true);
		Notify(WriteLauncherCommand(Command) ? TEXT("Solicitud aceptada") : TEXT("No se pudo aceptar la solicitud"));
		return;
	}

	IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
	FUniqueNetIdPtr Id = MakeNetId(NetId);
	if (!FriendsInterface.IsValid() || !Id.IsValid())
	{
		return;
	}

	FriendsInterface->AcceptInvite(0, *Id, FriendsListName(),
		FOnAcceptInviteComplete::CreateWeakLambda(this,
			[this](int32 /*LocalUserNum*/, bool bAccepted, const FUniqueNetId& /*FriendId*/, const FString& /*ListName*/, const FString& /*ErrorStr*/)
			{
				Notify(bAccepted ? TEXT("¡Petición aceptada!") : TEXT("No se pudo aceptar la petición"));
				RefreshFriends();
			}));
}

void UArenaFriendsSubsystem::DeclineRequest(const FString& NetId)
{
	if (NetId.StartsWith(TEXT("launcher:")))
	{
		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("respond"));
		Command->SetStringField(TEXT("id"), NetId.RightChop(9));
		Command->SetBoolField(TEXT("accept"), false);
		Notify(WriteLauncherCommand(Command) ? TEXT("Solicitud rechazada") : TEXT("No se pudo rechazar la solicitud"));
		return;
	}

	IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
	FUniqueNetIdPtr Id = MakeNetId(NetId);
	if (FriendsInterface.IsValid() && Id.IsValid())
	{
		FriendsInterface->RejectInvite(0, *Id, FriendsListName());
		Notify(TEXT("Petición rechazada"));
		RefreshFriends();
	}
}

void UArenaFriendsSubsystem::RemoveFriend(const FString& NetId)
{
	if (bLauncherSnapshotLoaded)
	{
		const FString* FriendshipId = LauncherFriendshipIds.Find(NetId);
		if (FriendshipId == nullptr)
		{
			return;
		}
		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("remove"));
		Command->SetStringField(TEXT("id"), *FriendshipId);
		Notify(WriteLauncherCommand(Command) ? TEXT("Amigo eliminado") : TEXT("No se pudo eliminar al amigo"));
		return;
	}

	IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
	FUniqueNetIdPtr Id = MakeNetId(NetId);
	if (FriendsInterface.IsValid() && Id.IsValid())
	{
		FriendsInterface->DeleteFriend(0, *Id, FriendsListName());
		RefreshFriends();
	}
}

void UArenaFriendsSubsystem::InviteToLobby(const FString& NetId)
{
	UArenaSessionSubsystem::WriteRuntimeLog(TEXT("Friend invite requested from the in-game friends panel."));
	if (bLauncherSnapshotLoaded)
	{
		if (!bLauncherAuthenticated)
		{
			UArenaSessionSubsystem::WriteRuntimeLog(TEXT("Invite rejected: launcher snapshot is not authenticated."));
			Notify(TEXT("Inicia sesión en el launcher de Arena para invitar amigos"));
			return;
		}
		UGameInstance* GameInstance = GetGameInstance();
		UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
		if (Sessions == nullptr)
		{
			Notify(TEXT("No se pudo iniciar el sistema de partidas LAN"));
			return;
		}

		if (!Sessions->GetCurrentLobbyName().IsEmpty())
		{
			QueueLauncherLobbyInvite(NetId);
			return;
		}

		if (!PendingLobbyInviteNetId.IsEmpty())
		{
			Notify(TEXT("Ya se está creando un lobby LAN para enviar la invitación"));
			return;
		}

		UWorld* World = GameInstance->GetWorld();
		if (World == nullptr)
		{
			Notify(TEXT("No se pudo crear el lobby LAN desde esta pantalla"));
			return;
		}

		PendingLobbyInviteNetId = NetId;
		Sessions->OnHostReady.RemoveDynamic(this, &UArenaFriendsSubsystem::HandleInviteHostReady);
		Sessions->OnHostReady.AddDynamic(this, &UArenaFriendsSubsystem::HandleInviteHostReady);
		const FString ServerName = Sessions->GetPlayerNickname();
		const FString MapName = World->GetMapName();
		UArenaSessionSubsystem::WriteRuntimeLog(FString::Printf(
			TEXT("No active LAN lobby; creating one on map '%s' before inviting friend."), *MapName));
		Notify(TEXT("Creando lobby LAN y preparando la invitación..."));
		Sessions->HostSession(ServerName, MapName, 8);
		return;
	}

	IOnlineSessionPtr SessionInterface = GetSessionInterface();
	FUniqueNetIdPtr Id = MakeNetId(NetId);
	if (!SessionInterface.IsValid() || !Id.IsValid())
	{
		return;
	}

	if (SessionInterface->GetNamedSession(NAME_GameSession) == nullptr)
	{
		Notify(TEXT("Primero crea un servidor (ALOJAR) para invitar amigos"));
		return;
	}

	const bool bSent = SessionInterface->SendSessionInviteToFriend(0, NAME_GameSession, *Id);
	Notify(bSent ? TEXT("¡Invitación al lobby enviada!") : TEXT("No se pudo enviar la invitación"));
}

void UArenaFriendsSubsystem::HandleInviteHostReady(bool bSuccess)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UArenaSessionSubsystem* Sessions = GameInstance->GetSubsystem<UArenaSessionSubsystem>())
		{
			Sessions->OnHostReady.RemoveDynamic(this, &UArenaFriendsSubsystem::HandleInviteHostReady);
		}
	}

	const FString NetId = MoveTemp(PendingLobbyInviteNetId);
	PendingLobbyInviteNetId.Reset();
	if (!bSuccess)
	{
		UArenaSessionSubsystem::WriteRuntimeLog(TEXT("Could not create LAN lobby; friend invitation was not sent."));
		Notify(TEXT("No se pudo crear el lobby LAN; no se envió la invitación"));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	if (Sessions == nullptr || Sessions->GetCurrentLobbyName().IsEmpty())
	{
		UArenaSessionSubsystem::WriteRuntimeLog(TEXT("LAN lobby reported ready but has no name; friend invitation was not sent."));
		Notify(TEXT("El lobby se creó, pero no se pudo obtener su nombre para invitar"));
		return;
	}

	QueueLauncherLobbyInvite(NetId);
}

void UArenaFriendsSubsystem::QueueLauncherLobbyInvite(const FString& NetId)
{
	UGameInstance* GameInstance = GetGameInstance();
	UArenaSessionSubsystem* Sessions = GameInstance ? GameInstance->GetSubsystem<UArenaSessionSubsystem>() : nullptr;
	const FString LobbyName = Sessions ? Sessions->GetCurrentLobbyName() : FString();
	if (LobbyName.IsEmpty())
	{
		UArenaSessionSubsystem::WriteRuntimeLog(TEXT("Invite rejected: no LAN lobby is available."));
		Notify(TEXT("No se pudo obtener el nombre del lobby LAN"));
		return;
	}

	TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
	Command->SetStringField(TEXT("action"), TEXT("invite"));
	Command->SetStringField(TEXT("userId"), NetId);
	const bool bCommandWritten = WriteLauncherCommand(Command);
	UArenaSessionSubsystem::WriteRuntimeLog(bCommandWritten
		? FString::Printf(TEXT("Invite command queued for LAN lobby '%s'."), *LobbyName)
		: TEXT("Invite failed: could not queue a command for the launcher."));
	Notify(bCommandWritten ? TEXT("Enviando invitación desde el launcher") : TEXT("No se pudo enviar la invitación"));
}

void UArenaFriendsSubsystem::OpenGameChat(const FString& NetId)
{
	if (!bLauncherSnapshotLoaded || !bLauncherAuthenticated)
	{
		Notify(TEXT("Inicia sesión en el launcher de Arena para usar el chat"));
		return;
	}
	if (!Friends.ContainsByPredicate([&NetId](const FArenaFriendInfo& Friend) { return Friend.NetId == NetId; }))
	{
		Notify(TEXT("Solo puedes chatear con amigos de Arena"));
		return;
	}
	ChatFriendId = NetId;
	const FArenaFriendInfo* Friend = Friends.FindByPredicate([&NetId](const FArenaFriendInfo& Entry) { return Entry.NetId == NetId; });
	ChatFriendName = Friend ? Friend->Name : FString();
	ChatMessages.Reset();

	TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
	Command->SetStringField(TEXT("action"), TEXT("gameOpenChat"));
	Command->SetStringField(TEXT("userId"), NetId);
	if (!WriteLauncherCommand(Command))
	{
		Notify(TEXT("No se pudo abrir el chat"));
	}
}

void UArenaFriendsSubsystem::SendGameChatMessage(const FString& NetId, const FString& Body)
{
	const FString Message = Body.TrimStartAndEnd();
	if (!bLauncherSnapshotLoaded || !bLauncherAuthenticated)
	{
		Notify(TEXT("Inicia sesión en el launcher de Arena para usar el chat"));
		return;
	}
	if (Message.IsEmpty())
	{
		return;
	}
	if (ChatFriendId != NetId)
	{
		Notify(TEXT("Selecciona una conversación antes de enviar un mensaje"));
		return;
	}

	TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
	Command->SetStringField(TEXT("action"), TEXT("gameSendChat"));
	Command->SetStringField(TEXT("userId"), NetId);
	Command->SetStringField(TEXT("body"), Message.Left(500));
	if (!WriteLauncherCommand(Command))
	{
		Notify(TEXT("No se pudo enviar el mensaje al launcher"));
	}
}

void UArenaFriendsSubsystem::SetGameChatOpen(bool bOpen)
{
	if (!bLauncherSnapshotLoaded || !bLauncherAuthenticated)
	{
		return;
	}

	TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
	Command->SetStringField(TEXT("action"), bOpen ? TEXT("gameOpenChat") : TEXT("gameCloseChat"));
	if (bOpen && !ChatFriendId.IsEmpty())
	{
		Command->SetStringField(TEXT("userId"), ChatFriendId);
	}
	if (!WriteLauncherCommand(Command))
	{
		UE_LOG(LogArena, Warning, TEXT("Could not update launcher game-chat state"));
	}
}

FString UArenaFriendsSubsystem::AcceptLobbyInvite(const FString& SenderNetId)
{
	if (bLauncherSnapshotLoaded)
	{
		const FString* LobbyName = LauncherInviteLobbies.Find(SenderNetId);
		if (!bLauncherAuthenticated || LobbyName == nullptr || LobbyName->IsEmpty())
		{
			Notify(TEXT("La invitación ya no está disponible"));
			return FString();
		}

		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("acceptInvite"));
		Command->SetStringField(TEXT("id"), SenderNetId);
		if (!WriteLauncherCommand(Command))
		{
			Notify(TEXT("No se pudo aceptar la invitación"));
			return FString();
		}

		const FString Result = *LobbyName;
		LauncherInviteLobbies.Remove(SenderNetId);
		LobbyInvites.RemoveAll([&SenderNetId](const FArenaLobbyInviteInfo& Entry) { return Entry.SenderNetId == SenderNetId; });
		OnFriendsChanged.Broadcast();
		return Result;
	}

	const FOnlineSessionSearchResult* Invite = PendingInvites.Find(SenderNetId);
	if (Invite == nullptr)
	{
		return FString();
	}

	const FOnlineSessionSearchResult InviteCopy = *Invite;
	PendingInvites.Remove(SenderNetId);
	LobbyInvites.RemoveAll([&SenderNetId](const FArenaLobbyInviteInfo& Entry) { return Entry.SenderNetId == SenderNetId; });
	OnFriendsChanged.Broadcast();

	if (UArenaSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UArenaSessionSubsystem>())
	{
		Sessions->JoinInvite(InviteCopy);
	}
	return FString();
}

bool UArenaFriendsSubsystem::DeclineLobbyInvite(const FString& SenderNetId)
{
	if (bLauncherSnapshotLoaded)
	{
		if (!bLauncherAuthenticated || !LauncherInviteLobbies.Contains(SenderNetId))
		{
			Notify(TEXT("La invitación ya no está disponible"));
			return false;
		}

		TSharedRef<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("action"), TEXT("declineInvite"));
		Command->SetStringField(TEXT("id"), SenderNetId);
		if (!WriteLauncherCommand(Command))
		{
			Notify(TEXT("No se pudo rechazar la invitación"));
			return false;
		}

		LauncherInviteLobbies.Remove(SenderNetId);
	}
	else
	{
		if (PendingInvites.Remove(SenderNetId) == 0)
		{
			return false;
		}
	}

	LobbyInvites.RemoveAll([&SenderNetId](const FArenaLobbyInviteInfo& Entry) { return Entry.SenderNetId == SenderNetId; });
	OnFriendsChanged.Broadcast();
	return true;
}

bool UArenaFriendsSubsystem::PollLauncherSocial(float /*DeltaSeconds*/)
{
	const FString SnapshotPath = FPaths::ProjectSavedDir() / TEXT("ArenaGameBridge") / TEXT("LauncherSocial.json");
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *SnapshotPath) || JsonContent.IsEmpty())
	{
		if (bLauncherSnapshotLoaded)
		{
			bLauncherSnapshotLoaded = false;
			bLauncherAuthenticated = false;
			LastLauncherSnapshot.Reset();
			LauncherFriendshipIds.Reset();
			LauncherInviteLobbies.Reset();
			Friends.Reset();
			Requests.Reset();
			LobbyInvites.Reset();
			ChatMessages.Reset();
			ChatFriendId.Reset();
			ChatFriendName.Reset();
			OnFriendsChanged.Broadcast();
		}
		return true;
	}
	if (JsonContent == LastLauncherSnapshot)
	{
		return true;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogArena, Warning, TEXT("Arena launcher social snapshot is invalid JSON"));
		return true;
	}

	Root->TryGetBoolField(TEXT("authenticated"), bLauncherAuthenticated);
	bLauncherSnapshotLoaded = true;
	LastLauncherSnapshot = JsonContent;
	Friends.Reset();
	Requests.Reset();
	LobbyInvites.Reset();
	LauncherFriendshipIds.Reset();
	LauncherInviteLobbies.Reset();
	ChatMessages.Reset();
	ChatFriendId.Reset();
	ChatFriendName.Reset();

	const TArray<TSharedPtr<FJsonValue>>* FriendRows = nullptr;
	if (Root->TryGetArrayField(TEXT("friends"), FriendRows) && FriendRows != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *FriendRows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			FArenaFriendInfo Friend;
			Row->TryGetStringField(TEXT("id"), Friend.NetId);
			Row->TryGetStringField(TEXT("name"), Friend.Name);
			Row->TryGetBoolField(TEXT("online"), Friend.bOnline);
			Row->TryGetStringField(TEXT("status"), Friend.Status);
			Row->TryGetStringField(TEXT("lobby"), Friend.LobbyName);
			Row->TryGetStringField(TEXT("avatarUrl"), Friend.AvatarUrl);
			if (Friend.Name.IsEmpty() || Friend.NetId.IsEmpty())
			{
				continue;
			}
			if (Friend.bOnline && !Friend.LobbyName.IsEmpty())
			{
				Friend.Status = FString::Printf(TEXT("En lobby: %s"), *Friend.LobbyName);
			}
			else if (!Friend.bOnline || Friend.Status == TEXT("offline"))
			{
				Friend.Status = TEXT("Desconectado");
			}
			else if (Friend.Status == TEXT("game"))
			{
				Friend.Status = TEXT("En el juego");
			}
			else
			{
				Friend.Status = TEXT("En el launcher");
			}

			double FriendshipId = 0.0;
			if (Row->TryGetNumberField(TEXT("friendshipId"), FriendshipId))
			{
				LauncherFriendshipIds.Add(Friend.NetId, FString::Printf(TEXT("%.0f"), FriendshipId));
			}
			Friends.Add(MoveTemp(Friend));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* IncomingRows = nullptr;
	if (Root->TryGetArrayField(TEXT("incoming"), IncomingRows) && IncomingRows != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *IncomingRows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			FArenaFriendRequestInfo Request;
			Row->TryGetStringField(TEXT("name"), Request.Name);
			double FriendshipId = 0.0;
			if (!Request.Name.IsEmpty() && Row->TryGetNumberField(TEXT("friendshipId"), FriendshipId))
			{
				Request.NetId = FString::Printf(TEXT("launcher:%.0f"), FriendshipId);
				Requests.Add(MoveTemp(Request));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* InviteRows = nullptr;
	if (Root->TryGetArrayField(TEXT("invites"), InviteRows) && InviteRows != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *InviteRows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			FArenaLobbyInviteInfo Invite;
			Row->TryGetStringField(TEXT("name"), Invite.SenderName);
			Row->TryGetStringField(TEXT("lobby"), Invite.LobbyName);
			double InviteId = 0.0;
			if (Invite.SenderName.IsEmpty() || Invite.LobbyName.IsEmpty() || !Row->TryGetNumberField(TEXT("id"), InviteId))
			{
				continue;
			}
			Invite.SenderNetId = FString::Printf(TEXT("%.0f"), InviteId);
			Invite.bFromLauncher = true;
			LauncherInviteLobbies.Add(Invite.SenderNetId, Invite.LobbyName);
			LobbyInvites.Add(MoveTemp(Invite));
		}
	}

	const TSharedPtr<FJsonObject>* ChatObject = nullptr;
	if (Root->TryGetObjectField(TEXT("chat"), ChatObject) && ChatObject != nullptr && ChatObject->IsValid())
	{
		(*ChatObject)->TryGetStringField(TEXT("withId"), ChatFriendId);
		(*ChatObject)->TryGetStringField(TEXT("name"), ChatFriendName);

		const TArray<TSharedPtr<FJsonValue>>* MessageRows = nullptr;
		if ((*ChatObject)->TryGetArrayField(TEXT("messages"), MessageRows) && MessageRows != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& Value : *MessageRows)
			{
				const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Row.IsValid())
				{
					continue;
				}

				FArenaChatMessageInfo Message;
				Row->TryGetStringField(TEXT("body"), Message.Body);
				Row->TryGetBoolField(TEXT("mine"), Message.bMine);
				Row->TryGetStringField(TEXT("at"), Message.Timestamp);
				if (!Message.Body.IsEmpty())
				{
					ChatMessages.Add(MoveTemp(Message));
				}
			}
		}
	}

	Friends.Sort([](const FArenaFriendInfo& A, const FArenaFriendInfo& B)
	{
		return A.bOnline != B.bOnline ? A.bOnline : A.Name < B.Name;
	});
	OnFriendsChanged.Broadcast();
	return true;
}

bool UArenaFriendsSubsystem::WriteLauncherCommand(const TSharedRef<FJsonObject>& Command) const
{
	const FString BridgeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ArenaGameBridge"));
	if (!IFileManager::Get().MakeDirectory(*BridgeDir, true))
	{
		UE_LOG(LogArena, Error, TEXT("Could not create the Arena launcher social bridge directory"));
		return false;
	}

	Command->SetStringField(TEXT("commandId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Command, Writer))
	{
		UE_LOG(LogArena, Error, TEXT("Could not serialize an Arena launcher social command"));
		return false;
	}

	const FString CommandPath = BridgeDir / FString::Printf(TEXT("command-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
	const FString TemporaryPath = CommandPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(Json, *TemporaryPath))
	{
		UE_LOG(LogArena, Error, TEXT("Could not write an Arena launcher social command"));
		return false;
	}
	if (!IFileManager::Get().Move(*CommandPath, *TemporaryPath, true, true, false, true))
	{
		IFileManager::Get().Delete(*TemporaryPath);
		UE_LOG(LogArena, Error, TEXT("Could not publish an Arena launcher social command"));
		return false;
	}
	return true;
}

void UArenaFriendsSubsystem::SetMyPresence(const FString& Status)
{
	MyPresenceStatus = Status;

	IOnlinePresencePtr PresenceInterface = GetPresenceInterface();
	FUniqueNetIdPtr LocalId = IsAvailable() ? GetIdentityInterface()->GetUniquePlayerId(0) : nullptr;
	if (!PresenceInterface.IsValid() || !LocalId.IsValid())
	{
		return;
	}

	FOnlineUserPresenceStatus PresenceStatus;
	PresenceStatus.State = EOnlinePresenceState::Online;
	PresenceStatus.StatusStr = Status;
	PresenceInterface->SetPresence(*LocalId, PresenceStatus);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void UArenaFriendsSubsystem::HandleLoginStatusChanged(int32 /*LocalUserNum*/, ELoginStatus::Type /*OldStatus*/, ELoginStatus::Type NewStatus, const FUniqueNetId& /*UserId*/)
{
	if (NewStatus == ELoginStatus::LoggedIn)
	{
		RefreshFriends();
		if (!MyPresenceStatus.IsEmpty())
		{
			SetMyPresence(MyPresenceStatus);
		}
	}
	else
	{
		Friends.Reset();
		Requests.Reset();
		LobbyInvites.Reset();
		PendingInvites.Reset();
		OnFriendsChanged.Broadcast();
	}
}

void UArenaFriendsSubsystem::HandleFriendsChange()
{
	RebuildLists();
}

void UArenaFriendsSubsystem::HandlePresenceReceived(const FUniqueNetId& /*UserId*/, const TSharedRef<FOnlineUserPresence>& /*Presence*/)
{
	RebuildLists();
}

void UArenaFriendsSubsystem::HandleReadFriendsComplete(int32 /*LocalUserNum*/, bool bWasSuccessful, const FString& /*ListName*/, const FString& ErrorStr)
{
	if (!bWasSuccessful)
	{
		UE_LOG(LogArena, Warning, TEXT("Reading the friends list failed: %s"), *ErrorStr);
	}
	RebuildLists();
}

void UArenaFriendsSubsystem::HandleSessionInviteReceived(const FUniqueNetId& /*UserId*/, const FUniqueNetId& FromId, const FString& /*AppId*/, const FOnlineSessionSearchResult& InviteResult)
{
	const FString SenderId = FromId.ToString();
	PendingInvites.Add(SenderId, InviteResult);

	FString SenderName = TEXT("Un amigo");
	for (const FArenaFriendInfo& Friend : Friends)
	{
		if (Friend.NetId == SenderId)
		{
			SenderName = Friend.Name;
			break;
		}
	}

	LobbyInvites.RemoveAll([&SenderId](const FArenaLobbyInviteInfo& Entry) { return Entry.SenderNetId == SenderId; });
	FArenaLobbyInviteInfo Invite;
	Invite.SenderName = SenderName;
	Invite.SenderNetId = SenderId;
	LobbyInvites.Add(Invite);

	OnFriendsChanged.Broadcast();
}

void UArenaFriendsSubsystem::HandleSessionInviteAccepted(const bool bWasSuccessful, const int32 /*ControllerId*/, FUniqueNetIdPtr /*UserId*/, const FOnlineSessionSearchResult& InviteResult)
{
	// Accepted from outside the game (Epic overlay / launcher): join straight away
	if (!bWasSuccessful)
	{
		return;
	}

	if (UArenaSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UArenaSessionSubsystem>())
	{
		Sessions->JoinInvite(InviteResult);
	}
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void UArenaFriendsSubsystem::RebuildLists()
{
	Friends.Reset();
	Requests.Reset();

	IOnlineFriendsPtr FriendsInterface = GetFriendsInterface();
	TArray<TSharedRef<FOnlineFriend>> EpicFriends;
	if (FriendsInterface.IsValid() && FriendsInterface->GetFriendsList(0, FriendsListName(), EpicFriends))
	{
		for (const TSharedRef<FOnlineFriend>& EpicFriend : EpicFriends)
		{
			const FString Name = EpicFriend->GetDisplayName();
			const FString NetId = EpicFriend->GetUserId()->ToString();

			switch (EpicFriend->GetInviteStatus())
			{
			case EInviteStatus::Accepted:
			{
				const FOnlineUserPresence& Presence = EpicFriend->GetPresence();
				FArenaFriendInfo Info;
				Info.Name = Name;
				Info.NetId = NetId;
				Info.bOnline = Presence.bIsOnline;
				Info.Status = !Presence.bIsOnline ? TEXT("Desconectado")
					: (Presence.Status.StatusStr.IsEmpty() ? TEXT("En línea") : Presence.Status.StatusStr);
				Friends.Add(Info);
				break;
			}
			case EInviteStatus::PendingInbound:
			{
				FArenaFriendRequestInfo Request;
				Request.Name = Name;
				Request.NetId = NetId;
				Requests.Add(Request);
				break;
			}
			default:
				break;
			}
		}
	}

	// Online friends first, then alphabetical
	Friends.Sort([](const FArenaFriendInfo& A, const FArenaFriendInfo& B)
	{
		return A.bOnline != B.bOnline ? A.bOnline : A.Name < B.Name;
	});

	OnFriendsChanged.Broadcast();
}

IOnlineFriendsPtr UArenaFriendsSubsystem::GetFriendsInterface() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub ? OnlineSub->GetFriendsInterface() : nullptr;
}

IOnlinePresencePtr UArenaFriendsSubsystem::GetPresenceInterface() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub ? OnlineSub->GetPresenceInterface() : nullptr;
}

IOnlineSessionPtr UArenaFriendsSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub ? OnlineSub->GetSessionInterface() : nullptr;
}

IOnlineIdentityPtr UArenaFriendsSubsystem::GetIdentityInterface() const
{
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	return OnlineSub ? OnlineSub->GetIdentityInterface() : nullptr;
}

FUniqueNetIdPtr UArenaFriendsSubsystem::MakeNetId(const FString& NetId) const
{
	IOnlineIdentityPtr Identity = GetIdentityInterface();
	return Identity.IsValid() ? Identity->CreateUniquePlayerId(NetId) : nullptr;
}

void UArenaFriendsSubsystem::Notify(const FString& Message) const
{
	OnFriendsMessage.Broadcast(Message);
}

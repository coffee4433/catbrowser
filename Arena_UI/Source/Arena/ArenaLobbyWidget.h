// Fortnite style lobby screen, built entirely from UMG widgets in code (fonts/icons ported with FortnitePorting)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "Components/EditableTextBox.h"
#include "ArenaSessionSubsystem.h"
#include "ArenaLobbyWidget.generated.h"

class UBorder;
class UBackgroundBlur;
class UArenaEmoteWheel;
class UButton;
class UImage;
class UCanvasPanel;
class UFontFace;
class UEditableTextBox;
class UTextBlock;
class UTexture2D;
class UUniformGridPanel;
class UVerticalBox;
class UWidget;
class UFortnitePortingStyleData;
class UWrapBox;
class APawn;
class UArenaLobbyWidget;

/** One outfit shown in the locker */
USTRUCT(BlueprintType)
struct ARENA_API FArenaSkinEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locker")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locker")
	TSoftClassPtr<APawn> PawnClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locker")
	TObjectPtr<UTexture2D> Icon;

	/** Pickaxe / glider data asset (null for outfits) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locker")
	TObjectPtr<UObject> Item;

	/** Styles of the outfit (channels of options). When there is a choice, the locker offers EDIT on this outfit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locker")
	TObjectPtr<UFortnitePortingStyleData> StyleData;
};

/** Locker categories, in the order of the category tabs */
namespace ArenaLockerCategory
{
	constexpr int32 Outfit = 0;
	constexpr int32 Backpack = 1;
	constexpr int32 Pickaxe = 2;
	constexpr int32 Glider = 3;
	constexpr int32 Count = 4;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FArenaLobbyPlayEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FArenaLobbyHostEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FArenaLobbyJoinEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArenaSkinSelectedEvent, int32, SkinIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArenaLobbyPageEvent, bool, bLocker);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FArenaLockerItemEvent, int32, LockerCategory, int32, ItemIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FArenaLobbyJoinServerEvent, int32, ServerIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FArenaSkinStyleEvent, int32, SkinIndex, int32, ChannelIndex, int32, OptionIndex);

/** Forwards a locker tile click with its index (dynamic button delegates carry no payload) */
UCLASS()
class ARENA_API UArenaSkinTileHandler : public UObject
{
	GENERATED_BODY()

public:
	int32 Index = INDEX_NONE;
	/** True for the tiles of the map selector */
	bool bMap = false;
	TWeakObjectPtr<UArenaLobbyWidget> Owner;

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();
};

USTRUCT(BlueprintType)
struct ARENA_API FArenaFriendEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FText Status;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	bool bOnline = true;

	/** Epic account id used to invite / remove this friend */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString NetId;

	/** LAN session advertised by this friend, if they are currently in a lobby */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString LobbyName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString AvatarUrl;
};

USTRUCT(BlueprintType)
struct ARENA_API FArenaFriendRequestEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FText RequesterName;

	/** Epic account id used to accept / decline the request */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString NetId;
};

USTRUCT(BlueprintType)
struct ARENA_API FArenaLobbyInviteEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FText SenderName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString SessionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Social")
	FString LobbyName;

	bool bFromLauncher = false;
};

/** Forwards social and friends button clicks with action type and index */
UCLASS()
class ARENA_API UArenaFriendActionHandler : public UObject
{
	GENERATED_BODY()

public:
	int32 Action = 0; // 0 = AcceptRequest, 1 = DeclineRequest, 2 = InviteFriend, 3 = AcceptLobbyInvite, 4 = JoinFriendLobby, 5 = SelectChatFriend
	int32 Index = INDEX_NONE;
	TWeakObjectPtr<UArenaLobbyWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * Lobby overlay: top navigation, player name plate, party invite slots, mode card and the big yellow PLAY button,
 * plus the LOCKER page (outfit grid) reached from the TAQUILLA tab.
 * The whole tree is created in code the first time the widget is built, so a Widget Blueprint child with its own
 * designer layout (any root widget) replaces it completely.
 */
UCLASS(Blueprintable)
class ARENA_API UArenaLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UArenaLobbyWidget(const FObjectInitializer& ObjectInitializer);

	/** Fired when PLAY (or Enter) is pressed */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLobbyPlayEvent OnPlayPressed;

	/** Fired when HOST is pressed (create a LAN session) */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLobbyHostEvent OnHostPressed;

	/** Fired when JOIN is pressed (open server browser) */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLobbyJoinEvent OnJoinPressed;

	/** Fired when a server is selected in the browser */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLobbyJoinServerEvent OnJoinServerSelected;

	/** Fired when an outfit tile is clicked in the locker */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaSkinSelectedEvent OnSkinSelected;

	/** Fired when a style is picked in the styles view of an outfit */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaSkinStyleEvent OnStyleSelected;

	/** Fired when a pickaxe or glider tile is clicked (category from ArenaLockerCategory) */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLockerItemEvent OnLockerItemSelected;

	/** Fired when switching between the lobby (false) and the locker (true) */
	UPROPERTY(BlueprintAssignable, Category = "Lobby")
	FArenaLobbyPageEvent OnPageChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	FText PlayerName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	FText ModeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	FText ModeDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	int32 Currency = 1000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	int32 PlayerLevel = 1;

	/** Tiles per row in the locker grid */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby|Locker")
	int32 LockerColumns = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UFontFace> HeadingFontFace;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UFontFace> BodyFontFace;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UFontFace> BoldFontFace;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UTexture2D> CurrencyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UTexture2D> GradientTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UTexture2D> VignetteTexture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lobby|Style")
	TObjectPtr<UTexture2D> GlowTexture;

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void RequestPlay();

	/** Triggers the host flow */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void RequestHost();

	/** Opens the server browser panel */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void RequestJoin();

	/** Populates the server browser with search results */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetServerList(const TArray<FArenaServerInfo>& Servers);

	/** Hides the server browser panel */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void HideServerBrowser();

	/** Shows a status message in the server browser (e.g. 'Searching...', 'Connecting...') */
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void SetServerBrowserStatus(const FText& Status);

	/** Outfits listed in the locker and the one currently worn */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void SetSkins(const TArray<FArenaSkinEntry>& InSkins, int32 InSelected);

	/** Marks an outfit as worn (updates the highlight and the info text, does not fire OnSkinSelected) */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void SetSelectedSkin(int32 Index);

	/** Marks the option picked in each style channel of the worn outfit (highlight in the styles view) */
	void SetStyleSelection(const TArray<int32>& Selection);

	/** Cursor over / off an outfit tile: an outfit with several styles shows EDIT while it is hovered */
	void HoverSkinTile(int32 Index);
	void UnhoverSkinTile(int32 Index);

	/** Pickaxes (ArenaLockerCategory::Pickaxe) or gliders (ArenaLockerCategory::Glider) and the one in use */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void SetLockerItems(int32 LockerCategory, const TArray<FArenaSkinEntry>& Items, int32 InSelected);

	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void SetLockerSelection(int32 LockerCategory, int32 Index);

	/** Shows the grid of a locker category */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void SelectCategory(int32 LockerCategory);

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void ShowLobby();

	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void ShowLocker();

	UFUNCTION(BlueprintPure, Category = "Lobby")
	bool IsLockerOpen() const { return bLockerOpen; }

	/** Locker zoom picked with the mouse wheel: 0 = full body, 1 = close-up of the upper body */
	float GetLockerZoom() const { return bLockerOpen ? LockerZoom : 0.0f; }

	/** Character turn (degrees) dragged with the left mouse button in the locker */
	float GetLockerYaw() const { return bLockerOpen ? LockerYaw : 0.0f; }

	/** Called by the tile handlers */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void ClickSkin(int32 Index);

	/** A map tile of the selector was clicked */
	void ClickMap(int32 Index);

	/** Package name of the map picked in the selector (/Game/...), empty when none is known */
	FString GetSelectedMapPath() const { return MapPaths.IsValidIndex(SelectedMap) ? MapPaths[SelectedMap] : FString(); }

	bool IsMapPageOpen() const { return bMapPageOpen; }

	/** The emote selector (B held) */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Emotes")
	void OpenEmoteWheel();

	UFUNCTION(BlueprintCallable, Category = "Lobby|Emotes")
	void CloseEmoteWheel();

	/** B pressed / released, from the input pre-processor (works whatever widget has the keyboard focus) */
	void HandleEmoteKey(bool bDown);
	void ShowMapPage();
	void HideMapPage();

	/** Configures this widget to show only the social panel entry point during a match */
	void ConfigureAsMatchSocialOverlay() { bMatchSocialOverlay = true; }
	void ToggleSidePanel();
	bool IsSidePanelOpen() const { return bSidePanelOpen; }

	/** Shows a temporary toast notification at the top of the screen */
	void ShowToast(const FText& Message);

	/** Social and Friends system */
	UFUNCTION(BlueprintCallable, Category = "Lobby|Social")
	void AddFriend(const FText& InName, const FText& InStatus, bool bOnline = true);

	UFUNCTION(BlueprintCallable, Category = "Lobby|Social")
	void AddFriendRequest(const FText& RequesterName);

	UFUNCTION(BlueprintCallable, Category = "Lobby|Social")
	void AddLobbyInvite(const FText& SenderName, const FString& InSessionId = TEXT(""));

	void AcceptFriendRequest(int32 Index);
	void DeclineFriendRequest(int32 Index);
	void InviteFriendToLobby(int32 Index);
	void JoinFriendLobby(int32 Index);
	void SelectChatFriend(int32 Index);
	void SendChatMessage();
	void AcceptLobbyInvite(int32 Index);
	void SetSidePanelTab(int32 TabIndex);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	float LockerZoom = 0.0f;
	float LockerYaw = 0.0f;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> TopBarWidgets;
	bool bRotatingCharacter = false;

	void BuildLayout();
	void BuildMatchSocialOverlay();
	void BuildSidePanel(UCanvasPanel* Root);
	void UpdateAvatars();
	void SetSidePanelOpen(bool bOpen);
	UImage* MakeAvatar(float Size);

	UFUNCTION() void HandleAvatarClicked();
	UFUNCTION() void HandleButtonHovered();
	UFUNCTION() void HandleSidePanelClose();
	UFUNCTION() void HandleSidePanelSettings();
	UFUNCTION() void HandleQuitGame();

	UFUNCTION() void HandleSidePanelSocial();
	UFUNCTION() void HandleSidePanelChat();
	UFUNCTION() void HandleSidePanelAmigos();
	UFUNCTION() void HandleAddFriendPrompt();
	UFUNCTION() void HandleFriendsChanged();
	UFUNCTION() void HandleFriendsMessage(const FString& Message);
	UFUNCTION() void HandleFriendNameChanged(const FText& Text);
	UFUNCTION() void HandleChatSendClicked();
	UFUNCTION() void HandleChatInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleEpicLoginClicked();
	UFUNCTION() void HandleEpicLoginComplete(bool bWasSuccessful, const FString& Error);
	UFUNCTION() void HandleCopyEpicLoginMessage();
	UFUNCTION() void HandleLauncherInviteJoinComplete(bool bSuccess);
	UFUNCTION() void HandleLobbyInviteAcceptClicked();
	UFUNCTION() void HandleLobbyInviteRejectClicked();

	/** Copies the active friends, requests and lobby invites into the lists the panel draws */
	void SyncFromFriendsSubsystem();
	void QueueLobbyInviteNotice(const FArenaLobbyInviteEntry& Invite);
	void ShowNextLobbyInviteNotice();
	void DismissLobbyInviteNotice();
	void RebuildChatContent();

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Text typed in the add-friend box, kept across panel rebuilds */
	FString PendingFriendName;

	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> AddFriendInput;

	UPROPERTY(Transient) TObjectPtr<UWidget> SidePanel;
	UPROPERTY(Transient) TObjectPtr<UWidget> SidePanelShade;
	UPROPERTY(Transient) TObjectPtr<UBackgroundBlur> SidePanelBlur;
	UPROPERTY(Transient) TObjectPtr<UWidget> LobbyInviteNotice;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LobbyInviteSenderText;
	FArenaLobbyInviteEntry ActiveLobbyInviteNotice;
	TArray<FArenaLobbyInviteEntry> QueuedLobbyInviteNotices;
	bool bLobbyInviteNoticeVisible = false;
	bool bLobbyInviteNoticeLeaving = false;
	bool bLobbyInviteNoticeEntering = false;
	float LobbyInviteNoticeTimer = 0.0f;
	float LobbyInviteNoticeSlide = 0.0f;
	float LobbyInviteNoticeOffscreenX = 72.0f;
	/** The blur clips with a radius in screen pixels while the glass border scales with the UI, so the blur follows the DPI scale */
	float SidePanelBlurScale = 0.0f;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> AvatarImages;
	bool bSidePanelOpen = false;
	float SidePanelSlide = 1.0f;
	bool bMatchSocialOverlay = false;
	bool bEpicLoginPending = false;
	bool bWaitingForLauncherInviteJoin = false;
	FString EpicLoginMessage;

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SocialViewContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> AmigosViewContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ChatViewContent;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ChatFriendsListBox;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ChatMessagesListBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChatConversationTitle;
	UPROPERTY(Transient) TObjectPtr<UEditableTextBox> ChatMessageInput;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SidePanelTabButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SidePanelTabTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UArenaFriendActionHandler>> FriendActionHandlers;
	UPROPERTY(Transient) TArray<TObjectPtr<UArenaFriendActionHandler>> ChatActionHandlers;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UImage>> FriendAvatarImages;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTextBlock>> FriendAvatarFallbacks;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UTexture2D>> FriendAvatarTextures;
	TSet<FString> PendingFriendAvatarUrls;

	UPROPERTY(Transient) TArray<FArenaFriendEntry> FriendsList;
	UPROPERTY(Transient) TArray<FArenaFriendRequestEntry> FriendRequestsList;
	UPROPERTY(Transient) TArray<FArenaLobbyInviteEntry> LobbyInvitesList;
	FString SelectedChatFriendId;
	FString SelectedChatFriendName;

	int32 ActiveSidePanelTab = 0;

	void RebuildAmigosContent();
	void InitDefaultSocialData();
	void RequestFriendAvatar(const FString& NetId, const FString& AvatarUrl);
	void BuildLockerPage(UCanvasPanel* Page);
	void RebuildSkinGrid();
	void RebuildStyleRows();
	UBorder* BuildTile(const FArenaSkinEntry& Skin, int32 Index);
	void RefreshSkinInfo();
	void SetTabSelected(int32 Tab);
	FReply HandleKey(const FKeyEvent& InKeyEvent);

	FSlateFontInfo MakeFont(UFontFace* Face, int32 Size) const;
	UTextBlock* MakeText(const FText& Text, UFontFace* Face, int32 Size, const FLinearColor& Color, bool bShadow = true);
	UButton* MakeButton(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed, float Radius);
	/** Glass pane: background blur + translucent rounded surface around Content */
	UWidget* WrapGlass(UWidget* Content, const FMargin& InnerPadding, float Radius, float Fill, float Rim, const FLinearColor& Tint = FLinearColor::White);
	void AddToCanvas(UCanvasPanel* Canvas, UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FVector2D& Position, const FVector2D& Size, bool bAutoSize = false);

	UFUNCTION()
	void HandlePlayClicked();

	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleJoinClicked();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleCloseServerBrowser();

	UFUNCTION()
	void HandleComingSoon();

	/** Leaves the group (session) the player is in: the others stop appearing in this lobby and the player is alone again */
	UFUNCTION()
	void HandleLeaveGroup();

	UPROPERTY(Transient) TObjectPtr<UButton> LeaveGroupButton;
	float GroupCheckTimer = 0.0f;

	void BuildServerBrowser(UCanvasPanel* Root);
	void BuildMapPage(UCanvasPanel* Root);
	void RefreshMapTiles();
	UFUNCTION() void HandleModeCardClicked();
	UFUNCTION() void HandleModeCardHovered();
	UFUNCTION() void HandleModeCardUnhovered();
	void RebuildServerList();

	UFUNCTION()
	void HandlePlayTab();

	/** ATRÁS: leaves the styles view first, then the locker */
	UFUNCTION()
	void HandleBackPressed();

	/** EDIT: opens the styles of the outfit under the cursor */
	UFUNCTION()
	void HandleEditPressed();

	UFUNCTION()
	void HandleEditHovered();

	UFUNCTION()
	void HandleEditUnhovered();

	UFUNCTION()
	void HandleLockerTab();

	UFUNCTION()
	void HandleCategoryOutfit();

	UFUNCTION()
	void HandleCategoryPickaxe();

	UFUNCTION()
	void HandleCategoryGlider();

	void RefreshCategoryTabs();
	TArray<FArenaSkinEntry>& ItemsOf(int32 LockerCategory);
	int32& SelectionOf(int32 LockerCategory);

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToastText;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> LobbyPage;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> LockerPage;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TabLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TabUnderlines;

	UPROPERTY(Transient)
	TObjectPtr<UUniformGridPanel> SkinGrid;

	/** The styles view: one row of option tiles per style channel (replaces the grid while editing an outfit) */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> StyleRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SkinFrames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UArenaSkinTileHandler>> TileHandlers;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SkinNameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SkinCountText;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Skins;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Pickaxes;

	UPROPERTY(Transient)
	TArray<FArenaSkinEntry> Gliders;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> CategoryButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> CategoryLabels;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> KindText;

	int32 SelectedSkin = INDEX_NONE;

	// Styles view: the grid shows the styles of one outfit instead of the outfits
	bool bStyleView = false;
	int32 StyleViewSkin = INDEX_NONE;
	TArray<int32> StyleSelection;

	// Styles view: channel and option each tile stands for (tile index = position in these arrays)
	TArray<int32> StyleTileChannel;
	TArray<int32> StyleTileOption;

	/** Outfit the EDIT button acts on, and the delayed hide that lets the cursor travel from the tile to the button */
	int32 EditTargetSkin = INDEX_NONE;
	bool bEditHovered = false;
	FTimerHandle EditHideTimer;

	UPROPERTY(Transient)
	TObjectPtr<UButton> EditButton;

	UFUNCTION(BlueprintCallable, Category = "Lobby|Locker")
	void EnterStyleView(int32 SkinIndex);
	void ExitStyleView();
	void ShowEditButton(int32 SkinIndex);
	void HideEditButton();
	/** EDIT stays on the worn outfit while it has styles; hovering another outfit with styles moves it there for a moment */
	void RestEditButton();
	int32 SelectedPickaxe = INDEX_NONE;
	int32 SelectedGlider = INDEX_NONE;
	int32 NoSelection = INDEX_NONE;
	int32 CurrentCategory = ArenaLockerCategory::Outfit;
	float ToastTimer = 0.0f;
	bool bPlayRequested = false;
	bool bLockerOpen = false;

	// ── Server browser ────────────────────────────────────────────
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> ServerBrowserPanel;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ServerListBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ServerBrowserStatusText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ServerTileHandlers;

	TArray<FArenaServerInfo> CachedServerList;
	bool bServerBrowserOpen = false;

	UPROPERTY(Transient) TObjectPtr<UArenaEmoteWheel> EmoteWheel;
	UPROPERTY(Transient) TObjectPtr<UWidget> StopDanceButton;
	float StopDanceAlpha = 0.0f;
	UFUNCTION() void HandleStopDance();
	TSharedPtr<class IInputProcessor> KeyProcessor;

	// Map selector
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MapPage;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ModeNameText;
	UPROPERTY(Transient) TObjectPtr<UImage> ModeArtworkImage;
	UPROPERTY(Transient) TObjectPtr<UBorder> ModeCardHoverFrame;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> MapFrames;
	UPROPERTY(Transient) TArray<TObjectPtr<UArenaSkinTileHandler>> MapHandlers;
	TArray<FString> MapPaths;
	TArray<FText> MapNames;
	int32 SelectedMap = INDEX_NONE;
	bool bMapPageOpen = false;
};

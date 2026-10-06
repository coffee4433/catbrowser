// Fortnite style VIDEO settings screen, built in code. Applies and saves every option immediately.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "ArenaSettingsWidget.generated.h"

class UBorder;
class UBackgroundBlur;
class UButton;
class UFontFace;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UArenaSettingsWidget;
class APlayerController;

/** Forwards the clicks / hover of one settings row (dynamic delegates carry no payload) */
UCLASS()
class ARENA_API UArenaSettingRowHandler : public UObject
{
	GENERATED_BODY()

public:
	int32 Row = INDEX_NONE;
	TWeakObjectPtr<UArenaSettingsWidget> Owner;

	UFUNCTION() void HandleNext();
	UFUNCTION() void HandlePrev();
	UFUNCTION() void HandleHovered();
	UFUNCTION() void HandleTab();
	UFUNCTION() void HandleKeyRow();
};

UCLASS()
class ARENA_API UArenaSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UArenaSettingsWidget(const FObjectInitializer& ObjectInitializer);

	/** Opens the settings screen on top of everything. FocusOnClose gets the keyboard back when it closes (the
	 *  lobby); without it the game input comes back (in a match). */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	static UArenaSettingsWidget* Open(APlayerController* PC, UUserWidget* FocusOnClose = nullptr);

	/** Applies the saved settings (Fortnite's defaults on first launch) */
	static void ApplySavedSettings();
	static FLinearColor GetEditGridColor();
	static void SetEditGridColor(FLinearColor Color);
	static FLinearColor GetBuildPreviewColor();
	static void SetBuildPreviewColor(FLinearColor Color);
	static int32 GetBuildTextureIndex();
	static void SetBuildTextureIndex(int32 Index);

	/** Resets everything to the Fortnite defaults */
	UFUNCTION(BlueprintCallable, Category = "Settings")
	void ResetToDefaults();

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void Close();

	void StepRow(int32 Row, int32 Direction);
	void ShowTab(int32 Tab);
	void BeginCapture(int32 ActionIndex);
	void HoverRow(int32 Row);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TObjectPtr<UFontFace> HeadingFontFace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TObjectPtr<UFontFace> BodyFontFace;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	struct FRowWidgets
	{
		TObjectPtr<UTextBlock> Value;
		TArray<TObjectPtr<UBorder>> Segments;
		TObjectPtr<UBorder> Frame;
	};

	void BuildLayout();
	void AddSection(UVerticalBox* List, const FText& Title);
	void AddRow(UVerticalBox* List, int32 Setting);
	void AddEditGridColorRow(UVerticalBox* List);
	void AddBuildPreviewColorRow(UVerticalBox* List);
	void AddBuildTextureRow(UVerticalBox* List);
	void AddKeyRow(UVerticalBox* List, int32 ActionIndex);
	void RefreshEditGridColorSwatch();
	void RefreshBuildPreviewColorSwatch();
	void RefreshBuildTextureValue();
	void OpenGridColorPicker(bool bBuildPreview);
	void RefreshKeys();
	void RefreshRow(int32 Row);
	void RefreshDescription();
	FReply HandleKey(const FKeyEvent& InKeyEvent);

	FSlateFontInfo MakeFont(UFontFace* Face, int32 Size) const;
	UTextBlock* MakeText(const FText& Text, UFontFace* Face, int32 Size, const FLinearColor& Color);
	UButton* MakeButton(const FLinearColor& Normal, const FLinearColor& Hovered, float Radius);

	UFUNCTION() void HandleResetClicked();
	UFUNCTION() void HandleBackClicked();
	UFUNCTION() void HandleButtonHovered();
	UFUNCTION() void HandleEditGridColorClicked();
	UFUNCTION() void HandleBuildPreviewColorClicked();
	UFUNCTION() void HandleBuildTextureNext();
	UFUNCTION() void HandleBuildTexturePrev();

	UPROPERTY(Transient) TArray<TObjectPtr<UArenaSettingRowHandler>> Handlers;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> ListScroll;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> VideoList;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ControlsList;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SoonList;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> TabBorders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> KeyTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> KeyCaps;
	int32 ActiveTab = 0;
	int32 CaptureAction = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescBody;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> DescOptions;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FpsText;
	UPROPERTY(Transient) TObjectPtr<UBorder> EditGridColorSwatch;
	UPROPERTY(Transient) TObjectPtr<UBorder> BuildPreviewColorSwatch;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuildTextureValue;
	UPROPERTY(Transient) TArray<TObjectPtr<UBackgroundBlur>> PanelBlurs;
	float BlurScale = 0.0f;

	TArray<int32> RowSettings;
	TArray<FRowWidgets> RowWidgets;
	int32 HoveredRow = 0;
	TWeakObjectPtr<UUserWidget> FocusOnClose;
	float FpsAccum = 0.0f;
	int32 FpsFrames = 0;
};

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArenaInventoryWidget.generated.h"

class AArenaPlayerController;
class SArenaInventorySlate;
class UFortnitePortingCharacterComponent;
class UFortnitePortingWeaponData;
class UTexture2D;

UCLASS()
class ARENA_API UArenaInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeInventory(AArenaPlayerController* InController, UFortnitePortingCharacterComponent* InInventory);
	void Close();
	void DropWeaponById(const FString& WeaponId);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	friend class SArenaInventorySlate;

	void RefreshSlateState(bool bRebuildWeapons = true);
	void SelectWeapon(const FString& WeaponId);
	void MoveWeaponToIndex(const FString& WeaponId, int32 TargetIndex);
	void SwapWeaponsById(const FString& FirstWeaponId, const FString& SecondWeaponId);
	void EquipSelectedWeapon();
	void DropWeapon(UFortnitePortingWeaponData* Weapon);
	UFortnitePortingWeaponData* FindWeaponById(const FString& WeaponId) const;
	FString GetWeaponId(const UFortnitePortingWeaponData* Weapon) const;

	TWeakObjectPtr<AArenaPlayerController> OwnerController;
	TWeakObjectPtr<UFortnitePortingCharacterComponent> Inventory;
	UPROPERTY(Transient) TObjectPtr<UFortnitePortingWeaponData> SelectedWeapon;
	UPROPERTY(Transient) TObjectPtr<UFortnitePortingWeaponData> LastEquippedSnapshot;
	UPROPERTY(Transient) TArray<TObjectPtr<UFortnitePortingWeaponData>> LastWeaponSnapshot;
	UPROPERTY(Transient) TArray<TObjectPtr<UFortnitePortingWeaponData>> DisplayWeapons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> MaterialIcons;
	TSharedPtr<SArenaInventorySlate> InventorySlate;
	int32 LastAmmoSnapshot = INDEX_NONE;
	float RefreshTimer = 0.0f;
};

#pragma once

#include "CoreMinimal.h"
#include "BuildingFoorItem.h"
#include "Blueprint/UserWidget.h"
#include "BuildingItem.generated.h"

class UVerticalBox;
class UButton;
class UTextBlock;
class UBuildingFoorItem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBuildingFloorSelected, AActor*, Building, int32, Floor);

UCLASS()
class DTSCORE_API UBuildingItem : public UUserWidget
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Init")
    void Init(AActor* InBuilding, const TArray<int32>& Floors);

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite, EditAnywhere)
    UButton* Btn_Header;

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite, EditAnywhere)
    UTextBlock* Txt_Title;

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite, EditAnywhere)
    UVerticalBox* FloorList;

    UPROPERTY(EditAnywhere)
    TSubclassOf<UBuildingFoorItem> FloorItemClass;

    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnBuildingFloorSelected OnFloorSelected;

private:
    UPROPERTY()
    AActor* Building;
    UPROPERTY()
    bool bExpanded = false;

    UFUNCTION(BlueprintCallable, Category = "Toggle")
    void ToggleExpand();
    UFUNCTION(BlueprintCallable, Category = "CreateFloors")
    void CreateFloors(const TArray<int32>& Floors);
    UFUNCTION(BlueprintCallable, Category = "Click Event")
	void HandleFloorClicked(int32 Floor);
};
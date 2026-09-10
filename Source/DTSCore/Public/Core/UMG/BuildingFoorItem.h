// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BuildingFoorItem.generated.h"

class UOverlay;
class UButton;
class UTextBlock;
class UImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFloorClicked, int32, Floor);

USTRUCT(BlueprintType)
struct FButtonStyleConfig
{
    GENERATED_USTRUCT_BODY()

public:

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    UTexture2D* Normal = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    UTexture2D* Hovered = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    UTexture2D* Pressed = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    FLinearColor NormalColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    FLinearColor HoveredColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    FLinearColor PressedColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
    FVector2D Size = FVector2D(208.f, 60.f);
};

/**
 * 
 */
UCLASS()
class DTSCORE_API UBuildingFoorItem : public UUserWidget
{
	GENERATED_BODY()
	
public:
    UFUNCTION(BlueprintCallable, Category = "Init")
    void Init(int32 InFloor);

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite)
    UOverlay* Overlay_FoorItem;

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite)
    UButton* Btn_Click;

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite)
    UTextBlock* Txt_Name;

    UPROPERTY(meta = (BindWidget), BlueprintReadWrite)
    UImage* Background;



    UPROPERTY(BlueprintAssignable, Category = "Events")
    FOnFloorClicked OnFloorClicked;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    FButtonStyleConfig StyleConfig;

    UFUNCTION(BlueprintCallable, Category = "Set Button Style")
    void SetButtonStyle(const FButtonStyleConfig& Config);
    UFUNCTION(BlueprintCallable, Category = "Set Widget Padding")
    void SetPaddingwithWidget(UWidget* Widget, FMargin Pad);

private:
    UPROPERTY()
    int32 Floor;

    UFUNCTION(BlueprintCallable, Category = "Click Event")
    void HandleClick();
};

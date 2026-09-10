#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "POIBase.generated.h"

class UButton;
class UImage;
class UTextBlock;

// ===== 点击事件（用于父UI互斥管理）=====
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPOIClicked, UPOIBase*, ClickedItem);

UCLASS()
class DTSCORE_API UPOIBase : public UUserWidget
{
	GENERATED_BODY()

public:

	// ===== 是否显示文本 =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POIConfig")
	bool bUseText = false;

	// ===== 当前选中状态 =====
	UPROPERTY(EditAnywhere, BlueprintReadWrite,Category ="POIConfig")
	bool bIsSelected = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "POIConfig")
	bool bTextIsSelected = false;

	// ===== 绑定控件 =====
	UPROPERTY(meta = (BindWidget), BlueprintReadwrite)
	UButton* Btn_Click;

	UPROPERTY(meta = (BindWidget), BlueprintReadwrite)
	UImage* Img_Background;

	UPROPERTY(meta = (BindWidget), BlueprintReadwrite)
	UTextBlock* Txt_Name;

	UPROPERTY(BlueprintAssignable, Category = "POI")
	FOnPOIClicked OnPOIClicked;
	//*****************Font
	// 文本 Padding
	// 字体配置
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Style")
	FSlateFontInfo FontInfo;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FMargin TextPadding;

	// 水平对齐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	TEnumAsByte<EHorizontalAlignment> TextHorizontalAlignment = HAlign_Fill;

	// 垂直对齐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	TEnumAsByte<EVerticalAlignment> TextVerticalAlignment = VAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FSlateColor SelectedColor = FSlateColor(FLinearColor::Yellow);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FSlateColor UnSelectedColor = FSlateColor(FLinearColor::White);
	//****************Image
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	UObject* BackgroundResource;

	// 背景尺寸
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	FVector2D BackgroundSize = FVector2D(64.f, 64.f);

	// 背景颜色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	FSlateColor BackgroundTint = FSlateColor(FLinearColor::White);
	//*************button
	// Button样式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FSlateBrush NormalBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FSlateBrush HoveredBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FSlateBrush PressedBrush;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FLinearColor NormalColor = FLinearColor(1, 1, 1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FLinearColor HoveredColor = FLinearColor(0.8f, 0.8f, 0.8f, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FLinearColor PressedColor = FLinearColor(0.6f, 0.6f, 0.6f, 1);
	// Button Padding
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	FMargin ButtonPadding;

	// 水平对齐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	TEnumAsByte<EHorizontalAlignment> ButtonHorizontalAlignment = HAlign_Fill;

	// 垂直对齐
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Button")
	TEnumAsByte<EVerticalAlignment> ButtonVerticalAlignment = VAlign_Fill;


protected:

	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	// 按钮点击回调
	UFUNCTION()
	void OnBtnClicked();
	UFUNCTION()
	void DefaultSelected();

public:

	// 设置选中状态
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetSelected(bool bSelected);

	// 切换状态（一般不用，交给管理器）
	UFUNCTION(BlueprintCallable, Category = "POI")
	void ToggleSelected();

	//Font
	// 设置文本
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetTextFont(const FSlateFontInfo& InFont);
	UFUNCTION(BlueprintCallable, Category = "POI")
	void ApplyTextFont();
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetNameText(const FString& InText);

	// 控制是否显示文本
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetUseText(bool bEnable);
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetFontState(bool bSelected);
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetTextLayout();
	// Image
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetBackgroundImage();
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetBackgroundVis(bool bIsvis);
	//Button
	// 设置按钮布局
	UFUNCTION(BlueprintCallable, Category = "POI")
	void SetButtonLayout();
	// 设置按钮样式
	UFUNCTION(BlueprintCallable, Category = "Style")
	void ApplyButtonStyle();
};
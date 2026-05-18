#include "POIBase.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/OverlaySlot.h"

void UPOIBase::NativePreConstruct()
{
	Super::NativePreConstruct();

	// 控制文本显隐（编辑器 + 运行时都生效）
	if (Txt_Name)
	{
		Txt_Name->SetVisibility(
			bUseText
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed
		);
	}
}

void UPOIBase::NativeConstruct()
{
	Super::NativeConstruct();

	// 绑定按钮点击
	/*if (Btn_Click)
	{
		Btn_Click->OnClicked.AddDynamic(this, &UPOIBase::OnBtnClicked);
	}*/

	// 初始化状态
	DefaultSelected();
}

void UPOIBase::OnBtnClicked()
{
	// ❗不自己Toggle，交给外部管理（互斥）
	OnPOIClicked.Broadcast(this);
}
void UPOIBase::DefaultSelected()
{
	if (Img_Background)
	{
		Img_Background->SetVisibility(
			bIsSelected
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed
		);
	}

	// 文本高亮（可选）
	if (Txt_Name)
	{
		Txt_Name->SetColorAndOpacity(
			bTextIsSelected
			? FSlateColor(FLinearColor::Yellow)
			: FSlateColor(FLinearColor::White)
		);
	}
}
void UPOIBase::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;

	// 背景显示控制
	if (Img_Background)
	{
		Img_Background->SetVisibility(
			bIsSelected
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed
		);
	}

	// 文本高亮（可选）
	if (Txt_Name)
	{
		Txt_Name->SetColorAndOpacity(
			bIsSelected
			? FSlateColor(FLinearColor::Yellow)
			: FSlateColor(FLinearColor::White)
		);
	}
}

void UPOIBase::ToggleSelected()
{
	SetSelected(!bIsSelected);
}

void UPOIBase::SetTextFont(const FSlateFontInfo& InFont)
{
	if (!Txt_Name) return;

	Txt_Name->SetFont(InFont);
}

void UPOIBase::ApplyTextFont()
{
	if (!Txt_Name) return;

	Txt_Name->SetFont(FontInfo);
}

void UPOIBase::SetNameText(const FString& InText)
{
	if (Txt_Name)
	{
		Txt_Name->SetText(FText::FromString(InText));
	}
}

void UPOIBase::SetUseText(bool bEnable)
{
	bUseText = bEnable;

	if (Txt_Name)
	{
		Txt_Name->SetVisibility(
			bUseText
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed
		);
	}
}

void UPOIBase::SetFontState(bool bSelected)
{
	if (!Txt_Name) return;

	const FSlateColor& TargetColor = bSelected ? SelectedColor : UnSelectedColor;

	Txt_Name->SetColorAndOpacity(TargetColor);
}

void UPOIBase::SetTextLayout()
{
	if (!Txt_Name) return;

	// 获取 OverlaySlot
	if (UOverlaySlot* TextSlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(Txt_Name))
	{
		// 设置 Padding
		TextSlot->SetPadding(TextPadding);

		// 设置水平对齐
		TextSlot->SetHorizontalAlignment(TextHorizontalAlignment);

		// 设置垂直对齐
		TextSlot->SetVerticalAlignment(TextVerticalAlignment);
	}
}

void UPOIBase::SetBackgroundImage()
{
	if (!Img_Background) return;

	FSlateBrush Brush;

	// 贴图 / 材质
	Brush.SetResourceObject(BackgroundResource);

	// 尺寸
	Brush.ImageSize = BackgroundSize;

	// Tint颜色
	Brush.TintColor = BackgroundTint;

	// 设置到Image
	Img_Background->SetBrush(Brush);
}

void UPOIBase::SetBackgroundVis(bool bIsvis)
{
	if (bIsvis)
	{
		Img_Background->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		Img_Background->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UPOIBase::SetButtonLayout()
{
	if (!Btn_Click) return;

	// 获取 OverlaySlot
	if (UOverlaySlot* BtnSlot = UWidgetLayoutLibrary::SlotAsOverlaySlot(Btn_Click))
	{
		// Padding
		BtnSlot->SetPadding(ButtonPadding);

		// 水平对齐
		BtnSlot->SetHorizontalAlignment(ButtonHorizontalAlignment);

		// 垂直对齐
		BtnSlot->SetVerticalAlignment(ButtonVerticalAlignment);
	}
}

void UPOIBase::ApplyButtonStyle()
{
	if (!Btn_Click) return;

	FButtonStyle Style;
	FSlateBrush Normal = NormalBrush;
	FSlateBrush Hovered = HoveredBrush;
	FSlateBrush Pressed = PressedBrush;

	// ⭐设置 Tint
	Normal.TintColor = FSlateColor(NormalColor);
	Hovered.TintColor = FSlateColor(HoveredColor);
	Pressed.TintColor = FSlateColor(PressedColor);


	// 三种状态
	Style.SetNormal(Normal);
	Style.SetHovered(Hovered);
	Style.SetPressed(Pressed);


	// 可选：点击后不偏移
	Style.NormalPadding = FMargin(0);
	Style.PressedPadding = FMargin(0);

	Btn_Click->SetStyle(Style);
}

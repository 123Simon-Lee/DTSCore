#include "BuildingFoorItem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/OverlaySlot.h"

void UBuildingFoorItem::Init(int32 InFloor)
{
    Floor = InFloor;

    if (Txt_Name)
    {
        Txt_Name->SetText(FText::FromString(FString::Printf(TEXT("%d层"), Floor)));
    }

    if (Btn_Click)
    {
        Btn_Click->OnClicked.AddDynamic(this, &UBuildingFoorItem::HandleClick);
    }
}

void UBuildingFoorItem::SetButtonStyle(const FButtonStyleConfig& Config)
{
    if (!Btn_Click) return;

    FButtonStyle Style = Btn_Click->WidgetStyle;

    auto MakeBrush = [&](UTexture2D* Tex, FLinearColor Color) -> FSlateBrush
        {
            FSlateBrush Brush;
            Brush.SetResourceObject(Tex);
            Brush.ImageSize = Config.Size;
            Brush.TintColor = FSlateColor(Color);
            return Brush;
        };

    Style.SetNormal(MakeBrush(Config.Normal, Config.NormalColor));
    Style.SetHovered(MakeBrush(Config.Hovered, Config.HoveredColor));
    Style.SetPressed(MakeBrush(Config.Pressed, Config.PressedColor));

    Btn_Click->SetStyle(Style);
}

void UBuildingFoorItem::SetPaddingwithWidget(UWidget* Widget, FMargin Pad)
{
    if (!Widget) return;

    if (UOverlaySlot* SlotPadding = Cast<UOverlaySlot>(Widget->Slot))
    {
        SlotPadding->SetPadding(Padding);
    }
}

void UBuildingFoorItem::HandleClick()
{
    OnFloorClicked.Broadcast(Floor);
}
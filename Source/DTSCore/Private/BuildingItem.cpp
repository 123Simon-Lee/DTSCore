#include "BuildingItem.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UBuildingItem::Init(AActor* InBuilding, const TArray<int32>& Floors)
{
    Building = InBuilding;

    if (Txt_Title)
    {
        Txt_Title->SetText(FText::FromString(Building->GetName()));
    }

    if (Btn_Header)
    {
        Btn_Header->OnClicked.AddDynamic(this, &UBuildingItem::ToggleExpand);
    }

    CreateFloors(Floors);
}

void UBuildingItem::ToggleExpand()
{
    bExpanded = !bExpanded;

    FloorList->SetVisibility(bExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UBuildingItem::CreateFloors(const TArray<int32>& Floors)
{
    if (!FloorItemClass) return;

    for (int32 Floor : Floors)
    {
        UBuildingFoorItem* Item = CreateWidget<UBuildingFoorItem>(
            GetWorld(),
            FloorItemClass
        );

        Item->Init(Floor);

        Item->OnFloorClicked.AddDynamic(this, &UBuildingItem::HandleFloorClicked);
       

        FloorList->AddChild(Item);
    }
}

void UBuildingItem::HandleFloorClicked(int32 Floor)
{
	OnFloorSelected.Broadcast(Building, Floor);
}

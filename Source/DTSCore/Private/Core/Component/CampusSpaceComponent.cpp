#include "Core/Component/CampusSpaceComponent.h"

#include "Core/Component/BuildingSpaceComponent.h"
#include "Core/Component/FloorSpaceComponent.h"
#include "Space/SpaceSubsystem.h"
#include "Space/Space.h"

UCampusSpaceComponent::UCampusSpaceComponent()
{
  PrimaryComponentTick.bCanEverTick = false;
}

void UCampusSpaceComponent::InitializeCampus(
    ASpace *InCampusSpace)
{
  CampusSpace = InCampusSpace;
  Buildings.Reset();
  ActiveBuildings.Reset();
  ActiveFloors.Reset();

  UWorld *World = GetWorld();

  if (!IsValid(World))
  {
    return;
  }

  USpaceSubsystem *SpaceSubsystem =
      World->GetSubsystem<USpaceSubsystem>();

  if (IsValid(SpaceSubsystem))
  {
    SpaceSubsystem->SetSelectMode(
        ESpaceSelectMode::Single);
  }
}

void UCampusSpaceComponent::RegisterBuilding(UBuildingSpaceComponent *BuildingComponent)
{
  if (IsValid(BuildingComponent))
  {
    Buildings.Add(BuildingComponent->GetBuildingId(), BuildingComponent);
  }
}

void UCampusSpaceComponent::SelectBuilding(UBuildingSpaceComponent *BuildingComponent, bool bAppend)
{
  if (!IsValid(BuildingComponent))
  {
    return;
  }

  // 当前使用单选模式，bAppend暂时不参与选择逻辑
  ActiveBuildings.Reset();
  ActiveFloors.Reset();

  ActiveBuildings.Add(BuildingComponent);

  /*
   * 进入SpaceSubsystem：
   * BuildingComponent::SelectSingle
   * → ToggleSingleSelect(ABuilding)
   * → ABuilding::OnSpaceSelected
   * → UBuildingSpaceComponent::OnSpaceSelected
   */
  BuildingComponent->SelectSingle();
}

void UCampusSpaceComponent::SelectFloor(UFloorSpaceComponent *FloorComponent, bool bAppend)
{
  if (!IsValid(FloorComponent))
  {
    return;
  }

  ActiveFloors.Reset();
  ActiveFloors.Add(FloorComponent);

  /*
   * ToggleSingleSelect会自动取消之前选中的楼栋，
   * 然后选择当前楼层。
   */
  FloorComponent->SelectSingle();
}

void UCampusSpaceComponent::DeselectFloor(UFloorSpaceComponent *FloorComponent)
{
  ActiveFloors.Remove(FloorComponent);
  if (IsValid(FloorComponent))
  {
    FloorComponent->OnSpaceDeselected();
  }
}

void UCampusSpaceComponent::ClearActiveSelection()
{
  UWorld *World = GetWorld();

  if (IsValid(World))
  {
    USpaceSubsystem *SpaceSubsystem =
        World->GetSubsystem<USpaceSubsystem>();

    if (IsValid(SpaceSubsystem))
    {
      SpaceSubsystem->ClearSelection();
    }
  }

  ActiveBuildings.Reset();
  ActiveFloors.Reset();
}

UBuildingSpaceComponent *UCampusSpaceComponent::FindBuildingByCode(const FString &BuildingCode) const
{
  const TObjectPtr<UBuildingSpaceComponent> *Found = Buildings.Find(BuildingCode);
  return Found ? Found->Get() : nullptr;
}

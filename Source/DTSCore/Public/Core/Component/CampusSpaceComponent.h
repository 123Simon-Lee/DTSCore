#pragma once

#include "CoreMinimal.h"
#include "Space/Component/SpaceSelectorComponent.h"
#include "CampusSpaceComponent.generated.h"

class ASpace;
class UBuildingSpaceComponent;
class UFloorSpaceComponent;

/**
 * 园区级空间状态组件。
 *
 * 保存当前活跃楼栋/楼层集合；Primary 项由 Manager 的 SceneScope 对外暴露。
 * 单选和多选共用同一份状态，不再由 Manager 维护平行的数据结构。
 */
UCLASS(ClassGroup = (Space), meta = (BlueprintSpawnableComponent), BlueprintType)
class DTSCORE_API UCampusSpaceComponent : public USpaceSelectorComponent
{
  GENERATED_BODY()

public:
  UCampusSpaceComponent();

  void InitializeCampus(ASpace *InCampusSpace);
  void RegisterBuilding(UBuildingSpaceComponent *BuildingComponent);

  UFUNCTION(BlueprintCallable, Category = "Campus")
  void SelectBuilding(UBuildingSpaceComponent *BuildingComponent, bool bAppend);

  UFUNCTION(BlueprintCallable, Category = "Campus")
  void SelectFloor(UFloorSpaceComponent *FloorComponent, bool bAppend);

  UFUNCTION(BlueprintCallable, Category = "Campus")
  void DeselectFloor(UFloorSpaceComponent *FloorComponent);

  UFUNCTION(BlueprintCallable, Category = "Campus")
  void ClearActiveSelection();

  UFUNCTION(BlueprintPure, Category = "Campus")
  UBuildingSpaceComponent *FindBuildingByCode(const FString &BuildingCode) const;

  UPROPERTY(BlueprintReadOnly, Category = "Campus")
  TObjectPtr<ASpace> CampusSpace = nullptr;

  UPROPERTY(BlueprintReadOnly, Category = "Campus")
  TMap<FString, TObjectPtr<UBuildingSpaceComponent>> Buildings;

  UPROPERTY(BlueprintReadOnly, Category = "Campus")
  TArray<TObjectPtr<UBuildingSpaceComponent>> ActiveBuildings;

  UPROPERTY(BlueprintReadOnly, Category = "Campus")
  TArray<TObjectPtr<UFloorSpaceComponent>> ActiveFloors;
};

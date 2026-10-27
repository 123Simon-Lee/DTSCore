#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MeasurementRegionSubsystem.generated.h"

class UMeasureRegionSelectorComponent;

DECLARE_MULTICAST_DELEGATE_TwoParams(
    FOnActiveMeasureRegionChanged,
    UMeasureRegionSelectorComponent *,
    UMeasureRegionSelectorComponent *);

UCLASS()
class MEASUREMENT_API UMeasurementRegionSubsystem : public UWorldSubsystem
{
  GENERATED_BODY()

public:
  void RegisterSelectedRegion(UMeasureRegionSelectorComponent *Region);

  void UnregisterSelectedRegion(UMeasureRegionSelectorComponent *Region);

  UMeasureRegionSelectorComponent *GetActiveRegion() const
  {
    return ActiveRegion.Get();
  }

  FOnActiveMeasureRegionChanged OnActiveRegionChanged;

private:
  // 缓存所有选中的区域组件
  UPROPERTY()
  TArray<TObjectPtr<UMeasureRegionSelectorComponent>> SelectedRegions;
  // 缓存当前选中的区域组件
  UPROPERTY()
  TObjectPtr<UMeasureRegionSelectorComponent> ActiveRegion;
};
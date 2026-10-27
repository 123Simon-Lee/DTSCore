#include "MeasurementRegionSubsystem.h"
#include "MeasureRegionSelectorComponent.h"

void UMeasurementRegionSubsystem::RegisterSelectedRegion(UMeasureRegionSelectorComponent *Region)
{
  if (!IsValid(Region))
  {
    return;
  }

  UMeasureRegionSelectorComponent *OldRegion = ActiveRegion.Get();

  SelectedRegions.AddUnique(Region);
  ActiveRegion = Region;

  if (OldRegion != ActiveRegion.Get())
  {
    OnActiveRegionChanged.Broadcast(
        OldRegion,
        ActiveRegion.Get());
  }
}

void UMeasurementRegionSubsystem::UnregisterSelectedRegion(
    UMeasureRegionSelectorComponent *Region)
{
  if (!Region)
  {
    return;
  }

  SelectedRegions.Remove(Region);

  if (ActiveRegion != Region)
  {
    return;
  }

  UMeasureRegionSelectorComponent *OldRegion = ActiveRegion.Get();

  ActiveRegion = SelectedRegions.IsEmpty()
                     ? nullptr
                     : SelectedRegions.Last();

  OnActiveRegionChanged.Broadcast(OldRegion, ActiveRegion);
}
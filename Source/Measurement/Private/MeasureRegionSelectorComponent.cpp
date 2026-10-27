#include "MeasureRegionSelectorComponent.h"

#include "MeasurementRegionSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace
{
  bool IsPointInsideLoop(
      const FVector2D &Point,
      const TArray<FVector2D> &Loop)
  {
    if (Loop.Num() < 3)
    {
      return false;
    }

    bool bInside = false;
    int32 PreviousIndex = Loop.Num() - 1;

    for (int32 Index = 0;
         Index < Loop.Num();
         ++Index)
    {
      const FVector2D &Current = Loop[Index];
      const FVector2D &Previous = Loop[PreviousIndex];

      const bool bCrossesY =
          (Current.Y > Point.Y) !=
          (Previous.Y > Point.Y);

      if (bCrossesY)
      {
        const double IntersectionX =
            (Previous.X - Current.X) *
                (Point.Y - Current.Y) /
                (Previous.Y - Current.Y) +
            Current.X;

        if (Point.X < IntersectionX)
        {
          bInside = !bInside;
        }
      }

      PreviousIndex = Index;
    }

    return bInside;
  }

  bool IsPointInsidePolygonWithHoles(
      const FVector2D &Point,
      const FMeasurePolygonLoop &OuterBoundary,
      const TArray<FMeasurePolygonLoop> &Holes)
  {
    if (!IsPointInsideLoop(
            Point,
            OuterBoundary.Points))
    {
      return false;
    }

    for (const FMeasurePolygonLoop &Hole : Holes)
    {
      if (IsPointInsideLoop(Point, Hole.Points))
      {
        return false;
      }
    }

    return true;
  }
}
void UMeasureRegionSelectorComponent::OnSpaceSelected()
{
  Super::OnSpaceSelected();

  if (UWorld *World = GetWorld())
  {
    if (UMeasurementRegionSubsystem *Subsystem = World->GetSubsystem<UMeasurementRegionSubsystem>())
    {
      Subsystem->RegisterSelectedRegion(this);
    }
  }
}

void UMeasureRegionSelectorComponent::OnSpaceDeselected()
{
  Super::OnSpaceDeselected();

  if (UWorld *World = GetWorld())
  {
    if (UMeasurementRegionSubsystem *Subsystem = World->GetSubsystem<UMeasurementRegionSubsystem>())
    {
      Subsystem->UnregisterSelectedRegion(this);
    }
  }
}

FTransform UMeasureRegionSelectorComponent::GetPlaneWorldTransform() const
{
  const AActor *Owner = GetOwner();

  if (!Owner)
  {
    return RelativePlaneTransform;
  }

  return RelativePlaneTransform * Owner->GetActorTransform();
}

bool UMeasureRegionSelectorComponent::ContainsLocalPoint(const FVector2D &Point) const
{
  switch (RegionShape)
  {
  case EMeasureRegionShape::Infinite:
    return true;

  case EMeasureRegionShape::Rectangle:
    return FMath::Abs(Point.X) <= RectangleHalfExtent.X &&
           FMath::Abs(Point.Y) <= RectangleHalfExtent.Y;
  case EMeasureRegionShape::Polygon:
    return IsPointInsidePolygonWithHoles(
        Point,
        OuterBoundary,
        Holes);

  default:
    return false;
  }
}

bool UMeasureRegionSelectorComponent::ProjectCursorToRegion(APlayerController *PlayerController, FVector2D &OutLocalPoint, FVector &OutWorldPoint) const
{

  if (!PlayerController)
  {
    return false;
  }

  FVector RayOrigin;
  FVector RayDirection;

  if (!PlayerController->DeprojectMousePositionToWorld(
          RayOrigin,
          RayDirection))
  {
    return false;
  }

  RayDirection.Normalize();

  const FTransform PlaneTransform = GetPlaneWorldTransform();
  const FVector PlaneOrigin = PlaneTransform.GetLocation();
  const FVector PlaneNormal =
      PlaneTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();

  const double Denominator =
      FVector::DotProduct(RayDirection, PlaneNormal);

  if (FMath::Abs(Denominator) <= KINDA_SMALL_NUMBER)
  {
    return false;
  }

  const double RayDistance =
      FVector::DotProduct(
          PlaneOrigin - RayOrigin,
          PlaneNormal) /
      Denominator;

  if (RayDistance < 0.0)
  {
    return false;
  }

  const FVector PlaneWorldPoint =
      RayOrigin + RayDirection * RayDistance;

  const FVector LocalPoint =
      PlaneTransform.InverseTransformPosition(
          PlaneWorldPoint);

  OutLocalPoint = FVector2D(
      LocalPoint.X,
      LocalPoint.Y);

  if (!ContainsLocalPoint(OutLocalPoint))
  {
    return false;
  }

  OutWorldPoint = LocalPointToWorld(OutLocalPoint);

  return true;
}

FVector UMeasureRegionSelectorComponent::LocalPointToWorld(const FVector2D &Point, float ExtraOffset) const
{
  return GetPlaneWorldTransform().TransformPosition(
      FVector(
          Point.X,
          Point.Y,
          SurfaceOffset + ExtraOffset));
}

FVector2D UMeasureRegionSelectorComponent::WorldPointToLocal2D(const FVector &Point) const
{
  const FVector Local = GetPlaneWorldTransform().InverseTransformPosition(Point);

  return FVector2D(Local.X, Local.Y);
}
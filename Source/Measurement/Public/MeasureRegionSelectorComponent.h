#pragma once

#include "CoreMinimal.h"
#include "Space/Component/SpaceSelectorComponent.h"
#include "MeasureRegionSelectorComponent.generated.h"

class APlayerController;
class UMeasurementRegionSubsystem;

UENUM(BlueprintType)
enum class EMeasureRegionShape : uint8
{
  Infinite,
  Rectangle,
  Polygon
};

USTRUCT(BlueprintType)
struct FMeasurePolygonLoop
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TArray<FVector2D> Points;
};
// 测量区域组件
UCLASS(ClassGroup = (Measurement), meta = (BlueprintSpawnableComponent))
class MEASUREMENT_API UMeasureRegionSelectorComponent : public USpaceSelectorComponent
{
  GENERATED_BODY()

public:
  // 选择组件的通知
  virtual void OnSpaceSelected();
  // 取消组件的通知
  virtual void OnSpaceDeselected();
  /**
   * 测量平面相对于 Space 的变换。
   *
   * Local X/Y：测量二维坐标轴
   * Local Z：测量平面法线
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Measurement")
  FTransform RelativePlaneTransform = FTransform::Identity;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Measurement")
  EMeasureRegionShape RegionShape = EMeasureRegionShape::Rectangle;

  /** 矩形区域半尺寸，单位厘米。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasureRegion", meta = (EditCondition = "RegionShape == EMeasureRegionShape::Rectangle"))
  FVector2D RectangleHalfExtent = FVector2D(5000.0, 5000.0);

  /** 多边形外轮廓，使用测量平面局部二维坐标。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasureRegion", meta = (EditCondition = "RegionShape == EMeasureRegionShape::Polygon"))
  FMeasurePolygonLoop OuterBoundary;

  /** 电梯井、楼梯井、中庭等不可测量区域。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasureRegion", meta = (EditCondition = "RegionShape == EMeasureRegionShape::Polygon"))
  TArray<FMeasurePolygonLoop> Holes;

  /** 绘制内容离开地面的高度，避免 Z-Fighting。 */
  // 避免深度冲突/Z值打架 给个向上抬的偏移量
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasureRegion")
  float SurfaceOffset = 2.0f;

  UFUNCTION(BlueprintPure, Category = "MeasureRegion")
  FTransform GetPlaneWorldTransform() const;

  UFUNCTION(BlueprintPure, Category = "MeasureRegion")
  bool ContainsLocalPoint(const FVector2D &Point) const;

  bool ProjectCursorToRegion(APlayerController *PlayerController, FVector2D &OutLocalPoint, FVector &OutWorldPoint) const;

  FVector LocalPointToWorld(const FVector2D &Point, float ExtraOffset = 0.0f) const;

  FVector2D WorldPointToLocal2D(const FVector &Point) const;
};

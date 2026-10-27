// 测量距离组件
#pragma once
#include "CoreMinimal.h"
#include "MeasureToolComponentBase.h"
#include "DistanceMeasureComponent.generated.h"

// 测量距离组件委托 用于通知距离测量变化
// DistanceMeters：米制距离；DisplayText：格式化显示文本；LabelWorldLocation：标签建议摆放位置
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDistanceMeasurementChanged, double, DistanceMeters, FText, DisplayText, FVector, LabelWorldLocation);

/**
 * 距离测量工具（业务层）。
 *
 * 交互流程：
 * 激活 → 光标预览（HandleCursorHit 更新终点与预览线）→
 * ConfirmPoint 确认起点 → 再确认终点 → FinishMeasurement 提交结果。
 *
 * 只持有可视化 Actor 里的 Instance 索引来原地更新图元，
 * 测量值单位换算为米后通过事件抛给 UI。
 */
UCLASS(ClassGroup = (Measurement), meta = (BlueprintSpawnableComponent))
class MEASUREMENT_API UDistanceMeasureComponent : public UMeasureToolComponentBase
{
  GENERATED_BODY()

public:
  UDistanceMeasureComponent();

  virtual void ConfirmPoint() override;
  virtual void UndoLastPoint() override;
  virtual void FinishMeasurement() override;
  virtual void ResetMeasurement() override;

  /** 当前测量距离（米，只读）。 */
  UFUNCTION(BlueprintPure, Category = "Measurement")
  double GetDistanceMeters() const
  {
    return DistanceMeters;
  }

  /** 预览中实时广播（拖动鼠标时持续触发）。 */
  UPROPERTY(BlueprintAssignable, Category = "Measurement")
  FOnDistanceMeasurementChanged OnDistanceChanged;

  /** 确认提交时广播一次（UI 落定显示用）。 */
  UPROPERTY(BlueprintAssignable, Category = "Measurement")
  FOnDistanceMeasurementChanged OnDistanceCommitted;

protected:
  virtual void HandleCursorHit(const FHitResult &HitResult) override;

  /** 确认起点，进入预览状态。 */
  void StartMeasurement(const FHitResult &HitResult);

  /** 用新的终点位置更新预览（线段、标签、距离值）。 */
  void UpdateMeasurement(const FVector &NewEndPoint);

  /** 按 DecimalPlaces 格式化距离显示文本。 */
  FText MakeDistanceText() const;

protected:
  /** 显示文本保留的小数位数。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementDistance")
  int32 DecimalPlaces = 2;

  /** 标签相对线段中点的世界空间偏移（默认抬高 20cm 避免遮挡）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementDistance")
  FVector LabelWorldOffset = FVector(0.0, 0.0, 20.0);

private:
  /** 测量起点世界坐标（ConfirmPoint 时确定）。 */
  FVector StartPoint = FVector::ZeroVector;
  /** 预览/确认的终点世界坐标（随光标实时变化）。 */
  FVector EndPoint = FVector::ZeroVector;

  /** 当前距离（米）。 */
  double DistanceMeters = 0.0;

  // 可视化 Actor 中对应图元的实例索引，用于原地更新与清理
  int32 StartPointInstance = INDEX_NONE;
  int32 EndPointInstance = INDEX_NONE;
  int32 LineInstance = INDEX_NONE;
};
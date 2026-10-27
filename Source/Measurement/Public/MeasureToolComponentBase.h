#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeasurementTypes.h"
#include "MeasurementRegionSubsystem.h"
#include "MeasureToolComponentBase.generated.h"

class AMeasureVisualizationActor;
class APlayerController;
class UMeasureRegionSelectorComponent;

/**
 * 测量工具组件抽象基类（交互层）。
 *
 * 职责划分：
 * - 本类负责"通用交互"：光标射线检测（Tick 节流）、工具状态机流转、
 *   可视化 Actor 的注入与显隐；
 * - 子类（测距/测面积）只实现业务钩子：
 *   HandleCursorHit（预览更新）、ConfirmPoint（落点）、
 *   UndoLastPoint（回退）、FinishMeasurement（结束）。
 *
 * 模板方法模式：TickComponent 是模板，HandleCursorHit 是钩子。
 */
UCLASS(Abstract, ClassGroup = (Measurement))
class MEASUREMENT_API UMeasureToolComponentBase : public UActorComponent
{
  GENERATED_BODY()
public:
  UMeasureToolComponentBase();

  /** 激活工具：进入"等待第一个点"状态，并显示可视化 Actor。 */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void ActivateTool();

  /**
   * 停用工具：回到未激活状态，清空并隐藏可视化 Actor，
   * 同时关闭 Tick（不激活时不做射线检测，省开销）。
   */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void DeactivateTool();

  /** 清空本次测量数据，工具保持激活则回到"等待第一个点"。 */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void ResetMeasurement();

  /**
   * 确认落点（通常是鼠标点击）。
   * 空实现由子类覆盖：测距记录端点、测面积追加顶点。
   */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void ConfirmPoint();

  /** 撤销最后一个点，基类默认退化为整次重置。 */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void UndoLastPoint();

  /** 结束本次测量（测面积封口、测距提交），进入 Finished 状态。 */
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  virtual void FinishMeasurement();

  /** 工具是否处于激活状态。 */
  UFUNCTION(BlueprintPure, Category = "Measurement")
  bool IsToolActive() const
  {
    return bToolActive;
  }

  /** 当前状态机状态。 */
  UFUNCTION(BlueprintPure, Category = "Measurement")
  EMeasurementToolState GetToolState() const
  {
    return ToolState;
  }
  UFUNCTION(BlueprintCallable, Category = "Measurement")
  void HandleRegionChanged(UMeasureRegionSelectorComponent *OldRegion, UMeasureRegionSelectorComponent *NewRegion);

  /**
   * 注入可视化 Actor（表现层）。
   * 交互与表现解耦：本组件只持有指针，不负责创建/销毁。
   */
  void SetVisualizationActor(AMeasureVisualizationActor *InVisualization);

protected:
  virtual void BeginPlay() override;

  virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

  /**
   * 从光标位置向场景做射线检测。
   * @param OutHit 命中结果
   * @return 是否命中
   */
  bool TraceUnderCursor(FHitResult &OutHit) const;

  bool RefreshCursorHitNow();
  /**
   * 射线命中钩子（模板方法的子类扩展点）。
   * 仅在工具激活且射线命中时被 Tick 调用，子类在此做预览更新。
   */
  virtual void HandleCursorHit(const FHitResult &HitResult) {}
  // 拿到此时射线射中的结果
  const FHitResult &GetCachedHit() const
  {
    return CachedHit;
  }

  bool HasCachedHit() const
  {
    return bHasCachedHit;
  }

  AMeasureVisualizationActor *GetVisualization() const
  {
    return VisualizationActor;
  }
  UMeasureRegionSelectorComponent *GetActiveMeasureRegion() const;

protected:
  /** 光标射线使用的碰撞通道。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementTrace")
  TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

  /** 是否检测复杂碰撞（精确到三角形，开销更大但更准）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementTrace")
  bool bTraceComplex = true;
  // 预览检测频率 设置为0每帧更新
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementPerformance")
  float PreviewUpdateRate = 30.0f;

  /** 工具状态机当前状态（蓝图只读）。 */
  UPROPERTY(BlueprintReadOnly, Category = "Measurement")
  EMeasurementToolState ToolState = EMeasurementToolState::Inactive;

  /** 所属 PlayerController，BeginPlay 时缓存，用于光标射线。 */
  UPROPERTY()
  TObjectPtr<APlayerController> PlayerController;

  /** 注入的可视化 Actor，可能为空（空时工具仍工作但不显示）。 */
  UPROPERTY()
  TObjectPtr<AMeasureVisualizationActor> VisualizationActor;

  /** 最近一次射线命中的缓存，供子类随时取用。 */
  FHitResult CachedHit;

  /** CachedHit 是否有效（射线未命中时为 false）。 */
  bool bHasCachedHit = false;
  /** 工具激活标志（与状态机 Inactive 状态保持一致）。 */
  bool bToolActive = false;

private:
  /** 预览节流累加器：累计 DeltaTime，达到更新间隔才做一次射线。 */
  float PreviewUpdateAccumulator = 0.0f;
};
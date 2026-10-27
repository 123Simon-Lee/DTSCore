#include "MeasureToolComponentBase.h"

#include "MeasurementRegionSubsystem.h"
#include "MeasureRegionSelectorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PlayerController.h"
#include "MeasureVisualizationActor.h"

UMeasureToolComponentBase::UMeasureToolComponentBase()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMeasureToolComponentBase::BeginPlay()
{
  Super::BeginPlay();
  PlayerController = Cast<APlayerController>(GetOwner());

  if (UWorld *World = GetWorld())
  {
    if (UMeasurementRegionSubsystem *Subsystem = World->GetSubsystem<UMeasurementRegionSubsystem>())
    {
      Subsystem->OnActiveRegionChanged.AddUObject(
          this,
          &UMeasureToolComponentBase::HandleRegionChanged);
    }
  }
}

void UMeasureToolComponentBase::HandleRegionChanged(UMeasureRegionSelectorComponent *OldRegion, UMeasureRegionSelectorComponent *NewRegion)
{
  ResetMeasurement();

  if (!NewRegion)
  {
    bHasCachedHit = false;
  }
}

void UMeasureToolComponentBase::SetVisualizationActor(AMeasureVisualizationActor *InVisualization)
{

  VisualizationActor = InVisualization;
}

void UMeasureToolComponentBase::ActivateTool()
{
  bToolActive = true;
  // 设置状态为在等待第一个点进入
  ToolState = EMeasurementToolState::WaitingFirstPoint;
  bHasCachedHit = false;
  CachedHit = FHitResult();

  SetComponentTickEnabled(true);
  // 显示可视化 Actor（图元在子类确认落点后逐步添加）
  if (VisualizationActor)
  {
    VisualizationActor->SetActorHiddenInGame(false);
  }
}

void UMeasureToolComponentBase::DeactivateTool()
{
  bToolActive = false;
  ToolState = EMeasurementToolState::Inactive;
  bHasCachedHit = false;

  // 关闭 Tick：工具未激活时不做射线检测，避免无谓开销
  SetComponentTickEnabled(false);

  // 清空全部图元并整体隐藏，下次激活从零开始
  if (VisualizationActor)
  {
    VisualizationActor->ClearAll();
    VisualizationActor->SetActorHiddenInGame(true);
  }
}

void UMeasureToolComponentBase::ResetMeasurement()
{
  bHasCachedHit = false;
  CachedHit = FHitResult();

  // 重置后回到哪个状态取决于工具是否仍处于激活中
  ToolState = bToolActive
                  ? EMeasurementToolState::WaitingFirstPoint
                  : EMeasurementToolState::Inactive;

  if (VisualizationActor)
  {
    VisualizationActor->ClearAll();
  }

  // 工具仍激活则保持 Tick 继续检测光标
  SetComponentTickEnabled(bToolActive);
}

UMeasureRegionSelectorComponent *UMeasureToolComponentBase::GetActiveMeasureRegion() const
{
  UWorld *World = GetWorld();

  if (!World)
  {
    return nullptr;
  }

  const UMeasurementRegionSubsystem *Subsystem =
      World->GetSubsystem<
          UMeasurementRegionSubsystem>();

  return Subsystem
             ? Subsystem->GetActiveRegion()
             : nullptr;
}
void UMeasureToolComponentBase::ConfirmPoint()
{
}

void UMeasureToolComponentBase::UndoLastPoint()
{
  ResetMeasurement();
}

void UMeasureToolComponentBase::FinishMeasurement()
{
}

bool UMeasureToolComponentBase::TraceUnderCursor(FHitResult &OutHit) const
{
  if (!PlayerController || !GetWorld())
  {
    return false;
  }

  const UMeasurementRegionSubsystem *Subsystem = GetWorld()->GetSubsystem<UMeasurementRegionSubsystem>();

  UMeasureRegionSelectorComponent *Region =
      Subsystem
          ? Subsystem->GetActiveRegion()
          : nullptr;

  /*
   * 正式模式：
   * 存在测量区域时，使用该空间规定的平面。
   */
  if (IsValid(Region))
  {
    FVector2D LocalPoint;
    FVector WorldPoint;

    if (!Region->ProjectCursorToRegion(
            PlayerController,
            LocalPoint,
            WorldPoint))
    {
      return false;
    }

    const FTransform PlaneTransform =
        Region->GetPlaneWorldTransform();

    OutHit = FHitResult();
    OutHit.bBlockingHit = true;
    OutHit.Location = WorldPoint;
    OutHit.ImpactPoint = WorldPoint;

    OutHit.Normal =
        PlaneTransform
            .GetUnitAxis(EAxis::Z)
            .GetSafeNormal();

    OutHit.ImpactNormal = OutHit.Normal;

    return true;
  }

  /*
   * 临时测试模式：
   * 没有选中空间时，直接检测建筑模型。
   */
  return PlayerController->GetHitResultUnderCursorByChannel(
      UEngineTypes::ConvertToTraceType(
          TraceChannel.GetValue()),
      bTraceComplex,
      OutHit);
}

void UMeasureToolComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // 模板方法：只有激活的工具才做光标检测
  if (!bToolActive)
  {
    return;
  }
  // 频率控制：累加 DeltaTime，达到更新间隔才做一次射线检测
  if (PreviewUpdateRate > 0.0f)
  {
    PreviewUpdateAccumulator += DeltaTime;
    const float UpdateInterval = 1.0f / PreviewUpdateRate;
    if (PreviewUpdateAccumulator < UpdateInterval)
    {
      return;
    }
    // 保留余量，避免固定帧率下的节拍漂移
    PreviewUpdateAccumulator = FMath::Fmod(PreviewUpdateAccumulator, UpdateInterval);
  }
  FHitResult HitResult;
  // 射线检测：命中则缓存并交给子类钩子做预览更新
  if (TraceUnderCursor(HitResult))
  {
    CachedHit = HitResult;
    bHasCachedHit = true;

    HandleCursorHit(HitResult);
  }
  else
  {
    // 未命中只清缓存，不通知子类（预览保持上一次位置）
    bHasCachedHit = false;
  }
}

bool UMeasureToolComponentBase::RefreshCursorHitNow()
{
  FHitResult HitResult;

  if (!TraceUnderCursor(HitResult))
  {
    CachedHit = FHitResult();
    bHasCachedHit = false;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Measure] Cursor trace failed"));

    return false;
  }

  CachedHit = HitResult;
  bHasCachedHit = true;

  return true;
}
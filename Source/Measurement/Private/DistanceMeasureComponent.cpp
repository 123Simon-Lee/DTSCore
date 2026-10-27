#include "DistanceMeasureComponent.h"
#include "MeasureVisualizationActor.h"

// 构造：Tick 能力交给基类/外部按需开启，默认不逐帧检测
UDistanceMeasureComponent::UDistanceMeasureComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;
}

// 重置：清空起终点、测量值和全部可视化实例索引，回到等待起点状态
void UDistanceMeasureComponent::ResetMeasurement()
{
  StartPoint = FVector::ZeroVector;
  EndPoint = FVector::ZeroVector;
  DistanceMeters = 0.0;

  StartPointInstance = INDEX_NONE;
  EndPointInstance = INDEX_NONE;
  LineInstance = INDEX_NONE;

  Super::ResetMeasurement();
}

// 确认起点：记录命中点，创建起点/终点两个球实例和一条线段实例，进入预览状态
void UDistanceMeasureComponent::StartMeasurement(const FHitResult &HitResult)
{
  AMeasureVisualizationActor *Visual = GetVisualization();
  if (!Visual)
    return;

  StartPoint = HitResult.ImpactPoint;
  EndPoint = StartPoint;

  StartPointInstance = Visual->AddPoint(StartPoint);
  EndPointInstance = Visual->AddPoint(EndPoint);
  LineInstance = Visual->AddLine(StartPoint, EndPoint);
  ToolState = EMeasurementToolState::Previewing;
  UpdateMeasurement(EndPoint);
}

// 预览更新：用新终点刷新线段图元、重算距离并广播给 UI
void UDistanceMeasureComponent::UpdateMeasurement(const FVector &NewEndPoint)
{
  AMeasureVisualizationActor *Visual = GetVisualization();
  if (!Visual)
    return;

  EndPoint = NewEndPoint;
  // 算出起点与终点的距离
  const double DistanceCentimeters = FVector::Distance(StartPoint, EndPoint);
  // 单位转换成米
  DistanceMeters = DistanceCentimeters / 100.0;
  Visual->UpdatePoint(EndPointInstance, EndPoint);

  Visual->UpdateLine(LineInstance, StartPoint, EndPoint);

  // 标签放在线段中点上方（LabelWorldOffset 抬高避免遮挡）
  const FVector LabelLocation = (StartPoint + EndPoint) * 0.5f + LabelWorldOffset;

  // 每次预览更新都重新生成格式化文本
  const FText DisplayText = MakeDistanceText();

  Visual->SetLabel(DisplayText, LabelLocation);

  OnDistanceChanged.Broadcast(DistanceMeters, DisplayText, LabelLocation);
}

// 按 DecimalPlaces 格式化距离文本（小数位钳制到 0~6 防异常输入）
FText UDistanceMeasureComponent::MakeDistanceText() const
{
  const int32 SafeDecimals = FMath::Clamp(DecimalPlaces, 0, 6);

  return FText::FromString(FString::Printf(TEXT("%.*f m"), SafeDecimals, DistanceMeters));
}

// 射线命中钩子（基类 Tick 节流后调用）：预览状态下持续把终点吸附到光标命中点
void UDistanceMeasureComponent::HandleCursorHit(const FHitResult &HitResult)
{
  if (ToolState == EMeasurementToolState::Previewing)
  {
    UpdateMeasurement(HitResult.ImpactPoint);
  }
}

// 确认落点（鼠标点击）：按状态机流转 起点→终点提交→开启下一轮
void UDistanceMeasureComponent::ConfirmPoint()
{
  if (!IsToolActive())
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Measure] Distance tool inactive"));

    return;
  }

  // 点击时先做一次即时射线，避免依赖节流的旧缓存
  if (!RefreshCursorHitNow())
  {
    return;
  }

  // 第一次点击：确定起点，进入预览
  if (ToolState == EMeasurementToolState::WaitingFirstPoint)
  {
    StartMeasurement(GetCachedHit());
    return;
  }

  // 第二次点击：提交结果，关 Tick，广播 OnDistanceCommitted
  if (ToolState == EMeasurementToolState::Previewing)
  {
    UpdateMeasurement(GetCachedHit().ImpactPoint);

    ToolState = EMeasurementToolState::Finished;

    SetComponentTickEnabled(false);

    const FVector LabelLocation = (StartPoint + EndPoint) * 0.5 + LabelWorldOffset;

    OnDistanceCommitted.Broadcast(DistanceMeters, MakeDistanceText(), LabelLocation);
    return;
  }

  // 已完成后点击：开启新一轮测量
  if (ToolState == EMeasurementToolState::Finished)
  {
    ResetMeasurement();
    StartMeasurement(GetCachedHit());
  }
}

// 结束测量：预览中且有有效命中时等价于确认终点
void UDistanceMeasureComponent::FinishMeasurement()
{
  if (ToolState == EMeasurementToolState::Previewing && HasCachedHit())
  {
    ConfirmPoint();
  }
}
// 撤销：测距只有一条线段，直接整轮重置
void UDistanceMeasureComponent::UndoLastPoint()
{
  ResetMeasurement();
}

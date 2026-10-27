#include "AreaMeasureComponent.h"
#include "MeasureRegionSelectorComponent.h"
#include "ProceduralMeshComponent.h"
#include "MeasureVisualizationActor.h"

// 构造：Tick 能力默认关闭，预置 4 个矩形角点槽位
UAreaMeasureComponent::UAreaMeasureComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;

  RectangleCorners.SetNum(4);
}

// 确认落点（鼠标点击）：按状态机流转 起点→矩形提交→开启下一轮
void UAreaMeasureComponent::ConfirmPoint()
{
  if (!IsToolActive())
  {
    return;
  }

  // 点击时先做一次即时射线，避免依赖节流的旧缓存
  if (!RefreshCursorHitNow())
  {
    return;
  }
  // 第一次点击：确定测量平面原点，进入矩形预览
  if (ToolState == EMeasurementToolState::WaitingFirstPoint)
  {
    StartMeasurement(GetCachedHit());
    return;
  }

  // 第二次点击：提交矩形面积，关 Tick，广播 OnAreaCommitted
  if (ToolState == EMeasurementToolState::Previewing)
  {
    UpdateMeasurement(GetCachedHit().ImpactPoint);

    ToolState = EMeasurementToolState::Finished;

    SetComponentTickEnabled(false);

    // 标签放在四角点均值中心，沿法线抬高避免嵌进面片
    const FVector Center = (RectangleCorners[0] + RectangleCorners[1] + RectangleCorners[2] + RectangleCorners[3]) * 0.25;

    const FVector LabelLocation = Center + PlaneNormal * LabelNormalOffset;

    OnAreaCommitted.Broadcast(AreaSquareMeters, MakeAreaText(), LabelLocation);
    return;
  }

  // 已完成后点击：开启新一轮测量
  if (ToolState == EMeasurementToolState::Finished)
  {
    ResetMeasurement();
    StartMeasurement(GetCachedHit());
  }
}
// 撤销：矩形模式只有一轮，直接整轮重置
void UAreaMeasureComponent::UndoLastPoint()
{
  ResetMeasurement();
}
// 结束测量：预览中且有有效命中时等价于确认提交
void UAreaMeasureComponent::FinishMeasurement()
{
  if (ToolState == EMeasurementToolState::Previewing && HasCachedHit())
  {
    ConfirmPoint();
  }
}

// 重置：清空测量平面、宽高、面积、实例索引，并清除面积面可视化
void UAreaMeasureComponent::ResetMeasurement()
{
  PlaneOrigin = FVector::ZeroVector;
  PlaneNormal = FVector::UpVector;
  PlaneAxisU = FVector::ForwardVector;
  PlaneAxisV = FVector::RightVector;

  SignedWidth = 0.0;
  SignedHeight = 0.0;
  AreaSquareMeters = 0.0;

  RectangleCorners.SetNum(4);

  PointInstances.Reset();
  LineInstances.Reset();

  // 单独清除 ProceduralMesh 面积面（Super::Reset 会清点/线实例）
  if (AMeasureVisualizationActor *Visual = GetVisualization())
  {
    Visual->ClearAreaPolygon();
  }

  Super::ResetMeasurement();
}

// 射线命中钩子（基类 Tick 节流后调用）：预览状态下把矩形对角点吸附到光标命中点
void UAreaMeasureComponent::HandleCursorHit(const FHitResult &HitResult)
{
  if (ToolState == EMeasurementToolState::Previewing)
  {
    UpdateMeasurement(HitResult.ImpactPoint);
  }
}

// 确认起点：确定测量平面（优先用区域选择器的楼板平面），创建初始图元，进入预览
void UAreaMeasureComponent::StartMeasurement(const FHitResult &HitResult)
{

  AMeasureVisualizationActor *Visual = GetVisualization();

  if (!Visual)
  {
    UE_LOG(LogTemp, Error, TEXT("[Measure] Visualization is null"));

    return;
  }

  // 测量平面原点 = 首个命中点
  PlaneOrigin = HitResult.ImpactPoint;

  // 测量平面的来源优先级：区域选择器的楼板平面 > 点击面法线
  UMeasureRegionSelectorComponent *Region = GetActiveMeasureRegion();

  if (IsValid(Region))
  {
    // 有区域选择器：直接取楼板平面的世界变换轴向，保证矩形贴合楼板
    const FTransform PlaneTransform = Region->GetPlaneWorldTransform();

    PlaneAxisU =
        PlaneTransform
            .GetUnitAxis(EAxis::X)
            .GetSafeNormal();

    PlaneAxisV =
        PlaneTransform
            .GetUnitAxis(EAxis::Y)
            .GetSafeNormal();

    PlaneNormal =
        PlaneTransform
            .GetUnitAxis(EAxis::Z)
            .GetSafeNormal();
  }
  else
  {
    // 没有选择空间时，使用点击到的建筑表面
    BuildPlaneBasis(HitResult.ImpactNormal);
  }

  // 初始化四个角点全部重合在原点，并创建对应的点/线实例
  RectangleCorners.SetNum(4);

  for (int32 Index = 0; Index < 4; ++Index)
  {
    RectangleCorners[Index] = PlaneOrigin;

    PointInstances.Add(
        Visual->AddPoint(PlaneOrigin));
  }

  for (int32 Index = 0; Index < 4; ++Index)
  {
    LineInstances.Add(
        Visual->AddLine(
            PlaneOrigin,
            PlaneOrigin));
  }

  // 清掉上一轮残留的面积面
  Visual->ClearAreaPolygon();

  ToolState = EMeasurementToolState::Previewing;

  UpdateMeasurement(PlaneOrigin);

  UE_LOG(
      LogTemp,
      Warning,
      TEXT("[Measure] Area started at %s"),
      *PlaneOrigin.ToString());
}
// 预览更新：把光标点投影到测量平面，推算带符号宽高并刷新四角/边线/面积面/标签
void UAreaMeasureComponent::UpdateMeasurement(const FVector &NewEndPoint)
{
  AMeasureVisualizationActor *Visual = GetVisualization();

  if (!Visual)
  {
    return;
  }

  // 把光标点投影到测量平面内，保证矩形始终贴合首个命中面
  const FVector ProjectedPoint = ProjectToMeasurementPlane(NewEndPoint);

  const FVector Delta = ProjectedPoint - PlaneOrigin;

  SignedWidth = FVector::DotProduct(Delta, PlaneAxisU);

  SignedHeight = FVector::DotProduct(Delta, PlaneAxisV);

  RectangleCorners[0] = PlaneOrigin;

  RectangleCorners[1] = PlaneOrigin + PlaneAxisU * SignedWidth;

  RectangleCorners[2] = PlaneOrigin + PlaneAxisU * SignedWidth + PlaneAxisV * SignedHeight;

  RectangleCorners[3] = PlaneOrigin + PlaneAxisV * SignedHeight;

  for (int32 Index = 0; Index < 4; ++Index)
  {
    Visual->UpdatePoint(PointInstances[Index], RectangleCorners[Index]);

    const int32 NextIndex = (Index + 1) % 4;

    Visual->UpdateLine(LineInstances[Index], RectangleCorners[Index], RectangleCorners[NextIndex]);
  }

  // 宽高都超过 1cm 才绘制面积面，避免刚点击时出现极小的闪烁面片
  constexpr double MinimumRectangleSize = 1.0;

  if (FMath::Abs(SignedWidth) >= MinimumRectangleSize && FMath::Abs(SignedHeight) >= MinimumRectangleSize)
  {
    // 首次调用创建网格，之后只原地更新四个顶点（避免每次重建渲染状态）
    Visual->SetAreaRectangle(RectangleCorners, PlaneAxisU, PlaneNormal);
  }
  else
  {
    Visual->ClearAreaPolygon();
  }

  const double AreaSquareCentimeters = FMath::Abs(SignedWidth * SignedHeight);

  AreaSquareMeters = AreaSquareCentimeters / 10000.0;

  const FVector Center = (RectangleCorners[0] + RectangleCorners[1] + RectangleCorners[2] + RectangleCorners[3]) * 0.25;

  const FVector LabelLocation = Center + PlaneNormal * LabelNormalOffset;

  const FText DisplayText = MakeAreaText();

  Visual->SetLabel(DisplayText, LabelLocation);

  OnAreaChanged.Broadcast(AreaSquareMeters, DisplayText, LabelLocation);
}

// 由命中面法线建立测量平面坐标基：法线兜底为向上，U 取前向量在面内的投影，V = N × U
void UAreaMeasureComponent::BuildPlaneBasis(const FVector &InNormal)
{
  PlaneNormal = InNormal.GetSafeNormal();

  if (PlaneNormal.IsNearlyZero())
  {
    PlaneNormal = FVector::UpVector;
  }

  PlaneAxisU =
      FVector::VectorPlaneProject(
          FVector::ForwardVector,
          PlaneNormal)
          .GetSafeNormal();

  if (PlaneAxisU.IsNearlyZero())
  {
    PlaneAxisU =
        FVector::VectorPlaneProject(
            FVector::RightVector,
            PlaneNormal)
            .GetSafeNormal();
  }

  PlaneAxisV =
      FVector::CrossProduct(
          PlaneNormal,
          PlaneAxisU)
          .GetSafeNormal();
}

// 点到平面的正交投影：减去沿法线方向的偏移分量（点到平面的有向距离）
FVector UAreaMeasureComponent::ProjectToMeasurementPlane(const FVector &WorldPoint) const
{
  const FVector ToPoint = WorldPoint - PlaneOrigin;

  const double PlaneDistance = FVector::DotProduct(ToPoint, PlaneNormal);

  return WorldPoint - PlaneNormal * PlaneDistance;
}

// 按 DecimalPlaces 格式化面积文本（小数位钳制到 0~6）
FText UAreaMeasureComponent::MakeAreaText() const
{
  const int32 SafeDecimals =
      FMath::Clamp(DecimalPlaces, 0, 6);

  return FText::FromString(
      FString::Printf(
          TEXT("%.*f m\u00B2"),
          SafeDecimals,
          AreaSquareMeters));
}

// 追加已确认顶点：刷新点/边线实例；达到 3 个顶点后开始实时重建面积多边形
void UAreaMeasureComponent::AddConfirmedPoint(
    const FVector &WorldPoint)
{
  ConfirmedWorldPoints.Add(WorldPoint);

  RefreshPointsAndLines();

  // 三个点以后可以实时预览区域
  if (ConfirmedWorldPoints.Num() >= 3)
  {
    RefreshAreaMesh();
  }
}

void UAreaMeasureComponent::RefreshPointsAndLines()
{
  AMeasureVisualizationActor *Visual = GetVisualization();

  if (!Visual)
  {
    return;
  }

  /*
   * 顶点数量动态变化，采用"清空 + 重建"策略：
   * 相比逐个更新实例更简单可靠，测量点数量级很小，开销可忽略。
   */
  PointInstances.Reset();
  LineInstances.Reset();

  for (const FVector &Point : ConfirmedWorldPoints)
  {
    PointInstances.Add(Visual->AddPoint(Point));
  }

  // 闭合连线：最后一个点连回第一个点
  for (int32 Index = 0; Index < ConfirmedWorldPoints.Num(); ++Index)
  {
    const int32 NextIndex = (Index + 1) % ConfirmedWorldPoints.Num();

    LineInstances.Add(Visual->AddLine(
        ConfirmedWorldPoints[Index],
        ConfirmedWorldPoints[NextIndex]));
  }
}

// 世界坐标 → 平面 2D：分别对 (点-原点) 与两条轴做点积，得到平面内坐标
FVector2D UAreaMeasureComponent::ProjectPointToPlane2D(
    const FVector &WorldPoint,
    const FVector &InPlaneOrigin,
    const FVector &InPlaneAxisX,
    const FVector &InPlaneAxisY)
{
  const FVector Offset = WorldPoint - InPlaneOrigin;

  return FVector2D(
      FVector::DotProduct(Offset, InPlaneAxisX),
      FVector::DotProduct(Offset, InPlaneAxisY));
}

// 平面 2D → 世界坐标：原点 + X 分量沿 AxisX + Y 分量沿 AxisY
FVector UAreaMeasureComponent::Plane2DToWorld(
    const FVector2D &Point,
    const FVector &InPlaneOrigin,
    const FVector &InPlaneAxisX,
    const FVector &InPlaneAxisY)
{
  return InPlaneOrigin + InPlaneAxisX * Point.X + InPlaneAxisY * Point.Y;
}
// 鞋带公式计算多边形面积：把顶点投影到平面 2D 后按有向面积累加，取绝对值（单位 cm²）
double UAreaMeasureComponent::CalculatePolygonAreaCm2(
    const TArray<FVector> &WorldPoints,
    const FVector &InPlaneOrigin,
    const FVector &InPlaneAxisX,
    const FVector &InPlaneAxisY)
{
  if (WorldPoints.Num() < 3)
  {
    return 0.0;
  }

  double TwiceSignedArea = 0.0;

  for (int32 Index = 0; Index < WorldPoints.Num(); ++Index)
  {
    const int32 NextIndex = (Index + 1) % WorldPoints.Num();

    const FVector2D A = ProjectPointToPlane2D(
        WorldPoints[Index],
        InPlaneOrigin,
        InPlaneAxisX,
        InPlaneAxisY);

    const FVector2D B = ProjectPointToPlane2D(
        WorldPoints[NextIndex],
        InPlaneOrigin,
        InPlaneAxisX,
        InPlaneAxisY);

    TwiceSignedArea +=
        static_cast<double>(A.X) * static_cast<double>(B.Y) -
        static_cast<double>(B.X) * static_cast<double>(A.Y);
  }

  return FMath::Abs(TwiceSignedArea) * 0.5;
}

// 重建面积多边形：提交顶点给 ProceduralMesh 三角化渲染，并重算面积、更新标签
void UAreaMeasureComponent::RefreshAreaMesh()
{
  if (!VisualizationActor)
  {
    return;
  }

  VisualizationActor->UpdateAreaPolygon(
      ConfirmedWorldPoints,
      PlaneOrigin,
      PlaneAxisU,
      PlaneAxisV,
      PlaneNormal);

  const double AreaCm2 = CalculatePolygonAreaCm2(
      ConfirmedWorldPoints,
      PlaneOrigin,
      PlaneAxisU,
      PlaneAxisV);

  const double AreaM2 = AreaCm2 / 10000.0;

  const FString Label = FString::Printf(
      TEXT("%.2f m²"),
      AreaM2);

  VisualizationActor->UpdateAreaLabel(
      GetPolygonLabelPosition(),
      Label,
      PlaneNormal);
}

// 标签位置 = 顶点均值中心（质心近似），沿法线抬高 10cm 浮于面片上方
FVector UAreaMeasureComponent::GetPolygonLabelPosition() const
{
  if (ConfirmedWorldPoints.IsEmpty())
  {
    return PlaneOrigin;
  }

  FVector Center = FVector::ZeroVector;

  for (const FVector &Point : ConfirmedWorldPoints)
  {
    Center += Point;
  }

  Center /= ConfirmedWorldPoints.Num();
  Center += PlaneNormal * 10.0f;

  return Center;
}
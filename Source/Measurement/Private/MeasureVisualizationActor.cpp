#include "MeasureVisualizationActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

/*
 * 面积多边形的平面几何辅助函数
 */
namespace
{
  FVector2D ProjectToPlane2D(
      const FVector &WorldPoint,
      const FVector &InOrigin,
      const FVector &InAxisX,
      const FVector &InAxisY)
  {
    const FVector Offset = WorldPoint - InOrigin;

    return FVector2D(
        FVector::DotProduct(Offset, InAxisX),
        FVector::DotProduct(Offset, InAxisY));
  }

  FVector PlaneToWorld(
      const FVector2D &Point,
      const FVector &InOrigin,
      const FVector &InAxisX,
      const FVector &InAxisY)
  {
    return InOrigin + InAxisX * Point.X + InAxisY * Point.Y;
  }

  double Cross2D(const FVector2D &A, const FVector2D &B)
  {
    return static_cast<double>(A.X) * static_cast<double>(B.Y) -
           static_cast<double>(A.Y) * static_cast<double>(B.X);
  }

  double Cross2D(
      const FVector2D &A,
      const FVector2D &B,
      const FVector2D &C)
  {
    return Cross2D(FVector2D(B.X - A.X, B.Y - A.Y),
                   FVector2D(C.X - A.X, C.Y - A.Y));
  }

  double SignedPolygonArea(const TArray<FVector2D> &Points)
  {
    double TwiceArea = 0.0;

    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
      const FVector2D &A = Points[Index];
      const FVector2D &B = Points[(Index + 1) % Points.Num()];

      TwiceArea +=
          static_cast<double>(A.X) * static_cast<double>(B.Y) -
          static_cast<double>(B.X) * static_cast<double>(A.Y);
    }

    return TwiceArea * 0.5;
  }

  bool IsPointInsideTriangle(const FVector2D &Point, const FVector2D &A, const FVector2D &B, const FVector2D &C)
  {
    constexpr double Epsilon = 0.001;

    const double CrossAB = Cross2D(A, B, Point); // （B-A）*(P-A)的z分量
    const double CrossBC = Cross2D(B, C, Point); // （C-B）*(P-B)的z分量
    const double CrossCA = Cross2D(C, A, Point); // （A-C）*(P-C)的z分量

    const bool bHasNegative =
        CrossAB < -Epsilon || CrossBC < -Epsilon || CrossCA < -Epsilon;

    const bool bHasPositive =
        CrossAB > Epsilon || CrossBC > Epsilon || CrossCA > Epsilon;

    return !(bHasNegative && bHasPositive);
  }

  // 耳切三角化（按逆时针处理，支持凹多边形）
  bool TriangulateSimplePolygon(const TArray<FVector2D> &Polygon, TArray<int32> &OutTriangles)
  {
    OutTriangles.Reset();

    if (Polygon.Num() < 3)
    {
      return false;
    }

    TArray<int32> RemainingIndices;
    RemainingIndices.Reserve(Polygon.Num());

    if (SignedPolygonArea(Polygon) >= 0.0)
    {
      for (int32 Index = 0; Index < Polygon.Num(); ++Index)
      {
        RemainingIndices.Add(Index);
      }
    }
    else
    {
      for (int32 Index = Polygon.Num() - 1; Index >= 0; --Index)
      {
        RemainingIndices.Add(Index);
      }
    }

    constexpr double ConvexEpsilon = 0.001;
    int32 SafetyCounter = 0;
    const int32 MaximumIterations = Polygon.Num() * Polygon.Num();

    while (RemainingIndices.Num() > 3 && SafetyCounter++ < MaximumIterations)
    {
      bool bFoundEar = false;

      for (int32 LocalIndex = 0; LocalIndex < RemainingIndices.Num(); ++LocalIndex)
      {
        const int32 PreviousLocal =
            (LocalIndex - 1 + RemainingIndices.Num()) % RemainingIndices.Num();

        const int32 NextLocal = (LocalIndex + 1) % RemainingIndices.Num();

        const int32 PreviousIndex = RemainingIndices[PreviousLocal];
        const int32 CurrentIndex = RemainingIndices[LocalIndex];
        const int32 NextIndex = RemainingIndices[NextLocal];

        const FVector2D &A = Polygon[PreviousIndex];
        const FVector2D &B = Polygon[CurrentIndex];
        const FVector2D &C = Polygon[NextIndex];

        // 逆时针凸角
        if (Cross2D(A, B, C) <= ConvexEpsilon)
        {
          continue;
        }

        bool bContainsAnotherPoint = false;

        for (const int32 TestIndex : RemainingIndices)
        {
          if (TestIndex == PreviousIndex ||
              TestIndex == CurrentIndex ||
              TestIndex == NextIndex)
          {
            continue;
          }

          if (IsPointInsideTriangle(Polygon[TestIndex], A, B, C))
          {
            bContainsAnotherPoint = true;
            break;
          }
        }

        if (bContainsAnotherPoint)
        {
          continue;
        }

        OutTriangles.Add(PreviousIndex);
        OutTriangles.Add(CurrentIndex);
        OutTriangles.Add(NextIndex);

        RemainingIndices.RemoveAt(LocalIndex);
        bFoundEar = true;
        break;
      }

      if (!bFoundEar)
      {
        // 点重复、边自交或大量共线时无法三角化
        OutTriangles.Reset();
        return false;
      }
    }

    if (RemainingIndices.Num() == 3)
    {
      OutTriangles.Add(RemainingIndices[0]);
      OutTriangles.Add(RemainingIndices[1]);
      OutTriangles.Add(RemainingIndices[2]);
    }

    return OutTriangles.Num() >= 3;
  }
}

AMeasureVisualizationActor::AMeasureVisualizationActor()
{
  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.bStartWithTickEnabled = false;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  SetRootComponent(SceneRoot);

  PointRenderer = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("PointRenderer"));

  PointRenderer->SetupAttachment(SceneRoot);

  LineRenderer = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LineRenderer"));
  LineRenderer->SetupAttachment(SceneRoot);

  AreaRenderer = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("AreaRenderer"));
  AreaRenderer->SetupAttachment(SceneRoot);

  TextRenderer = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TextRenderer"));
  TextRenderer->SetupAttachment(SceneRoot);

  // 统一配置图元渲染组件：测量辅助图元不参与碰撞/阴影/导航，
  // 纯视觉叠加，最大程度降低对场景的影响
  auto ConfigurePrimitive = [](UPrimitiveComponent *Component)
  {
    if (!Component)
    {
      return;
    }

    Component->SetMobility(EComponentMobility::Movable);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->SetCastShadow(false);
    Component->SetCanEverAffectNavigation(false);
    Component->SetReceivesDecals(false);
  };

  ConfigurePrimitive(PointRenderer);
  ConfigurePrimitive(LineRenderer);
  ConfigurePrimitive(AreaRenderer);
  ConfigurePrimitive(TextRenderer);

  AreaRenderer->SetTranslucentSortPriority(10);

  TextRenderer->SetHorizontalAlignment(EHTA_Center);
  TextRenderer->SetVerticalAlignment(EVRTA_TextCenter);
  TextRenderer->SetWorldSize(TextWorldSize);
  LastAppliedTextWorldSize = TextWorldSize;
  TextRenderer->SetTextRenderColor(TextColor);
  TextRenderer->SetVisibility(false);

  // 半透明渲染顺序拉到最高：配合 Disable Depth Test 材质保证标签压在所有半透明物之上
  TextRenderer->SetTranslucentSortPriority(100);

  static ConstructorHelpers::FObjectFinder<UStaticMesh>
      SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

  static ConstructorHelpers::FObjectFinder<UStaticMesh>
      CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

  static ConstructorHelpers::FObjectFinder<UStaticMesh>
      PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));

  static ConstructorHelpers::FObjectFinder<UMaterialInterface>
      BasicMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial."
                               "BasicShapeMaterial"));

  PointMesh = SphereFinder.Object;
  LineMesh = CylinderFinder.Object;

  /*
   * 不规则面积多边形用 ProceduralMesh 动态建模，
   * 注意这是独立组件，别和 AreaRenderer（实例化 Plane）/
   * AreaMesh（Plane 网格资产）混淆。
   */
  AreaPolygonRenderer = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("AreaPolygonRenderer"));

  AreaPolygonRenderer->SetupAttachment(SceneRoot);

  AreaPolygonRenderer->bUseAsyncCooking = true;

  PointMaterial = BasicMaterialFinder.Object;
  LineMaterial = BasicMaterialFinder.Object;
  AreaMaterial = BasicMaterialFinder.Object;

  ConfigurePrimitive(AreaPolygonRenderer);

  ApplyAssets();
}

// 开局重置一次：应用资产配置并清空所有图元实例
void AMeasureVisualizationActor::BeginPlay()
{
  Super::BeginPlay();

  ApplyAssets();
  ClearAll();
}

// 编辑器中属性变化时重新应用资产，保证所见即所得
void AMeasureVisualizationActor::OnConstruction(const FTransform &Transform)
{
  Super::OnConstruction(Transform);
  ApplyAssets();
}

void AMeasureVisualizationActor::ApplyAssets()
{
  if (PointRenderer)
  {
    PointRenderer->SetStaticMesh(PointMesh);

    if (PointMaterial)
    {
      PointRenderer->SetMaterial(0, PointMaterial);
    }
  }

  if (LineRenderer)
  {
    LineRenderer->SetStaticMesh(LineMesh);

    if (LineMaterial)
    {
      LineRenderer->SetMaterial(0, LineMaterial);
    }
  }

  if (AreaRenderer)
  {
    AreaRenderer->SetStaticMesh(AreaMesh);

    if (AreaMaterial)
    {
      AreaRenderer->SetMaterial(0, AreaMaterial);
    }
  }

  if (AreaPolygonRenderer && AreaMaterial)
  {
    AreaPolygonRenderer->SetMaterial(0, AreaMaterial);
  }

  if (TextRenderer)
  {
    TextRenderer->SetWorldSize(TextWorldSize);
    LastAppliedTextWorldSize = TextWorldSize;
    TextRenderer->SetTextRenderColor(TextColor);

    // 指定了自定义标签材质（Disable Depth Test）时应用，实现标签不被遮挡
    if (LabelMaterial)
    {
      TextRenderer->SetMaterial(0, LabelMaterial);
    }
  }
}

void AMeasureVisualizationActor::Tick(float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);

  // 标签不可见时自关 Tick，避免空转（SetLabel 时会重新开启）
  if (!TextRenderer || !TextRenderer->IsVisible())
  {
    SetActorTickEnabled(false);
    return;
  }

  UWorld *World = GetWorld();
  APlayerController *PC = World ? World->GetFirstPlayerController() : nullptr;

  if (!PC)
  {
    return;
  }

  FVector CameraLocation;
  FRotator CameraRotation;
  PC->GetPlayerViewPoint(CameraLocation, CameraRotation);

  // Billboard：标签每帧朝向相机，保证任意角度可读
  const FVector Direction = (CameraLocation - TextRenderer->GetComponentLocation()).GetSafeNormal();
  //
  if (!Direction.IsNearlyZero())
  {
    const FRotator FacingRotation = Direction.Rotation() + TextFacingOffset;

    TextRenderer->SetWorldRotation(FacingRotation);
  }

  /*
   * 动态字号：WorldSize = 相机距离 × 系数，让标签在屏幕上的
   * 视觉占比近似恒定，远处不缩成蚂蚁字。
   *
   * SetWorldSize 会重建文字网格，所以把距离按 50cm 一档量化，
   * 相机小幅移动不触发重建；变化后值不变也跳过设置。
   */
  const float Distance = FVector::Dist(CameraLocation, TextRenderer->GetComponentLocation());

  const float QuantizedDistance = FMath::GridSnap(Distance, 50.0f);

  const float WorldSize = FMath::Clamp(QuantizedDistance * LabelDistanceScale, LabelMinWorldSize, LabelMaxWorldSize);

  if (!FMath::IsNearlyEqual(WorldSize, LastAppliedTextWorldSize))
  {
    TextRenderer->SetWorldSize(WorldSize);

    LastAppliedTextWorldSize = WorldSize;
  }
}

FTransform AMeasureVisualizationActor::MakePointTransform(const FVector &Position) const
{
  const float SafeSourceDiameter = FMath::Max(PointSourceDiameter, 0.01f);

  const float Scale = FMath::Max(PointDiameter / SafeSourceDiameter, 0.001f);

  return FTransform(
      FQuat::Identity,
      Position,
      FVector(Scale));
}

FTransform AMeasureVisualizationActor::MakeLineTransform(const FVector &Start, const FVector &End) const
{
  const FVector Delta = End - Start;
  const double Length = Delta.Length();

  // 两端重合时退化为极小图元，避免零缩放/除零
  if (Length <= UE_KINDA_SMALL_NUMBER)
  {
    return FTransform(
        FQuat::Identity,
        Start,
        FVector(0.001f));
  }

  const FVector Direction = Delta / Length;
  const FVector MidPoint = (Start + End) * 0.5;

  // 源网格是沿 Z 轴的圆柱：把 Z 轴旋转到线段方向，再按实际长度缩放
  const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);

  const float SafeSourceDiameter = FMath::Max(LineSourceDiameter, 0.01f);

  const float SafeSourceLength = FMath::Max(LineSourceLength, 0.01f);

  const FVector Scale(
      FMath::Max(LineDiameter / SafeSourceDiameter, 0.001f),
      FMath::Max(LineDiameter / SafeSourceDiameter, 0.001f),
      FMath::Max(
          static_cast<float>(Length) / SafeSourceLength,
          0.001f));

  return FTransform(Rotation, MidPoint, Scale);
}

FTransform AMeasureVisualizationActor::MakeRectangleTransform(const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight) const
{
  const FVector U = AxisU.GetSafeNormal();
  const FVector V = AxisV.GetSafeNormal();
  // 面法线由两轴叉乘得出，用于整体抬升 AreaSurfaceOffset 防 Z-Fighting
  const FVector Normal = FVector::CrossProduct(U, V).GetSafeNormal();

  // 矩形中心 = 角点沿两轴各移半宽高，再沿法线抬升离开被测表面
  const FVector Center = Origin + U * (SignedWidth * 0.5) + V * (SignedHeight * 0.5) + Normal * AreaSurfaceOffset;

  // 用 XY 平面对齐两轴构造朝向，保证带符号宽高沿正确方向延伸
  const FQuat Rotation = FRotationMatrix::MakeFromXY(U, V).ToQuat();

  const float SafeSourceSize = FMath::Max(AreaSourceSize, 0.01f);

  const FVector Scale(FMath::Max(static_cast<float>(FMath::Abs(SignedWidth)) / SafeSourceSize, 0.001f),
                      FMath::Max(
                          static_cast<float>(FMath::Abs(SignedHeight)) /
                              SafeSourceSize,
                          0.001f),
                      1.0f);

  return FTransform(Rotation, Center, Scale);
}

// 添加测量点实例：UpdateInstanceTransform 的最后一个 true 立即提交到渲染线程
int32 AMeasureVisualizationActor::AddPoint(const FVector &WorldPosition)
{
  return PointRenderer
             ? PointRenderer->AddInstance(
                   MakePointTransform(WorldPosition),
                   true)
             : INDEX_NONE;
}

// 原地更新点实例：只改实例缓冲，不重建几何
bool AMeasureVisualizationActor::UpdatePoint(int32 InstanceIndex, const FVector &WorldPosition)
{
  return PointRenderer && InstanceIndex != INDEX_NONE && PointRenderer->UpdateInstanceTransform(InstanceIndex, MakePointTransform(WorldPosition), true, true, true);
}

// 添加线段实例（圆柱按两端点定向/定长缩放）
int32 AMeasureVisualizationActor::AddLine(const FVector &Start, const FVector &End)
{
  return LineRenderer ? LineRenderer->AddInstance(MakeLineTransform(Start, End), true) : INDEX_NONE;
}

// 原地更新线段实例
bool AMeasureVisualizationActor::UpdateLine(int32 InstanceIndex, const FVector &Start, const FVector &End)
{
  return LineRenderer && InstanceIndex != INDEX_NONE && LineRenderer->UpdateInstanceTransform(InstanceIndex, MakeLineTransform(Start, End), true, true, true);
}

// 添加矩形面实例（实例化 Plane）
int32 AMeasureVisualizationActor::AddRectangle(const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight)
{
  return AreaRenderer ? AreaRenderer->AddInstance(MakeRectangleTransform(Origin, AxisU, AxisV, SignedWidth, SignedHeight), true) : INDEX_NONE;
}

// 原地更新矩形面实例
bool AMeasureVisualizationActor::UpdateRectangle(int32 InstanceIndex, const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight)
{
  return AreaRenderer && InstanceIndex != INDEX_NONE && AreaRenderer->UpdateInstanceTransform(InstanceIndex, MakeRectangleTransform(Origin, AxisU, AxisV, SignedWidth, SignedHeight), true, true, true);
}

// 设置标签：写入文本/位置，显示并开启 Tick（供 Billboard 朝向相机）
void AMeasureVisualizationActor::SetLabel(const FText &Text, const FVector &WorldPosition)
{
  if (!TextRenderer)
  {
    return;
  }

  TextRenderer->SetText(Text);
  TextRenderer->SetWorldLocation(WorldPosition);
  TextRenderer->SetVisibility(true);
  TextRenderer->SetHiddenInGame(false);

  SetActorTickEnabled(true);
}

// 隐藏标签并关闭 Tick（无标签时不做 Billboard 更新）
void AMeasureVisualizationActor::HideLabel()
{
  if (TextRenderer)
  {
    TextRenderer->SetVisibility(false);
    TextRenderer->SetHiddenInGame(true);
  }

  SetActorTickEnabled(false);
}

// 清除所有图元：三类 ISM 实例 + ProceduralMesh 面片 + 标签
void AMeasureVisualizationActor::ClearAll()
{
  if (PointRenderer)
  {
    PointRenderer->ClearInstances();
  }

  if (LineRenderer)
  {
    LineRenderer->ClearInstances();
  }

  if (AreaRenderer)
  {
    AreaRenderer->ClearInstances();
  }

  ClearAreaPolygon();

  HideLabel();
}

/**
 * 更新不规则面积多边形面片。
 *
 * 流程：世界顶点 → 投影到测量平面 2D → 耳切三角化 →
 * 反算回世界坐标（沿法线抬高防 Z-Fighting）→ 转组件局部空间 →
 * 提交 CreateMeshSection。
 * 少于 3 个顶点或三角化失败时清空面片。
 */
bool AMeasureVisualizationActor::UpdateAreaPolygon(
    const TArray<FVector> &WorldPoints,
    const FVector &PlaneOrigin,
    const FVector &PlaneAxisX,
    const FVector &PlaneAxisY,
    const FVector &PlaneNormal)
{
  if (!AreaPolygonRenderer || WorldPoints.Num() < 3)
  {
    if (AreaPolygonRenderer)
    {
      // 清理所有绘制的ProceduralMeshComponent
      AreaPolygonRenderer->ClearAllMeshSections();
    }

    return false;
  }

  TArray<FVector2D> Polygon2D;
  Polygon2D.Reserve(WorldPoints.Num());

  for (const FVector &WorldPoint : WorldPoints)
  {
    Polygon2D.Add(ProjectToPlane2D(WorldPoint, PlaneOrigin, PlaneAxisX, PlaneAxisY));
  }

  TArray<int32> Triangles;

  if (!TriangulateSimplePolygon(Polygon2D, Triangles))
  {
    AreaPolygonRenderer->ClearAllMeshSections();
    return false;
  }

  TArray<FVector> Vertices;
  TArray<FVector> Normals;
  TArray<FVector2D> UV0;
  TArray<FColor> VertexColors;
  TArray<FProcMeshTangent> Tangents;

  Vertices.Reserve(Polygon2D.Num());
  Normals.Reserve(Polygon2D.Num());
  UV0.Reserve(Polygon2D.Num());
  VertexColors.Reserve(Polygon2D.Num());
  Tangents.Reserve(Polygon2D.Num());

  constexpr float SurfaceOffset = 2.0f;

  for (const FVector2D &Point2D : Polygon2D)
  {
    FVector WorldPosition = PlaneToWorld(Point2D, PlaneOrigin, PlaneAxisX, PlaneAxisY);

    // 防止和楼板发生 Z-Fighting
    WorldPosition += PlaneNormal * SurfaceOffset;

    const FVector LocalPosition = GetActorTransform().InverseTransformPosition(WorldPosition);

    const FVector LocalNormal = GetActorTransform().InverseTransformVectorNoScale(PlaneNormal).GetSafeNormal();

    Vertices.Add(LocalPosition);
    Normals.Add(LocalNormal);

    // 可根据区域大小归一化，这里只是基础 UV
    UV0.Add(Point2D * 0.001f);

    VertexColors.Add(FColor(50, 230, 190, 80));

    const FVector LocalTangent = GetActorTransform().InverseTransformVectorNoScale(PlaneAxisX).GetSafeNormal();

    Tangents.Add(FProcMeshTangent(LocalTangent, false));
  }

  AreaPolygonRenderer->ClearAllMeshSections();

  AreaPolygonRenderer->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, VertexColors, Tangents, false);

  AreaPolygonRenderer->SetVisibility(true);
  return true;
}

void AMeasureVisualizationActor::UpdateAreaLabel(const FVector &LabelPosition, const FString &LabelText, const FVector &PlaneNormal)
{
  // 标签沿平面法线抬高一点，避免文字嵌进面片里
  SetLabel(FText::FromString(LabelText), LabelPosition + PlaneNormal * AreaSurfaceOffset);
}

// 创建矩形面积面：四角点沿法线抬高后转局部空间，固定两个三角形 (0,1,2)(0,2,3)
void AMeasureVisualizationActor::CreateAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneAxisX, const FVector &PlaneNormal)
{
  if (!AreaPolygonRenderer || WorldPoints.Num() != 4)
  {
    return;
  }

  const FVector SafeNormal = PlaneNormal.IsNearlyZero() ? FVector::UpVector : PlaneNormal.GetSafeNormal();
  // 填充四个点
  TArray<FVector> Vertices;
  Vertices.Reserve(4);

  for (const FVector &WorldPoint : WorldPoints)
  {
    const FVector OffsetPoint = WorldPoint + SafeNormal * AreaSurfaceOffset;

    Vertices.Add(GetActorTransform().InverseTransformPosition(OffsetPoint));
  }

  const TArray<int32> Triangles = {0, 1, 2, 0, 2, 3};

  TArray<FVector> Normals;
  Normals.Init(GetActorTransform().InverseTransformVectorNoScale(SafeNormal).GetSafeNormal(), 4);

  const TArray<FVector2D> UV0 = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};

  TArray<FColor> Colors;
  Colors.Init(FColor(40, 210, 255, 70), 4);

  const FVector LocalTangent = GetActorTransform().InverseTransformVectorNoScale(PlaneAxisX).GetSafeNormal();

  TArray<FProcMeshTangent> Tangents;
  Tangents.Init(FProcMeshTangent(LocalTangent, false), 4);
  // 段索引、顶点位置、三角形索引、每顶点法线、每顶点UV、每顶点颜色、每顶点切线、是否生成碰撞体
  AreaPolygonRenderer->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, Colors, Tangents, false);

  AreaPolygonRenderer->SetVisibility(true);
}

// 预览时原地更新矩形四顶点：UpdateMeshSection 只重传顶点数据，不重建渲染状态
void AMeasureVisualizationActor::UpdateAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneNormal)
{
  if (!AreaPolygonRenderer || WorldPoints.Num() != 4)
  {
    return;
  }

  const FVector SafeNormal = PlaneNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);

  TArray<FVector> Vertices;
  Vertices.Reserve(4);

  for (const FVector &WorldPoint : WorldPoints)
  {
    const FVector OffsetPoint = WorldPoint + SafeNormal * AreaSurfaceOffset;

    Vertices.Add(GetActorTransform().InverseTransformPosition(OffsetPoint));
  }

  TArray<FVector> Normals;
  Normals.Init(GetActorTransform().InverseTransformVectorNoScale(SafeNormal).GetSafeNormal(), 4);

  TArray<FVector2D> UV0 = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1)};

  TArray<FColor> Colors;
  Colors.Init(FColor(40, 210, 255, 70), 4);

  TArray<FProcMeshTangent> Tangents;

  AreaPolygonRenderer->UpdateMeshSection(0, Vertices, Normals, UV0, Colors, Tangents);
}

// 矩形面积面统一入口：首次 Create 建网格，预览期间走 Update 只刷顶点
void AMeasureVisualizationActor::SetAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneAxisX, const FVector &PlaneNormal)
{
  if (WorldPoints.Num() != 4)
  {
    return;
  }

  if (!bAreaRectangleCreated)
  {
    CreateAreaRectangle(WorldPoints, PlaneAxisX, PlaneNormal);

    bAreaRectangleCreated = true;
  }
  else
  {
    UpdateAreaRectangle(WorldPoints, PlaneNormal);
  }
}

// 清除矩形面积面并复位"已创建"标志，下轮测量重新走 Create 路径
void AMeasureVisualizationActor::ClearAreaPolygon()
{
  if (AreaPolygonRenderer)
  {
    AreaPolygonRenderer->ClearAllMeshSections();
    AreaPolygonRenderer->SetVisibility(false);
  }

  bAreaRectangleCreated = false;
}
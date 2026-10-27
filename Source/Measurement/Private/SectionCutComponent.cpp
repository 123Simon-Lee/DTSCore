#include "SectionCutComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSectionCut, Log, All);

// 构造：Tick 按需开启——只在有切割请求/队列未消化完时才逐帧跑
USectionCutComponent::USectionCutComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;
}

// 结束时清理动态组件并释放源网格缓存
void USectionCutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  ClearTarget();
  ClearMeshCache();
  Super::EndPlay(EndPlayReason);
}
bool USectionCutComponent::IsKeepingPositiveSide() const
{
  return KeepSide == ESectionCutKeepSide::PositiveSide;
}
// 设置剖切目标：先清旧目标，再收集模型、计算投影范围，滑块归零
void USectionCutComponent::SetTargetBuilding(AActor *NewTargetBuilding)
{
  if (TargetBuilding == NewTargetBuilding)
  {
    return;
  }

  ClearTarget();

  TargetBuilding = NewTargetBuilding;

  if (!IsValid(TargetBuilding))
  {
    return;
  }

  CollectTargetComponents();
  CalculateProjectionRange();

  CutAmount = 0.0f;
  UpdatePlaneOrigin();
}

// 收集目标建筑（含挂载子 Actor）下所有有效 StaticMeshComponent，建立运行时条目
void USectionCutComponent::CollectTargetComponents()
{
  RuntimeItems.Reset();

  if (!IsValid(TargetBuilding))
  {
    return;
  }

  TSet<AActor *> VisitedActors;
  TArray<UStaticMeshComponent *> Components;

  CollectStaticMeshComponentsRecursive(TargetBuilding, VisitedActors, Components);

  RuntimeItems.Reserve(Components.Num());

  for (UStaticMeshComponent *Component : Components)
  {
    if (!IsValid(Component) || !IsValid(Component->GetStaticMesh()))
    {
      continue;
    }

    FSectionCutRuntimeItem Item;
    Item.SourceComponent = Component;
    Item.bOriginalVisible = Component->GetVisibleFlag();
    Item.bOriginalHiddenInGame = Component->bHiddenInGame;

    Item.bParticipates = Item.bOriginalVisible && !Item.bOriginalHiddenInGame;

    RuntimeItems.Add(MoveTemp(Item));
  }
}

// 递归收集：先取本 Actor 的组件，再深入挂载子 Actor（VisitedActors 防止循环挂载）
void USectionCutComponent::CollectStaticMeshComponentsRecursive(AActor *Actor, TSet<AActor *> &VisitedActors, TArray<UStaticMeshComponent *> &OutComponents)
{
  if (!IsValid(Actor) || VisitedActors.Contains(Actor))
  {
    return;
  }
  VisitedActors.Add(Actor);

  TInlineComponentArray<UStaticMeshComponent *> Components;

  Actor->GetComponents(Components);

  for (UStaticMeshComponent *Component : Components)
  {
    if (IsValid(Component) && IsValid(Component->GetStaticMesh()))
    {
      OutComponents.AddUnique(Component);
    }
  }

  TArray<AActor *> AttachedActors;
  Actor->GetAttachedActors(AttachedActors, true, false);

  for (AActor *ChildActor : AttachedActors)
  {
    CollectStaticMeshComponentsRecursive(ChildActor, VisitedActors, OutComponents);
  }
}

// 更新参数
void USectionCutComponent::SetCutAmount(float NewAmount, bool bFinalCut)
{
  CutAmount = FMath::Clamp(NewAmount, 0.0f, 1.0f);

  UpdatePlaneOrigin();

  if (CutAmount <= KINDA_SMALL_NUMBER)
  {
    Restore();
    return;
  }

  RequestCutUpdate(bFinalCut);
}

// 设置剖切法线：方向变了投影范围会变，需重算归一化范围后再触发切割
void USectionCutComponent::SetCutNormal(const FVector &NewWorldNormal, bool bFinalCut)
{
  if (NewWorldNormal.IsNearlyZero())
  {
    return;
  }

  CutNormal = NewWorldNormal.GetSafeNormal();

  CalculateProjectionRange();
  UpdatePlaneOrigin();

  if (CutAmount > KINDA_SMALL_NUMBER)
  {
    RequestCutUpdate(bFinalCut);
  }
}

// 以旋转量的 Up 轴作为剖切法线（约定：旋转的 Z 轴 = 平面法线）
void USectionCutComponent::SetCutRotation(const FRotator &NewRotation, bool bFinalCut)
{
  SetCutNormal(NewRotation.RotateVector(FVector::UpVector), bFinalCut);
}

void USectionCutComponent::SetKeepSide(ESectionCutKeepSide NewKeepSide, bool bFinalCut)
{
  KeepSide = NewKeepSide;

  UpdatePlaneOrigin();

  if (CutAmount > KINDA_SMALL_NUMBER)
  {
    RequestCutUpdate(bFinalCut);
  }
}

// 剖切平面世界变换：Z 轴对齐法线，原点为当前平面位置（可视化/手柄用）
FTransform USectionCutComponent::GetCutPlaneTransform() const
{
  const FVector Normal = CutNormal.IsNearlyZero() ? FVector::UpVector : CutNormal.GetSafeNormal();

  return FTransform(FRotationMatrix::MakeFromZ(Normal).ToQuat(), PlaneOrigin, FVector::OneVector);
}

// 松手/确认：执行一次最终高质量切割（生成截面盖）
void USectionCutComponent::CommitCut()
{
  if (CutAmount > KINDA_SMALL_NUMBER)
  {
    RequestCutUpdate(true);
  }
}

// 无损还原：作废未完成请求，全部条目还原源组件并隐藏切割结果
void USectionCutComponent::Restore()
{
  bUpdateRequested = false;
  bRequestedFinalCut = false;
  bQueueActive = false;

  PendingItemIndices.Reset();
  PendingItemCursor = 0;

  for (FSectionCutRuntimeItem &Item :
       RuntimeItems)
  {
    RestoreItem(Item);
    HideResult(Item);
  }

  CutAmount = 0.0f;
  UpdatePlaneOrigin();

  SetComponentTickEnabled(false);
}

// 完全退出：还原 + 销毁所有 Result/Working 动态组件 + 清空运行时状态
void USectionCutComponent::ClearTarget()
{
  Restore();

  for (FSectionCutRuntimeItem &Item :
       RuntimeItems)
  {
    if (IsValid(Item.ResultComponent))
    {
      Item.ResultComponent->DestroyComponent();
    }

    if (IsValid(Item.WorkingComponent))
    {
      Item.WorkingComponent->DestroyComponent();
    }

    Item.ResultComponent = nullptr;
    Item.WorkingComponent = nullptr;
    Item.SourceComponent = nullptr;
  }

  RuntimeItems.Reset();

  TargetBuilding = nullptr;
  bProjectionValid = false;

  ProjectionMin = 0.0f;
  ProjectionMax = 0.0f;
  BoundsCenter = FVector::ZeroVector;
  PlaneOrigin = FVector::ZeroVector;
}

/*
 * 投影范围归一化：把所有参与模型的包围盒 8 角点
 * 投影到剖切法线上（Dot(世界角点, 法线)），
 * 求出 [ProjectionMin, ProjectionMax]。
 * 这是"滑块 0~1"到"平面世界位置"的换算基准——
 * 每个建筑都从自己的边界开始切，而不是从世界原点算绝对距离。
 */
void USectionCutComponent::CalculateProjectionRange()
{
  bProjectionValid = false;
  ProjectionMin = TNumericLimits<float>::Max();
  ProjectionMax = TNumericLimits<float>::Lowest();

  FBox WorldBounds(EForceInit::ForceInit);

  const FVector Normal = CutNormal.GetSafeNormal();

  for (const FSectionCutRuntimeItem &Item : RuntimeItems)
  {
    UStaticMeshComponent *Component = Item.SourceComponent;
    if (!Item.bParticipates || !IsValid(Component) || !IsValid(Component->GetStaticMesh()))
    {
      continue;
    }
    // 拿到这个mesh的包围盒
    const FBox LocalBounds = Component->GetStaticMesh()->GetBoundingBox();
    if (!LocalBounds.IsValid)
    {
      continue;
    }

    const FVector LocalCenter = LocalBounds.GetCenter();

    const FVector LocalExtent = LocalBounds.GetExtent();

    const FTransform Transform = Component->GetComponentTransform();

    for (int32 X = -1; X <= 1; X += 2)
    {
      for (int32 Y = -1; Y <= 1; Y += 2)
      {
        for (int32 Z = -1; Z <= 1; Z += 2)
        {
          const FVector LocalCorner = LocalCenter + FVector(X * LocalExtent.X, Y * LocalExtent.Y, Z * LocalExtent.Z);
          const FVector WorldCorner = Transform.TransformPosition(LocalCorner);
          const float Projection = FVector::DotProduct(WorldCorner, Normal);

          ProjectionMin = FMath::Min(ProjectionMin, Projection);
          ProjectionMax = FMath::Max(ProjectionMax, Projection);

          WorldBounds += WorldCorner;
        }
      }
    }
  }

  if (ProjectionMin <= ProjectionMax && WorldBounds.IsValid)
  {

    BoundsCenter = WorldBounds.GetCenter();
    bProjectionValid = true;
  }
}

/*
 * 由 CutAmount 插值当前平面位置。
 *
 * Start/End 的方向随保留侧翻转：
 * 保留正侧 → 从 Min(全切光) 推进到 Max(完整)；
 * 保留负侧 → 从 Max 推进到 Min。
 *
 * 平面原点不用世界原点 + 纯法线距离，而是绕包围盒中心落点，
 * 避免建筑离世界原点很远时大数减小的浮点精度问题。
 */
void USectionCutComponent::UpdatePlaneOrigin()
{
  if (!bProjectionValid)
  {
    return;
  }

  const float MinProjection = ProjectionMin - BoundsPadding;
  const float MaxProjection = ProjectionMax + BoundsPadding;

  const float StartProjection = IsKeepingPositiveSide()
                                    ? MinProjection
                                    : MaxProjection;

  const float EndProjection = IsKeepingPositiveSide() ? MaxProjection : MinProjection;

  const float CurrentProjection = FMath::Lerp(StartProjection, EndProjection, CutAmount);

  const float CenterProjection = FVector::DotProduct(BoundsCenter, CutNormal);

  PlaneOrigin = BoundsCenter + CutNormal * (CurrentProjection - CenterProjection);
}

/*
 * 由平面世界变换反推状态：
 * Z 轴 = 新法线；平面位置在投影范围里的百分比 = 新的 CutAmount。
 * 与 SetCutAmount 互为逆操作（一个给比例求位置，一个给位置求比例）。
 */
void USectionCutComponent::SetCutPlaneTransform(const FTransform &WorldTransform, bool bFinalCut)
{
  FVector NewNormal = WorldTransform.GetUnitAxis(EAxis::Z);

  if (!NewNormal.Normalize())
  {
    return;
  }

  CutNormal = NewNormal;

  CalculateProjectionRange();

  if (!bProjectionValid)
  {
    return;
  }

  const float MinProjection = ProjectionMin - BoundsPadding;

  const float MaxProjection = ProjectionMax + BoundsPadding;

  const float StartProjection = IsKeepingPositiveSide()
                                    ? MinProjection
                                    : MaxProjection;

  const float EndProjection = IsKeepingPositiveSide()
                                  ? MaxProjection
                                  : MinProjection;

  const float InputProjection = FVector::DotProduct(WorldTransform.GetLocation(), CutNormal);

  CutAmount = FMath::GetRangePct(FVector2D(StartProjection, EndProjection), InputProjection);

  CutAmount = FMath::Clamp(CutAmount, 0.0f, 1.0f);

  UpdatePlaneOrigin();

  if (CutAmount <= KINDA_SMALL_NUMBER)
  {
    Restore();
    return;
  }

  RequestCutUpdate(bFinalCut);
}

/*
 * 包围盒快速分类（性能关键路径）：
 * Radius = 包围盒沿法线方向的"支撑半径"（BoxPushOut 近似），
 * Distance = 中心到平面的有向距离。
 * |Distance| > Radius + Tolerance 说明整个包围盒在平面同一侧，
 * 直接 Keep/Remove 跳过切割；只有跨平面的才进切割队列。
 */
ESectionBoundsRelation USectionCutComponent::ClassifyComponent(const UStaticMeshComponent *Component) const
{
  if (!IsValid(Component))
  {
    return ESectionBoundsRelation::Keep;
  }

  const FVector Center = Component->Bounds.Origin;

  const FVector Extent = Component->Bounds.BoxExtent;

  const FVector Normal = CutNormal.GetSafeNormal();

  const float Radius =
      FMath::Abs(Normal.X) * Extent.X +
      FMath::Abs(Normal.Y) * Extent.Y +
      FMath::Abs(Normal.Z) * Extent.Z;

  const float Distance = FVector::DotProduct(Center - PlaneOrigin, Normal);

  constexpr float Tolerance = 0.1f;

  if (IsKeepingPositiveSide())
  {
    if (Distance > Radius + Tolerance)
    {
      return ESectionBoundsRelation::Keep;
    }

    if (Distance < -Radius - Tolerance)
    {
      return ESectionBoundsRelation::Remove;
    }
  }
  else
  {
    if (Distance < -Radius - Tolerance)
    {
      return ESectionBoundsRelation::Keep;
    }

    if (Distance > Radius + Tolerance)
    {
      return ESectionBoundsRelation::Remove;
    }
  }

  return ESectionBoundsRelation::Intersect;
}
// 还原对应模型的LOD
int32 USectionCutComponent::ResolveLODIndex(const UStaticMesh *StaticMesh) const
{
  if (!IsValid(StaticMesh))
  {
    return 0;
  }

  const int32 LODCount = StaticMesh->GetNumLODs();

  if (LODCount <= 0)
  {
    return 0;
  }

  return FMath::Clamp(RuntimeLODIndex, 0, LODCount - 1);
}

/*
 * 源网格转 ProcMesh 缓存（按 StaticMesh 资产共享）。
 *
 * 转换成本高（遍历 RenderData），所以首次转换后缓存到 SourceMeshCache，
 * 同一 StaticMesh 被多个组件引用时只转换一次；
 * LODIndex 变化视为缓存失效，重新转换。
 */
const FSectionProcMeshSourceCache *USectionCutComponent::GetOrCreateSourceCache(UStaticMeshComponent *SourceComponent)
{
  if (!IsValid(SourceComponent) || !IsValid(SourceComponent->GetStaticMesh()))
  {
    return nullptr;
  }

  UStaticMesh *StaticMesh = SourceComponent->GetStaticMesh();

  const int32 LODIndex = ResolveLODIndex(StaticMesh);

  if (FSectionProcMeshSourceCache *Existing = SourceMeshCache.Find(StaticMesh))
  {
    if (Existing->LODIndex == LODIndex && !Existing->Sections.IsEmpty())
    {
      return Existing;
    }

    SourceMeshCache.Remove(StaticMesh);
  }

  /*
   * 临时组件只负责把 StaticMesh RenderData
   * 转成 ProceduralMesh Sections，不注册到世界。
   */
  UProceduralMeshComponent *TemporaryComponent = NewObject<UProceduralMeshComponent>(GetOwner(), NAME_None, RF_Transient);

  if (!IsValid(TemporaryComponent))
  {
    return nullptr;
  }

  UKismetProceduralMeshLibrary::CopyProceduralMeshFromStaticMeshComponent(
      SourceComponent,
      LODIndex,
      TemporaryComponent,
      false);

  const int32 SectionCount = TemporaryComponent->GetNumSections();

  if (SectionCount <= 0)
  {
    UE_LOG(
        LogSectionCut,
        Warning,
        TEXT(
            "StaticMesh conversion failed: %s. "
            "Check Allow CPU Access."),
        *GetPathNameSafe(StaticMesh));

    TemporaryComponent->DestroyComponent();
    return nullptr;
  }

  FSectionProcMeshSourceCache NewCache;
  NewCache.LODIndex = LODIndex;
  NewCache.Sections.Reserve(SectionCount);

  for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
  {
    FProcMeshSection *Section = TemporaryComponent->GetProcMeshSection(SectionIndex);

    if (Section)
    {
      NewCache.Sections.Add(*Section);
    }
    else
    {
      NewCache.Sections.Add(FProcMeshSection());
    }
  }

  TemporaryComponent->DestroyComponent();

  FSectionProcMeshSourceCache &AddedCache = SourceMeshCache.Add(StaticMesh, MoveTemp(NewCache));

  return &AddedCache;
}

/*
 * 懒创建 Result/Working 两个 PMC。
 *
 * Result：挂到源组件的父级、复用源组件世界变换，关闭碰撞/阴影/
 * 光照贴图等一切附加开销，只作为渲染载体。
 * Working：纯计算用，连世界都不注册（NewObject 后不 RegisterComponent）。
 */
bool USectionCutComponent::EnsureProceduralComponents(FSectionCutRuntimeItem &Item)
{
  if (IsValid(Item.ResultComponent) && IsValid(Item.WorkingComponent))
  {
    return true;
  }

  UStaticMeshComponent *Source = Item.SourceComponent;

  if (!IsValid(Source) || !IsValid(Source->GetOwner()))
  {
    return false;
  }

  AActor *SourceOwner = Source->GetOwner();

  UProceduralMeshComponent *Result = NewObject<UProceduralMeshComponent>(SourceOwner, MakeUniqueObjectName(SourceOwner, UProceduralMeshComponent::StaticClass(), TEXT("SectionCutResult")), RF_Transient);

  if (!IsValid(Result))
  {
    return false;
  }

  Result->SetMobility(EComponentMobility::Movable);

  if (USceneComponent *Parent = Source->GetAttachParent())
  {
    Result->SetupAttachment(Parent);
  }

  SourceOwner->AddInstanceComponent(Result);
  Result->RegisterComponent();

  Result->SetWorldTransform(Source->GetComponentTransform());

  Result->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  Result->SetGenerateOverlapEvents(false);
  Result->SetCanEverAffectNavigation(false);
  Result->SetCastShadow(false);
  Result->SetAffectDistanceFieldLighting(false);

  Result->bVisibleInRayTracing = false;
  Result->bUseAsyncCooking = false;
  Result->bUseComplexAsSimpleCollision = false;

  Result->SetVisibility(false, false);
  Result->SetHiddenInGame(true, false);

  /*
   * 工作组件不注册，不产生 SceneProxy，也不会绘制。
   */
  UProceduralMeshComponent *Working = NewObject<UProceduralMeshComponent>(
      SourceOwner,
      NAME_None,
      RF_Transient);

  if (!IsValid(Working))
  {
    Result->DestroyComponent();
    return false;
  }

  Working->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  Working->bUseComplexAsSimpleCollision = false;
  Working->bUseAsyncCooking = false;

  Item.ResultComponent = Result;
  Item.WorkingComponent = Working;

  return true;
}

/*
 * 每次切割前把 Working 复位成"完整的未切割源网格"：
 * 清段 → 对齐源变换 → 从缓存灌回全部 Sections →
 * 按源组件材质槽对应关系赋材质（SectionInfoMap 里的 MaterialIndex 才是真槽位）。
 */
bool USectionCutComponent::ResetWorkingComponent(FSectionCutRuntimeItem &Item, const FSectionProcMeshSourceCache &Cache)
{
  UStaticMeshComponent *Source = Item.SourceComponent;

  UProceduralMeshComponent *Working = Item.WorkingComponent;

  if (!IsValid(Source) || !IsValid(Working) || !IsValid(Source->GetStaticMesh()))
  {
    return false;
  }

  Working->ClearAllMeshSections();
  Working->ClearCollisionConvexMeshes();
  Working->EmptyOverrideMaterials();

  Working->SetWorldTransform(Source->GetComponentTransform());

  for (int32 SectionIndex = 0; SectionIndex < Cache.Sections.Num(); ++SectionIndex)
  {
    FProcMeshSection Section = Cache.Sections[SectionIndex];

    Section.bEnableCollision = false;

    Working->SetProcMeshSection(SectionIndex, Section);
  }

  UStaticMesh *StaticMesh = Source->GetStaticMesh();

  const FMeshSectionInfoMap &SectionInfoMap = StaticMesh->GetSectionInfoMap();

  for (int32 SectionIndex = 0;
       SectionIndex < Cache.Sections.Num();
       ++SectionIndex)
  {
    int32 MaterialSlotIndex = SectionIndex;

    if (SectionInfoMap.IsValidSection(Cache.LODIndex, SectionIndex))
    {
      MaterialSlotIndex = SectionInfoMap.Get(Cache.LODIndex, SectionIndex).MaterialIndex;
    }

    UMaterialInterface *Material = Source->GetMaterial(MaterialSlotIndex);

    if (!IsValid(Material))
    {
      Material = UMaterial::GetDefaultMaterial(MD_Surface);
    }

    /*
     * ProceduralMesh 的材质索引与 SectionIndex 对应。
     */
    Working->SetMaterial(SectionIndex, Material);
  }

  return true;
}

/*
 * 计算传给 SliceProceduralMesh 的法线。
 *
 * 该 API 保留"平面法线正方向"一侧，所以要按 KeepSide 取正/负；
 * 另外它内部按组件世界变换做了逆变换，
 * 这里对"组件带旋转/非均匀缩放"的情况做相应补偿，
 * 保证世界空间里保留侧与 KeepSide 语义一致。
 */
FVector USectionCutComponent::CalculateSliceNormalForComponent(const UProceduralMeshComponent *Component) const
{
  if (!IsValid(Component))
  {
    return CutNormal;
  }

  /*
   * SliceProceduralMesh 保留传入法线的正方向。
   */
  const FVector DesiredWorldNormal = IsKeepingPositiveSide() ? CutNormal : -CutNormal;

  const FTransform Transform = Component->GetComponentTransform();

  const FVector Scale = Transform.GetScale3D();

  FVector DesiredLocalNormal = Transform.GetRotation().UnrotateVector(DesiredWorldNormal);

  /*
   * 世界平面法线转局部平面法线：
   * LocalNormal = Scale * R^-1 * WorldNormal
   */
  DesiredLocalNormal.X *= Scale.X;
  DesiredLocalNormal.Y *= Scale.Y;
  DesiredLocalNormal.Z *= Scale.Z;

  if (!DesiredLocalNormal.Normalize())
  {
    return DesiredWorldNormal;
  }

  /*
   * SliceProceduralMesh 内部会做一次逆旋转，
   * 所以这里先把补偿后的局部法线旋回世界空间。
   */
  return Transform.GetRotation().RotateVector(DesiredLocalNormal).GetSafeNormal();
}

/*
 * 单模型切割全流程：
 * 取源缓存 → 确保 Result/Working → Working 复位成完整网格 →
 * SliceProceduralMesh 切片（按需封口）→ Sections 复制到 Result →
 * Result 对齐源变换。任何一步失败返回 false（由调用方还原该条目）。
 */
bool USectionCutComponent::RebuildCutMesh(FSectionCutRuntimeItem &Item, bool bFinalCut)
{
  UStaticMeshComponent *Source = Item.SourceComponent;

  if (!IsValid(Source) || !IsValid(Source->GetStaticMesh()))
  {
    return false;
  }

  const FSectionProcMeshSourceCache *Cache = GetOrCreateSourceCache(Source);

  if (!Cache)
  {
    return false;
  }

  if (!EnsureProceduralComponents(Item))
  {
    return false;
  }

  if (!ResetWorkingComponent(Item, *Cache))
  {
    return false;
  }

  UProceduralMeshComponent *Working = Item.WorkingComponent;

  const FVector SliceNormal = CalculateSliceNormalForComponent(Working);

  const bool bCreateCap = bEnableCap && (bFinalCut || bFillCapDuringPreview);

  const EProcMeshSliceCapOption CapOption =
      bCreateCap
          ? EProcMeshSliceCapOption::CreateNewSectionForCap // 要封口 新建一个截面段
          : EProcMeshSliceCapOption::NoCap;                 // 不要封口 切完就是空壳

  UMaterialInterface *CapMaterial =
      IsValid(SectionCapMaterial)
          ? SectionCapMaterial
          : UMaterial::GetDefaultMaterial(MD_Surface);

  UProceduralMeshComponent *UnusedOtherHalf =
      nullptr;
  // 切割 ProceduralMesh
  // 遍历网格三角形 判断三角形位于平面哪一侧 删除不保留侧的三角形 对跨越平面的三角形计算交点 生成新的边缘顶点和三角形 最终切割时根据配置进行封口
  UKismetProceduralMeshLibrary::SliceProceduralMesh(
      Working,         // 输入：要切割的 ProceduralMesh 组件
      PlaneOrigin,     // 输入：切割平面的原点
      SliceNormal,     // 输入：切割平面的法线
      false,           // 输入：是否保留切分线
      UnusedOtherHalf, // 输出：未被保留的 ProceduralMesh 组件
      CapOption,       // 输入：封口选项
      CapMaterial);    // 输入：封口材质

  /*
   * Working 不渲染。
   * 切割完成后才把最终 Sections 一次性复制到 Result。
   */
  CopyProceduralMeshSections(Working, Item.ResultComponent);

  Item.ResultComponent->SetWorldTransform(Source->GetComponentTransform());

  return true;
}

// Working → Result 全量拷贝：Sections（禁碰撞）+ 全部材质槽（含封口新增段）
void USectionCutComponent::CopyProceduralMeshSections(UProceduralMeshComponent *From, UProceduralMeshComponent *To)
{
  if (!IsValid(From) || !IsValid(To))
  {
    return;
  }

  To->ClearAllMeshSections();
  To->ClearCollisionConvexMeshes();
  To->EmptyOverrideMaterials();

  const int32 SectionCount = From->GetNumSections();

  for (int32 SectionIndex = 0;
       SectionIndex < SectionCount;
       ++SectionIndex)
  {
    FProcMeshSection *Section = From->GetProcMeshSection(SectionIndex);

    if (!Section)
    {
      continue;
    }

    FProcMeshSection ResultSection = *Section;
    ResultSection.bEnableCollision = false;

    To->SetProcMeshSection(SectionIndex, ResultSection);
  }

  /*
   * 包括 SliceProceduralMesh 新增的封口材质。
   */
  const int32 MaterialCount = From->GetNumMaterials();

  for (int32 MaterialIndex = 0;
       MaterialIndex < MaterialCount;
       ++MaterialIndex)
  {
    To->SetMaterial(MaterialIndex, From->GetMaterial(MaterialIndex));
  }
}

// 请求切割：重建队列状态并开启 Tick，实际执行时机由 Tick 里的节流/预算控制
void USectionCutComponent::RequestCutUpdate(bool bFinalCut)
{
  if (!IsValid(TargetBuilding) ||
      !bProjectionValid)
  {
    return;
  }

  PendingItemIndices.Reset();
  PendingItemCursor = 0;
  bQueueActive = false;

  bUpdateRequested = true;
  bRequestedFinalCut = bFinalCut;

  SetComponentTickEnabled(true);
}

/*
 * Tick 双职责：
 * 1) 有未处理的更新请求时，按预览节流间隔（最终切割不限）发起一轮切割；
 * 2) 队列活跃时按帧预算继续消化切割；
 * 都没活干就自关 Tick（RequestCutUpdate 时再打开）。
 */
void USectionCutComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *TickFunction)
{
  Super::TickComponent(DeltaTime, TickType, TickFunction);

  const double CurrentTime = FPlatformTime::Seconds();

  if (bUpdateRequested)
  {
    const bool bCanUpdate = bRequestedFinalCut || LastPreviewUpdateTime < 0.0 || CurrentTime - LastPreviewUpdateTime >= PreviewUpdateInterval;

    if (bCanUpdate)
    {
      const bool bFinal = bRequestedFinalCut;

      bUpdateRequested = false;
      bRequestedFinalCut = false;

      BeginQueuedUpdate(bFinal);

      if (!bFinal)
      {
        LastPreviewUpdateTime = CurrentTime;
      }
    }
  }

  if (bQueueActive)
  {
    ProcessQueuedWork();
  }

  if (!bUpdateRequested && !bQueueActive)
  {
    SetComponentTickEnabled(false);
  }
}

/*
 * 一轮切割的开始：先做一遍零成本分类——
 * Keep：整模型在保留侧 → 还原源组件显示，不进队列；
 * Remove：整模型在移除侧 → 隐藏源组件，不进队列；
 * Intersect：跨平面 → 记录 Result 可见性决定源组件先隐藏还是先保留，
 *            排进队列等 RebuildCutMesh 真正切割。
 * 队列为空说明没有任何模型需要切割，直接广播完成。
 */
void USectionCutComponent::BeginQueuedUpdate(bool bFinalCut)
{
  PendingItemIndices.Reset();
  PendingItemCursor = 0;

  bQueuedFinalCut = bFinalCut;
  bQueueActive = true;

  for (int32 Index = 0; Index < RuntimeItems.Num(); ++Index)
  {
    FSectionCutRuntimeItem &Item = RuntimeItems[Index];

    if (!Item.bParticipates ||
        !IsValid(Item.SourceComponent))
    {
      RestoreItem(Item);
      HideResult(Item);
      continue;
    }

    switch (ClassifyComponent(Item.SourceComponent))
    {
    case ESectionBoundsRelation::Keep:
      RestoreItem(Item);
      HideResult(Item);
      break;

    case ESectionBoundsRelation::Remove:
      HideSource(Item);
      HideResult(Item);
      break;

    case ESectionBoundsRelation::Intersect:
      if (IsValid(Item.ResultComponent) &&
          Item.ResultComponent->GetVisibleFlag() &&
          !Item.ResultComponent->bHiddenInGame)
      {
        HideSource(Item);
      }
      else
      {
        RestoreItem(Item);
      }

      PendingItemIndices.Add(Index);
      break;
    }
  }

  if (PendingItemIndices.IsEmpty())
  {
    bQueueActive = false;
    OnSectionCutFinished.Broadcast(bFinalCut);
  }
}

/*
 * 分帧消化切割队列：至少处理一个条目，然后检查耗时，
 * 超过 FrameBudgetMilliseconds 就 break 留到下一帧，
 * 保证拖动剖切时不掉帧。队列清空时广播完成。
 */
void USectionCutComponent::ProcessQueuedWork()
{
  const double StartTime = FPlatformTime::Seconds();

  const double BudgetSeconds = FMath::Max(FrameBudgetMilliseconds, 0.1f) / 1000.0;

  bool bProcessedOne = false;

  while (PendingItemCursor < PendingItemIndices.Num())
  {
    const int32 ItemIndex = PendingItemIndices[PendingItemCursor++];

    if (RuntimeItems.IsValidIndex(ItemIndex))
    {
      FSectionCutRuntimeItem &Item = RuntimeItems[ItemIndex];

      if (RebuildCutMesh(Item, bQueuedFinalCut))
      {
        HideSource(Item);
        ShowResult(Item);
      }
      else
      {
        RestoreItem(Item);
        HideResult(Item);
      }
    }

    bProcessedOne = true;

    if (bProcessedOne && FPlatformTime::Seconds() - StartTime >= BudgetSeconds)
    {
      break;
    }
  }

  if (PendingItemCursor >= PendingItemIndices.Num())
  {
    bQueueActive = false;

    OnSectionCutFinished.Broadcast(bQueuedFinalCut);
  }
}

// 清空源网格转换缓存（下次切割会重新转换，StaticMesh 内容变更后调用）
void USectionCutComponent::ClearMeshCache()
{
  SourceMeshCache.Reset();
}

// 显示切割结果
void USectionCutComponent::ShowResult(FSectionCutRuntimeItem &Item)
{
  if (!IsValid(Item.ResultComponent))
  {
    return;
  }

  Item.ResultComponent->SetVisibility(true, false);

  Item.ResultComponent->SetHiddenInGame(false, false);
}

// 隐藏切割结果
void USectionCutComponent::HideResult(FSectionCutRuntimeItem &Item)
{
  if (!IsValid(Item.ResultComponent))
  {
    return;
  }

  Item.ResultComponent->SetVisibility(false, false);

  Item.ResultComponent->SetHiddenInGame(true, false);
}

// 还原源组件到进入剖切前的可见状态
void USectionCutComponent::RestoreItem(
    FSectionCutRuntimeItem &Item)
{
  if (!IsValid(Item.SourceComponent))
  {
    return;
  }

  Item.SourceComponent->SetVisibility(Item.bOriginalVisible, false);

  Item.SourceComponent->SetHiddenInGame(Item.bOriginalHiddenInGame, false);
}

// 隐藏源组件（切割结果已接管显示）
void USectionCutComponent::HideSource(
    FSectionCutRuntimeItem &Item)
{
  if (!IsValid(Item.SourceComponent))
  {
    return;
  }

  Item.SourceComponent->SetVisibility(false, false);

  Item.SourceComponent->SetHiddenInGame(true, false);
}
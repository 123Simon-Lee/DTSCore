#include "Core/Component/BuildingFloorComponent.h"
#include "Core/Effect/BuildingStencilChannels.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Space/Space.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
  /**
   * 楼层动画移动 RuntimeActor 时，附加在它下面的 Datasmith Actor 也会跟随移动。
   * Mobility 不会从父 Actor 继承，因此要在动画开始前将整棵附加层级设为 Movable。
   */
  void SetActorHierarchyMovable(
      AActor *Actor,
      TSet<AActor *> &VisitedActors)
  {
    if (!IsValid(Actor) || VisitedActors.Contains(Actor))
    {
      return;
    }

    VisitedActors.Add(Actor);

    TInlineComponentArray<USceneComponent *> SceneComponents;
    Actor->GetComponents(SceneComponents);

    for (USceneComponent *SceneComponent : SceneComponents)
    {
      if (IsValid(SceneComponent) &&
          SceneComponent->Mobility != EComponentMobility::Movable)
      {
        SceneComponent->SetMobility(EComponentMobility::Movable);
      }
    }

    TArray<AActor *> AttachedActors;
    Actor->GetAttachedActors(AttachedActors, true, false);

    for (AActor *AttachedActor : AttachedActors)
    {
      SetActorHierarchyMovable(AttachedActor, VisitedActors);
    }
  }

  /** 在运行时附加关系建立完成后，一次性准备所有会参与位移动画的楼层。 */
  void PrepareAnimatedFloorActors(
      const TArray<TObjectPtr<UBuildingRuntimeNode>> &FloorNodes)
  {
    TSet<AActor *> VisitedActors;

    for (const TObjectPtr<UBuildingRuntimeNode> &FloorNodePtr : FloorNodes)
    {
      const UBuildingRuntimeNode *FloorNode = FloorNodePtr.Get();
      if (!FloorNode || !IsValid(FloorNode->RuntimeActor))
      {
        continue;
      }

      SetActorHierarchyMovable(
          FloorNode->RuntimeActor.Get(),
          VisitedActors);
    }
  }

  /**
   * 不依赖业务模块，通过反射调用楼层已有的 SetFloorMeshHidden。
   * 该业务方法会同时处理 OrdinaryActors、TH 天花和 WQ 外墙。
   */
  bool TrySetFloorMeshHidden(AActor *Actor, bool bHidden)
  {
    if (!IsValid(Actor))
    {
      return false;
    }

    UFunction *Function = Actor->FindFunction(
        TEXT("SetFloorMeshHidden"));
    if (!Function)
    {
      return false;
    }

    FBoolProperty *NewHiddenProperty =
        FindFProperty<FBoolProperty>(Function, TEXT("NewHidden"));
    if (!NewHiddenProperty)
    {
      UE_LOG(
          LogTemp,
          Warning,
          TEXT("[BuildingFloorComponent] SetFloorMeshHidden has no NewHidden parameter: %s"),
          *Actor->GetActorNameOrLabel());
      return false;
    }

    FStructOnScope Parameters(Function);
    NewHiddenProperty->SetPropertyValue_InContainer(
        Parameters.GetStructMemory(),
        bHidden);
    Actor->ProcessEvent(Function, Parameters.GetStructMemory());
    return true;
  }

  /**
   * 不直接依赖项目业务模块，通过反射调用楼层 Actor 自己维护的
   * SetFloorCeilingHidden。
   *
   * 单层楼栋业务直接使用 ABuildingFloor::FloorCeilingSet 隐藏天花，
   * 该集合不一定与拆楼组件按 TH 名称扫描出的缓存完全一致。
   * 设备聚焦返回时重新应用楼层状态，必须同时恢复这份业务数据源。
   */
  bool TrySetFloorCeilingHidden(AActor *Actor, bool bHidden)
  {
    if (!IsValid(Actor))
    {
      return false;
    }

    UFunction *Function = Actor->FindFunction(
        TEXT("SetFloorCeilingHidden"));
    if (!Function)
    {
      return false;
    }

    FBoolProperty *NewHiddenProperty =
        FindFProperty<FBoolProperty>(Function, TEXT("NewHidden"));
    if (!NewHiddenProperty)
    {
      UE_LOG(
          LogTemp,
          Warning,
          TEXT("[BuildingFloorComponent] SetFloorCeilingHidden has no NewHidden parameter: %s"),
          *Actor->GetActorNameOrLabel());
      return false;
    }

    FStructOnScope Parameters(Function);
    NewHiddenProperty->SetPropertyValue_InContainer(
        Parameters.GetStructMemory(),
        bHidden);
    Actor->ProcessEvent(Function, Parameters.GetStructMemory());
    return true;
  }
}

// ============================================================
// Component
// ============================================================
UBuildingFloorComponent::UBuildingFloorComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;

  // 曲线资产注册
  ConstructorHelpers::FObjectFinder<UCurveFloat> CurveObj(
      TEXT("/Script/Engine.CurveFloat'/DTSCore/CameraTimeline.CameraTimeline'"));

  if (CurveObj.Succeeded())
  {
    RuntimeCurve = CurveObj.Object;
  }
}

void UBuildingFloorComponent::BeginPlay()
{
  Super::BeginPlay();
}

void UBuildingFloorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  // timeline tick
  LayeringTimeline.TickTimeline(DeltaTime);
  FloorExpandTimeline.TickTimeline(DeltaTime);
  LiftDropTimeline.TickTimeline(DeltaTime);
}

// ============================================================
// Initialize初始化
// ============================================================
void UBuildingFloorComponent::Initialize(const FBuildingRuntimeData &InRuntimeData)
{
  BuildingRuntimeData = InRuntimeData;
  // 缓存运行数据
  BuildRuntimeCache();
  // Datasmith 子 Actor 的 Mobility 不会继承自楼层 Actor，动画前统一处理一次。
  PrepareAnimatedFloorActors(FloorNodes);
  // 初始化节点的Transform
  InitializeTransforms();
  // 计算楼层的总高度和最大尺寸
  ComputeFloorMetrics();
  // 初始化Timeline和绑定对应方法到timeline
  InitializeTimelines();
}

void UBuildingFloorComponent::InitializeTransforms()
{
  // Initialize the original and current transforms for all nodes in the building runtime data
  if (!BuildingRuntimeData.Root)
  {
    return;
  }

  BuildingRuntimeData.Root->ForEachNode(
      [](UBuildingRuntimeNode *Node)
      {
        if (!Node || !Node->RuntimeActor)
        {
          return;
        }

        Node->OriginalTransform = Node->RuntimeActor->GetActorTransform();
        Node->CurrentTransform = Node->OriginalTransform;
        Node->CurrentOffset = FVector::ZeroVector;
        Node->TargetOffset = FVector::ZeroVector;
        Node->bExpanded = false;
        Node->bHidden = Node->RuntimeActor->IsHidden();
      });
}

void UBuildingFloorComponent::InitializeTimelines()
{
  // 从配置获取对应的曲线资产
  UCurveFloat *Curve = BuildingRuntimeData.Config.Animation.Curve
                           ? BuildingRuntimeData.Config.Animation.Curve.Get()
                           : RuntimeCurve.Get();

  if (!Curve)
  {
    UE_LOG(LogTemp, Error, TEXT("Curve is not Vaild."));
    return;
  }
  // timeline bind function/value
  LayeringTimeline = FTimeline();
  FloorExpandTimeline = FTimeline();
  LiftDropTimeline = FTimeline();

  // 楼层分离动画
  FOnTimelineFloat LayeringUpdate;
  LayeringUpdate.BindDynamic(this, &UBuildingFloorComponent::OnLayeringUpdate);

  FOnTimelineEvent LayeringFinished;
  LayeringFinished.BindDynamic(this, &UBuildingFloorComponent::OnLayeringFinished);

  LayeringTimeline.AddInterpFloat(Curve, LayeringUpdate);
  LayeringTimeline.SetTimelineFinishedFunc(LayeringFinished);
  LayeringTimeline.SetLooping(false);

  // 楼层抽出推入动画
  FOnTimelineFloat FloorExpandUpdate;
  FloorExpandUpdate.BindDynamic(this, &UBuildingFloorComponent::OnFloorExpandUpdate);

  FOnTimelineEvent FloorExpandFinished;
  FloorExpandFinished.BindDynamic(this, &UBuildingFloorComponent::OnFloorExpandFinished);

  FloorExpandTimeline.AddInterpFloat(Curve, FloorExpandUpdate);
  FloorExpandTimeline.SetTimelineFinishedFunc(FloorExpandFinished);
  FloorExpandTimeline.SetLooping(false);

  // List Drop动画
  FOnTimelineFloat LiftDropUpdate;
  LiftDropUpdate.BindDynamic(this, &UBuildingFloorComponent::OnLiftDropUpdate);

  FOnTimelineEvent LiftDropFinished;
  LiftDropFinished.BindDynamic(this, &UBuildingFloorComponent::OnLiftDropFinished);

  LiftDropTimeline.AddInterpFloat(Curve, LiftDropUpdate);

  LiftDropTimeline.SetTimelineFinishedFunc(LiftDropFinished);

  LiftDropTimeline.SetLooping(false);
}
// ============================================================
// Runtime Cache
// ============================================================
void UBuildingFloorComponent::BuildRuntimeCache()
{
  // 先清空缓存
  FloorMap.Empty();
  ActorMap.Empty();
  FloorNodes.Empty();
  HiddenFloors.Empty();
  FloorCeilingActorMap.Empty();
  FloorWallActorMap.Empty();

  if (BuildingRuntimeData.Root)
  {
    CollectFloorNodes(BuildingRuntimeData.Root);
    FloorNodes.Sort(
        [](const UBuildingRuntimeNode &A, const UBuildingRuntimeNode &B)
        {
          return A.FloorIndex < B.FloorIndex;
        });
    BuildNamedActorCache();
  }
}

void UBuildingFloorComponent::CollectFloorNodes(UBuildingRuntimeNode *Node)
{
  if (!Node)
  {
    return;
  }

  // 数据源可以只提供 SourceObject；若它本身是 Actor 则作为移动对象兜底。
  if (!Node->RuntimeActor && Node->SourceObject)
  {
    Node->RuntimeActor = Cast<AActor>(Node->SourceObject);
  }
  // 如果节点是楼层节点且允许拆解才进来
  if (Node->FloorIndex != BUILDING_INVALID_FLOOR_INDEX && Node->bCanDisassemble)
  {
    FloorMap.Add(Node->FloorIndex, Node);
    FloorNodes.Add(Node);
  }

  if (Node->RuntimeActor)
  {
    ActorMap.Add(Node->RuntimeActor, Node);
  }

  for (AActor *ControlledActor : Node->ControlledActors)
  {
    if (IsValid(ControlledActor))
    {
      ActorMap.Add(ControlledActor, Node);
    }
  }

  for (TObjectPtr<UBuildingRuntimeNode> &Child : Node->Children)
  {
    if (Child)
    {
      CollectFloorNodes(Child.Get());
    }
  }
}

void UBuildingFloorComponent::BuildNamedActorCache()
{
  const FBuildingNameSettings &NameSettings = BuildingRuntimeData.Config.Name;

  for (const TObjectPtr<UBuildingRuntimeNode> &FloorNodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *FloorNode = FloorNodePtr.Get();
    if (!FloorNode)
    {
      continue;
    }

    TSet<AActor *> Candidates;
    FloorNode->ForEachNode(
        [&Candidates](UBuildingRuntimeNode *Node)
        {
          if (!Node)
          {
            return;
          }

          if (IsValid(Node->RuntimeActor))
          {
            Candidates.Add(Node->RuntimeActor.Get());
          }
          for (AActor *ControlledActor : Node->ControlledActors)
          {
            if (IsValid(ControlledActor))
            {
              Candidates.Add(ControlledActor);
            }
          }
        });

    // SpaceAdapter 不需要暴露 OrdinaryActors；普通模型只要挂在楼层
    // Actor 下，也可以通过通用 Actor 附加关系被识别。
    if (IsValid(FloorNode->RuntimeActor))
    {
      TArray<AActor *> AttachedActors;
      FloorNode->RuntimeActor->GetAttachedActors(
          AttachedActors,
          true,
          true);
      for (AActor *AttachedActor : AttachedActors)
      {
        if (IsValid(AttachedActor))
        {
          Candidates.Add(AttachedActor);
        }
      }
    }

    TArray<TWeakObjectPtr<AActor>> &CeilingActors =
        FloorCeilingActorMap.FindOrAdd(FloorNode->FloorIndex);
    TArray<TWeakObjectPtr<AActor>> &WallActors =
        FloorWallActorMap.FindOrAdd(FloorNode->FloorIndex);

    for (AActor *Candidate : Candidates)
    {
      if (MatchesActorNameRule(Candidate, NameSettings.THRule))
      {
        CeilingActors.AddUnique(Candidate);
      }
      if (MatchesActorNameRule(Candidate, NameSettings.WQRule))
      {
        WallActors.AddUnique(Candidate);
      }
    }
  }
}

bool UBuildingFloorComponent::MatchesActorNameRule(
    const AActor *Actor,
    const FNameMatchRule &Rule) const
{
  if (!IsValid(Actor) || Rule.MatchString.IsEmpty())
  {
    return false;
  }

  auto RemoveNumericSuffix = [](FString Value)
  {
    int32 SeparatorIndex = INDEX_NONE;
    if (!Value.FindLastChar(TEXT('_'), SeparatorIndex) ||
        SeparatorIndex >= Value.Len() - 1)
    {
      return Value;
    }

    const FString Suffix = Value.Mid(SeparatorIndex + 1);
    bool bOnlyDigits = !Suffix.IsEmpty();
    for (const TCHAR Character : Suffix)
    {
      if (!FChar::IsDigit(Character))
      {
        bOnlyDigits = false;
        break;
      }
    }

    return bOnlyDigits ? Value.Left(SeparatorIndex) : Value;
  };

  auto MatchesValue = [&Rule](const FString &Value)
  {
    switch (Rule.MatchType)
    {
    case ENameMatchType::Equals:
      return Value.Equals(Rule.MatchString, ESearchCase::IgnoreCase);
    case ENameMatchType::StartsWith:
      return Value.StartsWith(Rule.MatchString, ESearchCase::IgnoreCase);
    case ENameMatchType::EndsWith:
      return Value.EndsWith(Rule.MatchString, ESearchCase::IgnoreCase);
    case ENameMatchType::Contains:
      return Value.Contains(Rule.MatchString, ESearchCase::IgnoreCase);
    default:
      return false;
    }
  };

  const FString Label = Actor->GetActorNameOrLabel();
  const FString ObjectName = Actor->GetName();
  return MatchesValue(Label) ||
         MatchesValue(RemoveNumericSuffix(Label)) ||
         MatchesValue(ObjectName) ||
         MatchesValue(RemoveNumericSuffix(ObjectName));
}

void UBuildingFloorComponent::SetNamedActorsHidden(
    const TMap<int32, TArray<TWeakObjectPtr<AActor>>> &NamedActorMap,
    int32 FloorIndex,
    bool bHidden)
{
  const TArray<TWeakObjectPtr<AActor>> *Actors =
      NamedActorMap.Find(FloorIndex);
  if (!Actors)
  {
    return;
  }

  for (const TWeakObjectPtr<AActor> &ActorPtr : *Actors)
  {
    if (AActor *Actor = ActorPtr.Get())
    {
      Actor->SetActorHiddenInGame(bHidden);
    }
  }
}

void UBuildingFloorComponent::RestoreAllFloorCeilings()
{
  for (const TPair<int32, TArray<TWeakObjectPtr<AActor>>> &Pair :
       FloorCeilingActorMap)
  {
    SetNamedActorsHidden(FloorCeilingActorMap, Pair.Key, false);
  }
}

void UBuildingFloorComponent::ComputeFloorMetrics()
{
  TotalBuildingHeight = 0.f;
  MaxFloorSizeX = 0.f;
  MaxFloorSizeY = 0.f;
  if (FloorMap.Num() == 0)
  {
    return;
  }
  float MinZ = FLT_MAX;
  float MaxZ = -FLT_MAX;
  // 计算每个节点的包围盒，并更新总高度
  for (TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *Node = NodePtr.Get();
    if (!Node)
    {
      continue;
    }
    FBox Box = GetNodeBounds(Node);
    Node->Bounds = Box;
    if (!Box.IsValid)
      continue;
    MinZ = FMath::Min(MinZ, Box.Min.Z);
    MaxZ = FMath::Max(MaxZ, Box.Max.Z);
  }
  if (MaxZ > MinZ)
  {
    TotalBuildingHeight = MaxZ - MinZ;
  }
  // 如果配置中有设置最大尺寸，则使用配置的尺寸
  if (BuildingRuntimeData.Config.Drawer.MaxFloorSizeX > 0.f)
  {
    MaxFloorSizeX = BuildingRuntimeData.Config.Drawer.MaxFloorSizeX;
  }

  if (BuildingRuntimeData.Config.Drawer.MaxFloorSizeY > 0.f)
  {
    MaxFloorSizeY = BuildingRuntimeData.Config.Drawer.MaxFloorSizeY;
  }
  // 如果配置中没有设置最大尺寸，且计算得到的尺寸小于0 则使用默认值1000.f
  if (TotalBuildingHeight <= 0.f)
  {
    TotalBuildingHeight = 1000.f;
  }

  if (MaxFloorSizeX <= 0.f)
  {
    MaxFloorSizeX = 1000.f;
  }

  if (MaxFloorSizeY <= 0.f)
  {
    MaxFloorSizeY = 1000.f;
  }
}

FBox UBuildingFloorComponent::GetNodeBounds(UBuildingRuntimeNode *Node) const
{
  if (!Node || !Node->RuntimeActor)
  {
    return FBox(ForceInit);
  }

  FVector Origin;
  FVector Extent;
  Node->RuntimeActor->GetActorBounds(true, Origin, Extent);

  return FBox(Origin - Extent, Origin + Extent);
}

FBuildingFocusTarget UBuildingFloorComponent::GetBuildingFocusTarget() const
{
  if (!BuildingRuntimeData.Root || !BuildingRuntimeData.BuildingActor)
  {
    return FBuildingFocusTarget();
  }

  TArray<UBuildingRuntimeNode *> FocusRoots;
  FocusRoots.Reserve(FloorNodes.Num());
  for (const TObjectPtr<UBuildingRuntimeNode> &FloorNode : FloorNodes)
  {
    if (FloorNode)
    {
      FocusRoots.Add(FloorNode.Get());
    }
  }

  // 单层或非标准 Adapter 没有标记楼层时，以整棵 Runtime 树兜底。
  if (FocusRoots.Num() == 0)
  {
    FocusRoots.Add(BuildingRuntimeData.Root.Get());
  }

  return BuildFocusTarget(
      FocusRoots,
      BuildingRuntimeData.BuildingActor.Get());
}

FBuildingFocusTarget UBuildingFloorComponent::GetFloorFocusTarget(
    int32 FloorIndex) const
{
  UBuildingRuntimeNode *FloorNode = FindFloorWithIndex(FloorIndex);
  if (!FloorNode)
  {
    return FBuildingFocusTarget();
  }

  TArray<UBuildingRuntimeNode *> FocusRoots;
  FocusRoots.Add(FloorNode);
  return BuildFocusTarget(FocusRoots, FloorNode->RuntimeActor.Get());
}

FBuildingFocusTarget UBuildingFloorComponent::GetFloorWallFocusTarget(
    int32 FloorIndex) const
{
  UBuildingRuntimeNode *FloorNode = FindFloorWithIndex(FloorIndex);
  if (!FloorNode)
  {
    return FBuildingFocusTarget();
  }

  // 本层按 WQRule 缓存的外墙 Actor（BuildNamedActorCache 初始化时填好）。
  const TArray<AActor *> WallActors = GetFloorWallActors(FloorIndex);
  if (WallActors.Num() == 0)
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[BuildingFloorComponent] Floor %d 没有匹配到 WQ 外墙，回退整层聚焦。"),
        FloorIndex);
  }

  // WQ 为空时 BuildFocusTargetFromActors 内部会用 FallbackActor 兜底。
  return BuildFocusTargetFromActors(
      WallActors,
      FloorNode->RuntimeActor.Get());
}

FBuildingFocusSettings UBuildingFloorComponent::GetFloorFocusSettings(
    const int32 FloorIndex) const
{
  FBuildingFocusSettings Result = BuildingRuntimeData.Config.Focus;
  const FBuildingFloorFocusSettings *Override =
      BuildingRuntimeData.Config.FloorFocusOverrides.Find(FloorIndex);
  if (!Override)
  {
    return Result;
  }

  Result.Padding = Override->Padding;
  Result.Pitch = Override->Pitch;
  Result.bUseAutoYaw = Override->bUseAutoYaw;
  Result.Yaw = Override->Yaw;
  Result.Roll = Override->Roll;
  Result.DiagonalViewRatio = Override->DiagonalViewRatio;
  Result.CameraLocation = Override->CameraLocation;
  Result.InterpTime = Override->InterpTime;
  Result.MinDistance = Override->MinDistance;
  Result.MaxDistance = Override->MaxDistance;
  return Result;
}

TArray<AActor *> UBuildingFloorComponent::GetFloorCeilingActors(
    int32 FloorIndex) const
{
  TArray<AActor *> Result;
  if (const TArray<TWeakObjectPtr<AActor>> *Actors =
          FloorCeilingActorMap.Find(FloorIndex))
  {
    Result.Reserve(Actors->Num());
    for (const TWeakObjectPtr<AActor> &ActorPtr : *Actors)
    {
      if (AActor *Actor = ActorPtr.Get())
      {
        Result.Add(Actor);
      }
    }
  }
  return Result;
}

TArray<AActor *> UBuildingFloorComponent::GetFloorWallActors(
    int32 FloorIndex) const
{
  TArray<AActor *> Result;
  if (const TArray<TWeakObjectPtr<AActor>> *Actors =
          FloorWallActorMap.Find(FloorIndex))
  {
    Result.Reserve(Actors->Num());
    for (const TWeakObjectPtr<AActor> &ActorPtr : *Actors)
    {
      if (AActor *Actor = ActorPtr.Get())
      {
        Result.Add(Actor);
      }
    }
  }
  return Result;
}

void UBuildingFloorComponent::SetFloorCeilingHidden(
    int32 FloorIndex,
    bool bHidden)
{
  /*
   * 先使用楼层 Actor 自己维护的 FloorCeilingSet。
   * 这一步覆盖单层仓库等业务直接登记的 TH Actor。
   */
  if (UBuildingRuntimeNode *FloorNode = FindFloorWithIndex(FloorIndex))
  {
    TrySetFloorCeilingHidden(FloorNode->RuntimeActor, bHidden);
  }

  // 保留插件原有按 THRule 扫描的缓存，兼容非 ABuildingFloor 数据源。
  SetNamedActorsHidden(FloorCeilingActorMap, FloorIndex, bHidden);
}

FBuildingFocusTarget UBuildingFloorComponent::BuildFocusTarget(
    const TArray<UBuildingRuntimeNode *> &FocusRoots,
    AActor *FallbackActor) const
{
  if (!BuildingRuntimeData.BuildingActor)
  {
    return FBuildingFocusTarget();
  }

  // 先把 Runtime 子树展开成 Actor 列表，再走通用 Bounds 汇总。
  TArray<AActor *> FocusActors;
  for (UBuildingRuntimeNode *FocusRoot : FocusRoots)
  {
    if (!FocusRoot)
    {
      continue;
    }

    FocusRoot->ForEachNode(
        [&FocusActors](UBuildingRuntimeNode *Node)
        {
          if (!Node)
          {
            return;
          }

          if (IsValid(Node->RuntimeActor))
          {
            FocusActors.Add(Node->RuntimeActor.Get());
          }

          for (AActor *ControlledActor : Node->ControlledActors)
          {
            if (IsValid(ControlledActor))
            {
              FocusActors.Add(ControlledActor);
            }
          }
        });
  }

  return BuildFocusTargetFromActors(FocusActors, FallbackActor);
}

FBuildingFocusTarget UBuildingFloorComponent::BuildFocusTargetFromActors(
    const TArray<AActor *> &InActors,
    AActor *FallbackActor) const
{
  FBuildingFocusTarget Result;
  if (!BuildingRuntimeData.BuildingActor)
  {
    return Result;
  }

  const FTransform BuildingTransform =
      BuildingRuntimeData.BuildingActor->GetActorTransform();
  FBox WorldBounds(ForceInit);
  FBox LocalBounds(ForceInit);
  TSet<TWeakObjectPtr<AActor>> ProcessedActors;

  auto AddWorldBox = [&WorldBounds, &LocalBounds, &BuildingTransform](
                         const FBox &Box)
  {
    if (!Box.IsValid || Box.GetExtent().IsNearlyZero(UE_KINDA_SMALL_NUMBER))
    {
      return;
    }

    WorldBounds += Box;
    const FVector Origin = Box.GetCenter();
    const FVector Extent = Box.GetExtent();

    for (int32 X = -1; X <= 1; X += 2)
    {
      for (int32 Y = -1; Y <= 1; Y += 2)
      {
        for (int32 Z = -1; Z <= 1; Z += 2)
        {
          const FVector Corner = Origin + FVector(
                                              Extent.X * static_cast<float>(X),
                                              Extent.Y * static_cast<float>(Y),
                                              Extent.Z * static_cast<float>(Z));
          LocalBounds +=
              BuildingTransform.InverseTransformPosition(Corner);
        }
      }
    }
  };

  auto AddActorBounds = [&ProcessedActors, &AddWorldBox](AActor *Actor)
  {
    if (!IsValid(Actor))
    {
      return;
    }

    const TWeakObjectPtr<AActor> ActorKey(Actor);
    if (ProcessedActors.Contains(ActorKey))
    {
      return;
    }
    ProcessedActors.Add(ActorKey);

    FVector Origin = FVector::ZeroVector;
    FVector Extent = FVector::ZeroVector;
    Actor->GetActorBounds(false, Origin, Extent);
    if (Extent.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
    {
      return;
    }

    AddWorldBox(FBox(Origin - Extent, Origin + Extent));
  };

  auto AddActorTreeBounds = [&AddActorBounds](AActor *RootActor)
  {
    if (!IsValid(RootActor))
    {
      return;
    }

    AddActorBounds(RootActor);

    // Space/SpaceStruct 经常只是空 SceneComponent，真正的 StaticMeshActor
    // 位于附加子树，因此聚焦 Bounds 必须递归收集全部附属 Actor。
    TArray<AActor *> AttachedActors;
    RootActor->GetAttachedActors(AttachedActors, true, true);
    for (AActor *AttachedActor : AttachedActors)
    {
      AddActorBounds(AttachedActor);
    }
  };

  for (AActor *FocusActor : InActors)
  {
    AddActorTreeBounds(FocusActor);
  }

  if (!WorldBounds.IsValid)
  {
    AddActorTreeBounds(FallbackActor);
  }

  if (!WorldBounds.IsValid)
  {
    // 最终安全兜底：即使数据源只提供空 Actor，也返回可聚焦目标。
    // 最小 Bounds 仅用于建立有效中心，最终距离仍会受 MinDistance 限制。
    AActor *AnchorActor = IsValid(FallbackActor)
                              ? FallbackActor
                              : BuildingRuntimeData.BuildingActor.Get();
    if (!IsValid(AnchorActor))
    {
      return Result;
    }

    constexpr float SafeExtent = 50.f;
    const FVector Center = AnchorActor->GetActorLocation();
    AddWorldBox(FBox(
        Center - FVector(SafeExtent),
        Center + FVector(SafeExtent)));

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("Building focus target has no render bounds; using safe bounds at %s for %s."),
        *Center.ToString(),
        *GetNameSafe(AnchorActor));
  }

  const FVector LocalSize = LocalBounds.IsValid
                                ? LocalBounds.GetSize()
                                : WorldBounds.GetSize();
  const bool bLongAxisIsX = LocalSize.X >= LocalSize.Y;

  FVector ForwardAxis = BuildingTransform.TransformVectorNoScale(
                                             FVector::ForwardVector)
                            .GetSafeNormal2D();
  FVector RightAxis = BuildingTransform.TransformVectorNoScale(
                                           FVector::RightVector)
                          .GetSafeNormal2D();
  if (ForwardAxis.IsNearlyZero())
  {
    ForwardAxis = FVector::ForwardVector;
  }
  if (RightAxis.IsNearlyZero())
  {
    RightAxis = FVector::RightVector;
  }

  Result.bIsValid = true;
  Result.Center = WorldBounds.GetCenter();
  Result.Extent = WorldBounds.GetExtent();
  Result.LongAxis = bLongAxisIsX ? ForwardAxis : RightAxis;
  Result.ShortAxis = bLongAxisIsX ? RightAxis : ForwardAxis;
  return Result;
}
// ============================================================
// Drawer
// ============================================================
// 抽出
void UBuildingFloorComponent::LayeringDisplay()
{
  if (!BuildingRuntimeData.Config.bCanDisassemble)
  {
    return;
  }

  if (BuildingRuntimeData.Config.DisassembleMode != EDisassembleMode::Drawer)
  {
    return;
  }

  if (FloorNodes.Num() == 0)
  {
    return;
  }

  if (bIsLayering && !bLayeringRunning)
  {
    return;
  }

  if (bLayeringRunning && LayeringTimeline.IsReversing())
  {
    LayeringTimeline.Play();
    return;
  }

  PlayLayeringTimeline(true);
}
// 还原抽出
void UBuildingFloorComponent::BackToNormal()
{
  if (BuildingRuntimeData.Config.DisassembleMode != EDisassembleMode::Drawer)
  {
    return;
  }

  RestoreAllFloorCeilings();

  if (!bIsLayering && !bLayeringRunning)
  {
    return;
  }

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  for (TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
  {
    if (!NodePtr)
    {
      continue;
    }

    NodePtr->CurrentOffset = FVector::ZeroVector;
    NodePtr->TargetOffset = FVector::ZeroVector;
    NodePtr->bExpanded = false;
  }

  PlayLayeringTimeline(false);
}
FVector UBuildingFloorComponent::GetPullDirection() const
{
  FVector Direction = FVector::ZeroVector;

  switch (BuildingRuntimeData.Config.Drawer.PullDirection)
  {
  case EPullDirection::Right:
    Direction = FVector(1.f, 0.f, 0.f);
    break;

  case EPullDirection::Left:
    Direction = FVector(-1.f, 0.f, 0.f);
    break;

  case EPullDirection::Forward:
    Direction = FVector(0.f, 1.f, 0.f);
    break;

  case EPullDirection::Backward:
    Direction = FVector(0.f, -1.f, 0.f);
    break;

  case EPullDirection::Auto:
    Direction = MaxFloorSizeX >= MaxFloorSizeY
                    ? FVector(1.f, 0.f, 0.f)
                    : FVector(0.f, 1.f, 0.f);
    break;

  case EPullDirection::None:
  default:
    Direction = FVector::ZeroVector;
    break;
  }

  if (BuildingRuntimeData.BuildingActor)
  {
    Direction =
        BuildingRuntimeData.BuildingActor->GetActorTransform()
            .TransformVectorNoScale(Direction);
  }

  if (!Direction.IsNearlyZero())
  {
    Direction.Normalize();
  }

  return Direction;
}

void UBuildingFloorComponent::ToggleFloor(int32 Floor)
{
  if (!BuildingRuntimeData.Config.bCanDisassemble)
  {
    return;
  }

  const EDisassembleMode Mode = BuildingRuntimeData.Config.DisassembleMode;

  // 只有抽屉和上下分离 能进来
  if (Mode != EDisassembleMode::Drawer &&
      Mode != EDisassembleMode::LiftDrop)
  {
    return;
  }

  // 只有 Drawer 必须先完成整栋分层。
  if (Mode == EDisassembleMode::Drawer && !bIsLayering)
  {
    return;
  }

  if (bLayeringRunning || bExpandRunning)
  {
    return;
  }

  UBuildingRuntimeNode *Node = FindFloorWithIndex(Floor);
  if (!Node || !Node->bCanDisassemble)
  {
    return;
  }

  ToggleNode(Node);
}

void UBuildingFloorComponent::ToggleNode(UBuildingRuntimeNode *Node)
{
  if (!Node)
  {
    return;
  }

  if (!Node->bCanDisassemble)
  {
    return;
  }

  switch (BuildingRuntimeData.Config.DisassembleMode)
  {
  case EDisassembleMode::Drawer:
    DrawerNode(Node);
    break;

  case EDisassembleMode::Visibility:
    if (Node->FloorIndex != BUILDING_INVALID_FLOOR_INDEX)
    {
      ToggleFloorVisibility(Node->FloorIndex);
    }
    break;

  case EDisassembleMode::LiftDrop:

    break;

  default:
    break;
  }
}
void UBuildingFloorComponent::DrawerNode(
    UBuildingRuntimeNode *Node)
{
  if (!Node || !Node->RuntimeActor)
  {
    return;
  }
  if (!Node->bCanDisassemble)
  {
    return;
  }
  if (Node->FloorIndex == BUILDING_INVALID_FLOOR_INDEX)
  {
    return;
  }

  ExpandStartOffsetMap.Empty();

  for (TObjectPtr<UBuildingRuntimeNode> &FloorNodePtr : FloorNodes)
  {
    if (FloorNodePtr)
    {
      ExpandStartOffsetMap.Add(FloorNodePtr.Get(), FloorNodePtr->CurrentOffset);
    }
  }

  if (ActiveFloor == Node->FloorIndex)
  {
    SetFloorCeilingHidden(Node->FloorIndex, false);
    Node->TargetOffset = FVector::ZeroVector;
    Node->bExpanded = false;
    ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;
  }
  else
  {
    UBuildingRuntimeNode *OldNode = FindFloorWithIndex(ActiveFloor);
    if (OldNode)
    {
      SetFloorCeilingHidden(OldNode->FloorIndex, false);
      OldNode->TargetOffset = FVector::ZeroVector;
      OldNode->bExpanded = false;
    }

    ActiveFloor = Node->FloorIndex;

    if (BuildingRuntimeData.Config.Name.bHideCurrentFloorCeiling)
    {
      SetFloorCeilingHidden(Node->FloorIndex, true);
    }

    Node->TargetOffset = GetPullDirection() * GetPullDistance(Node);
    Node->bExpanded = true;
  }

  PlayFloorExpandTimeline();
}

void UBuildingFloorComponent::SetNodeHighlight(
    UBuildingRuntimeNode *Node,
    bool bHighlight)
{
  if (!Node || !Node->RuntimeActor)
  {
    return;
  }

  TArray<UPrimitiveComponent *> PrimComps;
  Node->RuntimeActor->GetComponents<UPrimitiveComponent>(PrimComps);

  for (UPrimitiveComponent *Prim : PrimComps)
  {
    if (!Prim)
    {
      continue;
    }

    uint8 StencilValue =
        static_cast<uint8>(Prim->CustomDepthStencilValue);
    if (bHighlight)
    {
      StencilValue |= DTSBuildingStencil::Hover;
    }
    else
    {
      StencilValue &= ~DTSBuildingStencil::Hover;
    }

    Prim->SetCustomDepthStencilValue(StencilValue);
    Prim->SetRenderCustomDepth(StencilValue != 0);
  }
}
bool UBuildingFloorComponent::IsLiftDropDisassembleMode() const
{
  return BuildingRuntimeData.Config.bCanDisassemble &&
         BuildingRuntimeData.Config.DisassembleMode ==
             EDisassembleMode::LiftDrop;
}
void UBuildingFloorComponent::EnterLiftDropDisassemble()
{
  if (!IsLiftDropDisassembleMode())
  {
    return;
  }

  LiftDropTimeline.Stop();

  bLiftDropRunning = false;
  LiftDropTransition = ELiftDropTransition::None;

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  // 进入楼栋时先恢复所有楼层。
  // 等镜头聚焦完成后，用户再选择具体楼层。
  for (const TObjectPtr<UBuildingRuntimeNode> &NodePtr :
       FloorNodes)
  {
    UBuildingRuntimeNode *Node = NodePtr.Get();

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    Node->RuntimeActor->SetActorLocation(Node->OriginalTransform.GetLocation());

    Node->CurrentTransform = Node->OriginalTransform;

    Node->CurrentOffset = FVector::ZeroVector;

    Node->TargetOffset = FVector::ZeroVector;

    Node->bExpanded = false;

    ApplyFloorVisibility(Node->FloorIndex, true);
  }

  RestoreAllFloorCeilings();
}

bool UBuildingFloorComponent::SelectLiftDropFloor(
    int32 TargetFloor)
{
  if (!IsLiftDropDisassembleMode() || bLiftDropRunning)
  {
    return false;
  }

  UBuildingRuntimeNode *TargetNode = FindFloorWithIndex(TargetFloor);

  if (!TargetNode ||
      !TargetNode->RuntimeActor ||
      !TargetNode->bCanDisassemble)
  {
    return false;
  }

  // 重复选择当前楼层时不做处理。
  if (ActiveFloor == TargetFloor)
  {
    return false;
  }

  const int32 PreviousFloor = ActiveFloor;

  const bool bFirstSelection = PreviousFloor == BUILDING_INVALID_FLOOR_INDEX;

  const bool bMovingToHigherFloor = bFirstSelection || TargetFloor > PreviousFloor;

  LiftDropTimeline.Stop();

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  const float DropStartHeight = BuildingRuntimeData.Config.LiftDrop.DropStartHeight;

  const float LiftOutHeight = BuildingRuntimeData.Config.LiftDrop.LiftOutHeight;

  if (bMovingToHigherFloor)
  {
    /*
     * 例如第一次选择 6F，或者从 3F 切到 6F：
     *
     * 1～5F：显示并保持原位
     * 6F：从高处落回原位
     * 7F以上：隐藏
     */
    LiftDropTransition =
        ELiftDropTransition::DropIn;

    for (const TObjectPtr<UBuildingRuntimeNode> &NodePtr :
         FloorNodes)
    {
      UBuildingRuntimeNode *Node = NodePtr.Get();

      if (!Node || !Node->RuntimeActor)
      {
        continue;
      }

      const int32 FloorIndex = Node->FloorIndex;

      const FVector OriginalLocation = Node->OriginalTransform.GetLocation();

      Node->CurrentOffset = FVector::ZeroVector;

      Node->TargetOffset = FVector::ZeroVector;

      if (FloorIndex < TargetFloor)
      {
        // 目标层以下：显示、原位、不播放动画。
        Node->RuntimeActor->SetActorLocation(OriginalLocation);

        ApplyFloorVisibility(FloorIndex, true);
      }
      else if (FloorIndex == TargetFloor)
      {
        const FVector StartLocation =
            OriginalLocation +
            FVector::UpVector *
                DropStartHeight;

        // 先设置到高处，再显示，避免闪一下原始位置。
        Node->RuntimeActor->SetActorLocation(
            StartLocation);

        ApplyFloorVisibility(
            FloorIndex,
            true);

        LiftDropStartLocationMap.Add(
            Node,
            StartLocation);

        LiftDropTargetLocationMap.Add(
            Node,
            OriginalLocation);
      }
      else
      {
        // 目标楼层以上全部隐藏并重置到原位。
        ApplyFloorVisibility(
            FloorIndex,
            false);

        Node->RuntimeActor->SetActorLocation(
            OriginalLocation);
      }
    }
  }
  else
  {
    /*
     * 例如从 6F 切换到 3F：
     *
     * 1～3F：显示并保持原位
     * 4～6F：向上抬升，动画完成后隐藏
     * 7F以上：继续隐藏
     */
    LiftDropTransition =
        ELiftDropTransition::LiftOut;

    for (const TObjectPtr<UBuildingRuntimeNode> &NodePtr :
         FloorNodes)
    {
      UBuildingRuntimeNode *Node = NodePtr.Get();

      if (!Node || !Node->RuntimeActor)
      {
        continue;
      }

      const int32 FloorIndex =
          Node->FloorIndex;

      const FVector OriginalLocation =
          Node->OriginalTransform.GetLocation();

      Node->CurrentOffset =
          FVector::ZeroVector;

      Node->TargetOffset =
          FVector::ZeroVector;

      if (FloorIndex <= TargetFloor)
      {
        // 新目标楼层及以下保持显示和原位。
        Node->RuntimeActor->SetActorLocation(
            OriginalLocation);

        ApplyFloorVisibility(
            FloorIndex,
            true);
      }
      else if (FloorIndex <= PreviousFloor)
      {
        /*
         * 这些是上一次还显示、现在需要退出的楼层。
         * 从原位向上抬升，结束后再隐藏。
         */
        const FVector LiftTargetLocation =
            OriginalLocation +
            FVector::UpVector *
                LiftOutHeight;

        Node->RuntimeActor->SetActorLocation(
            OriginalLocation);

        ApplyFloorVisibility(
            FloorIndex,
            true);

        LiftDropStartLocationMap.Add(
            Node,
            OriginalLocation);

        LiftDropTargetLocationMap.Add(
            Node,
            LiftTargetLocation);

        LiftDropHideOnFinished.Add(Node);
      }
      else
      {
        // 原来就隐藏的更高楼层继续保持隐藏。
        ApplyFloorVisibility(
            FloorIndex,
            false);

        Node->RuntimeActor->SetActorLocation(
            OriginalLocation);
      }
    }
  }

  ActiveFloor = TargetFloor;

  if (BuildingRuntimeData.Config.Name
          .bHideCurrentFloorCeiling)
  {
    RestoreAllFloorCeilings();

    SetFloorCeilingHidden(
        TargetFloor,
        true);
  }

  PlayLiftDropTimeline();

  return true;
}
void UBuildingFloorComponent::ExitLiftDropDisassemble()
{
  LiftDropTimeline.Stop();

  bLiftDropRunning = false;
  LiftDropTransition = ELiftDropTransition::None;

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  for (const TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *Node = NodePtr.Get();

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    Node->RuntimeActor->SetActorLocation(Node->OriginalTransform.GetLocation());

    Node->CurrentTransform = Node->OriginalTransform;

    Node->CurrentOffset = FVector::ZeroVector;

    Node->TargetOffset = FVector::ZeroVector;

    Node->bExpanded = false;

    ApplyFloorVisibility(Node->FloorIndex, true);
  }

  RestoreAllFloorCeilings();

  if (!bLayeringRunning && !bExpandRunning)
  {
    SetComponentTickEnabled(false);
  }
}
// 抽屉动画更新/结束

void UBuildingFloorComponent::OnLayeringUpdate(float Alpha)
{
  const int32 FloorCount = FloorNodes.Num();
  // 如果配置中有设置最大高度，则使用配置的最大高度，否则使用计算得到的总高度
  const float BaseHeight =
      BuildingRuntimeData.Config.Drawer.CampusMaxBuildingHeight > 0.f
          ? BuildingRuntimeData.Config.Drawer.CampusMaxBuildingHeight
          : TotalBuildingHeight;
  // 如果楼层数量大于1，则计算每层的高度间隔，否则间隔为0
  const float SpreadStep =
      FloorCount > 1
          ? (TotalBuildingHeight / static_cast<float>(FloorCount - 1)) *
                BuildingRuntimeData.Config.Drawer.SpreadScale
          : 0.f;

  for (int32 i = 0; i < FloorNodes.Num(); ++i)
  {
    UBuildingRuntimeNode *Node = FloorNodes[i].Get();
    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    const FVector StartLocation =
        Node->OriginalTransform.GetLocation();

    FVector TargetLocation = StartLocation;
    TargetLocation.Z =
        StartLocation.Z + BaseHeight + static_cast<float>(i) * SpreadStep;

    const FVector NewLocation =
        FMath::Lerp(StartLocation, TargetLocation, Alpha);

    Node->RuntimeActor->SetActorLocation(NewLocation);
    Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();
  }
}

void UBuildingFloorComponent::OnLayeringFinished()
{
  bLayeringRunning = false;
  bDrawerRunning = false;

  const float Pos = LayeringTimeline.GetPlaybackPosition();
  const float Len = LayeringTimeline.GetTimelineLength();

  if (Pos <= KINDA_SMALL_NUMBER)
  {
    bIsLayering = false;
    ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;
    LayeredBaseLocationMap.Empty();

    for (TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
    {
      if (!NodePtr)
      {
        continue;
      }

      NodePtr->CurrentOffset = FVector::ZeroVector;
      NodePtr->TargetOffset = FVector::ZeroVector;
      NodePtr->bExpanded = false;
    }
  }
  else if (Pos >= Len - KINDA_SMALL_NUMBER || Len <= KINDA_SMALL_NUMBER)
  {
    bIsLayering = true;
    LayeredBaseLocationMap.Empty();

    for (TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
    {
      if (!NodePtr || !NodePtr->RuntimeActor)
      {
        continue;
      }

      LayeredBaseLocationMap.Add(
          NodePtr.Get(),
          NodePtr->RuntimeActor->GetActorLocation());

      NodePtr->CurrentOffset = FVector::ZeroVector;
      NodePtr->TargetOffset = FVector::ZeroVector;
    }
  }

  OnLayeringTimeLineFinished.Broadcast();

  if (!bLayeringRunning && !bExpandRunning)
  {
    SetComponentTickEnabled(false);
  }
}

// 楼层展开更新/结束
void UBuildingFloorComponent::OnFloorExpandUpdate(float Alpha)
{
  for (TObjectPtr<UBuildingRuntimeNode> &NodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *Node = NodePtr.Get();

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    const FVector StartOffset =
        ExpandStartOffsetMap.Contains(Node)
            ? ExpandStartOffsetMap[Node]
            : Node->CurrentOffset;

    const FVector TargetOffset = Node->TargetOffset;

    const FVector NewOffset = FMath::Lerp(StartOffset, TargetOffset, Alpha);

    FVector BaseLocation = Node->OriginalTransform.GetLocation();

    /*
     * 如果已经分层，就用分层后的楼层位置作为基准。
     * 如果没有分层，就直接用原始位置作为基准。
     */
    if (const FVector *LayeredBaseLocation = LayeredBaseLocationMap.Find(Node))
    {
      BaseLocation = *LayeredBaseLocation;
    }

    const FVector NewLocation = BaseLocation + NewOffset;

    /*
     * 这里就是直接移动楼层 Space。
     * 比如抽 FSSJQ，就移动 FSSJQ 这个 Space。
     * 抽 1F，就移动 1F 这个 Space。
     */
    Node->RuntimeActor->SetActorLocation(NewLocation);

    Node->CurrentOffset = NewOffset;
    Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();
  }
}

void UBuildingFloorComponent::OnFloorExpandFinished()
{
  bExpandRunning = false;
  bDrawerRunning = false;
  // 楼层展开完毕之后会发送通知事件
  OnFloorExpandTimelineFinished.Broadcast();

  if (!bLayeringRunning && !bExpandRunning)
  {
    SetComponentTickEnabled(false);
  }
}
// 通用显隐会同时处理楼栋模型和设备
void UBuildingFloorComponent::SetNodeTreeVisibility(
    UBuildingRuntimeNode *Node,
    bool bVisible)
{
  if (!Node)
  {
    return;
  }

  Node->bHidden = !bVisible;

  // 楼栋、楼层以及附加模型
  if (Node->RuntimeActor)
  {
    SetActorTreeHidden(Node->RuntimeActor.Get(), !bVisible);
  }
  // 设备、摄像头、管线等
  for (TObjectPtr<AActor> &ControlledActor : Node->ControlledActors)
  {
    if (ControlledActor)
    {
      SetActorTreeHidden(ControlledActor.Get(), !bVisible);
    }
  }
  // 继续处理子楼层
  for (TObjectPtr<UBuildingRuntimeNode> &Child : Node->Children)
  {
    if (Child)
    {
      SetNodeTreeVisibility(Child.Get(), bVisible);
    }
  }
}

void UBuildingFloorComponent::SetNodeStructuralTreeVisibility(
    UBuildingRuntimeNode *Node,
    bool bVisible)
{
  if (!Node)
  {
    return;
  }

  Node->bHidden = !bVisible;

  /*
   * 可见性拆楼专用逻辑：
   * Space 只处理 OrdinaryActors，设备等 SpaceStruct 保持原状态。
   */
  if (ASpace *Space = Cast<ASpace>(Node->RuntimeActor.Get()))
  {
    // Space 方法会递归处理普通容器 Actor 下挂接的模型。
    Space->SetOrdinaryActorsHidden(!bVisible);

    // 楼层业务方法额外补充 TH 天花和 WQ 外墙。
    TrySetFloorMeshHidden(Node->RuntimeActor.Get(), !bVisible);
  }
  else
  {
    // 非 Space 数据源无法区分结构模型与设备，保持原通用行为。
    if (Node->RuntimeActor)
    {
      SetActorTreeHidden(Node->RuntimeActor.Get(), !bVisible);
    }

    for (TObjectPtr<AActor> &ControlledActor : Node->ControlledActors)
    {
      if (ControlledActor)
      {
        SetActorTreeHidden(ControlledActor.Get(), !bVisible);
      }
    }
  }

  for (TObjectPtr<UBuildingRuntimeNode> &Child : Node->Children)
  {
    // 楼层拆楼显隐 使用ASpace的OrdinaryActors只处理模型。
    if (Child)
    {
      SetNodeStructuralTreeVisibility(Child.Get(), bVisible);
    }
  }
}

void UBuildingFloorComponent::SetNodeIsolationContentVisibility(
    UBuildingRuntimeNode *Node,
    bool bVisible)
{
  if (!Node)
  {
    return;
  }

  Node->bHidden = !bVisible;

  if (ASpace *Space = Cast<ASpace>(Node->RuntimeActor.Get()))
  {
    // 递归恢复普通容器 Actor 以及它下挂接的模型。
    Space->SetOrdinaryActorsHidden(!bVisible);

    // 楼层业务方法额外补充 TH 天花和 WQ 外墙。
    TrySetFloorMeshHidden(Node->RuntimeActor.Get(), !bVisible);

    // SpaceStruct 已由 Adapter 放入 ControlledActors，设备等结构正常恢复。
    for (TObjectPtr<AActor> &ControlledActor : Node->ControlledActors)
    {
      if (ControlledActor)
      {
        SetActorTreeHidden(ControlledActor.Get(), !bVisible);
      }
    }
  }
  else
  {
    // 非 Space 数据源没有分类信息，只能保持通用显隐行为。
    if (Node->RuntimeActor)
    {
      SetActorTreeHidden(Node->RuntimeActor.Get(), !bVisible);
    }

    for (TObjectPtr<AActor> &ControlledActor : Node->ControlledActors)
    {
      if (ControlledActor)
      {
        SetActorTreeHidden(ControlledActor.Get(), !bVisible);
      }
    }
  }

  for (TObjectPtr<UBuildingRuntimeNode> &Child : Node->Children)
  {
    if (Child)
    {
      SetNodeIsolationContentVisibility(Child.Get(), bVisible);
    }
  }
}

void UBuildingFloorComponent::SetActorTreeHidden(
    AActor *Actor,
    bool bHidden)
{
  if (!Actor)
  {
    return;
  }

  // 隐藏当前 Actor
  Actor->SetActorHiddenInGame(bHidden);

  /*
   * 继续隐藏它下面挂接的所有 Actor。
   * 你的楼层 Space 下面通常还有：
   *
   * Floor Space
   *   └── 普通 Actor
   *       └── StaticMeshActor
   */
  TArray<AActor *> AttachedActors;
  Actor->GetAttachedActors(
      AttachedActors,
      true,
      true);

  for (AActor *ChildActor : AttachedActors)
  {
    if (!ChildActor)
    {
      continue;
    }

    ChildActor->SetActorHiddenInGame(bHidden);
  }
}

void UBuildingFloorComponent::PlayLiftDropTimeline()
{
  if (LiftDropStartLocationMap.Num() == 0)
  {
    OnLiftDropFinished();
    return;
  }

  if (LiftDropTimeline.GetTimelineLength() <= KINDA_SMALL_NUMBER)
  {
    OnLiftDropUpdate(1.f);
    OnLiftDropFinished();
    return;
  }

  bLiftDropRunning = true;

  SetComponentTickEnabled(true);

  LiftDropTimeline.PlayFromStart();
}

void UBuildingFloorComponent::OnLiftDropUpdate(float Alpha)
{
  for (const TPair<UBuildingRuntimeNode *, FVector> &Pair : LiftDropStartLocationMap)
  {
    UBuildingRuntimeNode *Node = Pair.Key;

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    const FVector *TargetLocation = LiftDropTargetLocationMap.Find(Node);

    if (!TargetLocation)
    {
      continue;
    }

    const FVector NewLocation = FMath::Lerp(Pair.Value, *TargetLocation, Alpha);

    Node->RuntimeActor->SetActorLocation(NewLocation);

    Node->CurrentOffset = NewLocation - Node->OriginalTransform.GetLocation();

    Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();
  }
}

void UBuildingFloorComponent::OnLiftDropFinished()
{
  /*
   * 向下切换楼层时，把已经抬升出去的楼层隐藏，
   * 并在隐藏状态下重置到原始位置。
   */
  for (UBuildingRuntimeNode *Node : LiftDropHideOnFinished)
  {
    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    ApplyFloorVisibility(Node->FloorIndex, false);

    Node->RuntimeActor->SetActorLocation(Node->OriginalTransform.GetLocation());

    Node->CurrentTransform = Node->OriginalTransform;

    Node->CurrentOffset = FVector::ZeroVector;

    Node->TargetOffset = FVector::ZeroVector;
  }

  // 掉落楼层落地后也清空偏移状态。
  for (const TPair<UBuildingRuntimeNode *, FVector> &Pair : LiftDropTargetLocationMap)
  {
    UBuildingRuntimeNode *Node = Pair.Key;

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    if (!LiftDropHideOnFinished.Contains(Node))
    {
      Node->RuntimeActor->SetActorLocation(Pair.Value);

      Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();

      Node->CurrentOffset = FVector::ZeroVector;

      Node->TargetOffset = FVector::ZeroVector;
    }
  }

  bLiftDropRunning = false;
  LiftDropTransition = ELiftDropTransition::None;

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  OnFloorExpandTimelineFinished.Broadcast();

  if (!bLayeringRunning &&
      !bExpandRunning &&
      !bLiftDropRunning)
  {
    SetComponentTickEnabled(false);
  }
}

float UBuildingFloorComponent::GetPullDistance(UBuildingRuntimeNode *Node) const
{
  if (!Node)
  {
    return 0.f;
  }

  float BaseDistance = 0.f;

  switch (BuildingRuntimeData.Config.Drawer.PullDirection)
  {
  case EPullDirection::Right:
  case EPullDirection::Left:
    BaseDistance = MaxFloorSizeX;
    break;

  case EPullDirection::Forward:
  case EPullDirection::Backward:
    BaseDistance = MaxFloorSizeY;
    break;

  case EPullDirection::Auto:
    BaseDistance = MaxFloorSizeX >= MaxFloorSizeY
                       ? MaxFloorSizeX
                       : MaxFloorSizeY;
    break;

  case EPullDirection::None:
  default:
    BaseDistance = 0.f;
    break;
  }

  float Scale = BuildingRuntimeData.Config.Drawer.PullDistanceScale;

  if (const float *OverrideScale =
          BuildingRuntimeData.Config.Drawer.FloorPullScaleOverride.Find(Node->FloorIndex))
  {
    Scale = *OverrideScale;
  }

  return BaseDistance * Scale;
}

void UBuildingFloorComponent::PlayFloorExpandTimeline()
{
  if (FloorExpandTimeline.GetTimelineLength() <= KINDA_SMALL_NUMBER)
  {
    OnFloorExpandUpdate(1.f);
    OnFloorExpandFinished();
    return;
  }

  bExpandRunning = true;
  bDrawerRunning = true;
  SetComponentTickEnabled(true);

  FloorExpandTimeline.PlayFromStart();
}

void UBuildingFloorComponent::PlayLayeringTimeline(bool bForward)
{
  if (LayeringTimeline.GetTimelineLength() <= KINDA_SMALL_NUMBER)
  {
    OnLayeringUpdate(bForward ? 1.f : 0.f);
    OnLayeringFinished();
    return;
  }

  bLayeringRunning = true;
  bDrawerRunning = true;
  SetComponentTickEnabled(true);

  if (bForward)
  {
    LayeringTimeline.PlayFromStart();
  }
  else
  {
    LayeringTimeline.ReverseFromEnd();
  }
}

// ============================================================
// Visibility
// ============================================================
void UBuildingFloorComponent::VisibilityNode(
    UBuildingRuntimeNode *Node)
{
  if (!Node)
  {
    return;
  }
  if (!Node->bCanDisassemble)
  {
    return;
  }

  const bool bNewVisible = Node->bHidden;

  ApplyNodeVisibility(Node, bNewVisible);
}

void UBuildingFloorComponent::ApplyNodeVisibility(
    UBuildingRuntimeNode *Node,
    bool bVisible)
{
  SetNodeStructuralTreeVisibility(Node, bVisible);
}

void UBuildingFloorComponent::ApplyFloorVisibility(int32 Floor, bool bVisible)
{
  UBuildingRuntimeNode *Node = FindFloorWithIndex(Floor);
  if (!Node)
  {
    return;
  }

  ApplyNodeVisibility(Node, bVisible);

  if (bVisible)
  {
    HiddenFloors.Remove(Floor);
  }
  else
  {
    HiddenFloors.Add(Floor);
  }

  OnFloorVisibilityChanged.Broadcast(Floor, bVisible);
}

void UBuildingFloorComponent::ToggleFloorVisibility(int32 Floor)
{
  if (!BuildingRuntimeData.Config.bCanDisassemble)
  {
    return;
  }

  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    const int32 FloorIndex = Pair.Key;

    bool bVisible = true;

    switch (BuildingRuntimeData.Config.Visibility.FloorDisplayMode)
    {
    case EVisibilityFloorDisplayMode::ShowCurrentAndBelow:
      bVisible = FloorIndex <= Floor;
      break;

    case EVisibilityFloorDisplayMode::ShowOnlyCurrent:
      bVisible = FloorIndex == Floor;
      break;

    case EVisibilityFloorDisplayMode::ShowCurrentAndAbove:
      bVisible = FloorIndex >= Floor;
      break;

    default:
      bVisible = FloorIndex <= Floor;
      break;
    }

    ApplyFloorVisibility(FloorIndex, bVisible);
  }

  if (BuildingRuntimeData.Config.Name.bHideCurrentFloorCeiling)
  {
    SetFloorCeilingHidden(Floor, true);
  }
}

void UBuildingFloorComponent::ShowOnlyFloor(int32 Floor)
{
  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, Pair.Key == Floor);
  }
}

void UBuildingFloorComponent::ShowFloorRange(int32 MinFloor, int32 MaxFloor)
{
  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    const int32 FloorIndex = Pair.Key;
    const bool bVisible = FloorIndex >= MinFloor && FloorIndex <= MaxFloor;

    ApplyFloorVisibility(FloorIndex, bVisible);
  }
}

void UBuildingFloorComponent::ShowAll()
{
  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, true);
  }
}

void UBuildingFloorComponent::HideAbove(int32 Floor)
{
  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    if (Pair.Key > Floor)
    {
      ApplyFloorVisibility(Pair.Key, false);
    }
  }
}

void UBuildingFloorComponent::SetAllFloorsVisibility(bool bVisible)
{
  for (const TPair<int32, TObjectPtr<UBuildingRuntimeNode>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, bVisible);
  }
}

bool UBuildingFloorComponent::IsFloorVisible(int32 Floor) const
{
  return !HiddenFloors.Contains(Floor);
}

// ============================================================
// Query
// ============================================================

UBuildingRuntimeNode *UBuildingFloorComponent::FindFloorWithIndex(
    int32 FloorIndex) const
{
  if (const TObjectPtr<UBuildingRuntimeNode> *Ptr =
          FloorMap.Find(FloorIndex))
  {
    return Ptr->Get();
  }

  return nullptr;
}

UBuildingRuntimeNode *UBuildingFloorComponent::FindFloorWithId(
    const FString &FloorId) const
{
  const FString Target = FloorId.TrimStartAndEnd();
  if (Target.IsEmpty())
  {
    return nullptr;
  }

  auto Matches = [&Target](const FString &Candidate)
  {
    const FString Value = Candidate.TrimStartAndEnd();
    return Value.Equals(Target, ESearchCase::IgnoreCase) ||
           Value.EndsWith(TEXT("_") + Target, ESearchCase::IgnoreCase) ||
           Value.EndsWith(TEXT("-") + Target, ESearchCase::IgnoreCase);
  };

  for (const TObjectPtr<UBuildingRuntimeNode> &FloorNodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *FloorNode = FloorNodePtr.Get();
    if (!FloorNode)
    {
      continue;
    }

    if (Matches(FloorNode->NodeId))
    {
      return FloorNode;
    }

    if (AActor *FloorActor = FloorNode->RuntimeActor.Get())
    {
      if (Matches(FloorActor->GetActorNameOrLabel()) ||
          Matches(FloorActor->GetName()))
      {
        return FloorNode;
      }
    }
  }

  // 有些业务设备使用 RF，但场景没有独立 RF 节点，沿用原逻辑映射到最高层。
  if (Target.Equals(TEXT("RF"), ESearchCase::IgnoreCase) ||
      Target.Equals(TEXT("ROOF"), ESearchCase::IgnoreCase))
  {
    return GetHighestFloorNode();
  }

  return nullptr;
}

UBuildingRuntimeNode *UBuildingFloorComponent::GetHighestFloorNode() const
{
  UBuildingRuntimeNode *Result = nullptr;
  for (const TObjectPtr<UBuildingRuntimeNode> &FloorNodePtr : FloorNodes)
  {
    UBuildingRuntimeNode *FloorNode = FloorNodePtr.Get();
    if (FloorNode &&
        (!Result || FloorNode->FloorIndex > Result->FloorIndex))
    {
      Result = FloorNode;
    }
  }
  return Result;
}

UBuildingRuntimeNode *UBuildingFloorComponent::FindNodeWithActor(
    AActor *Actor) const
{
  AActor *Current = Actor;

  while (Current)
  {
    TObjectPtr<AActor> Key = Current;

    if (const TObjectPtr<UBuildingRuntimeNode> *Ptr =
            ActorMap.Find(Key))
    {
      return Ptr->Get();
    }

    Current = Current->GetAttachParentActor();
  }

  return nullptr;
}
// ============================================================
// Rest
// ============================================================

void UBuildingFloorComponent::Reset()
{
  if (!BuildingRuntimeData.Root)
  {
    return;
  }

  LayeringTimeline.Stop();
  FloorExpandTimeline.Stop();

  bDrawerRunning = false;
  bVisibilityRunning = false;
  bIsLayering = false;
  bLayeringRunning = false;
  bExpandRunning = false;

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  LayeredBaseLocationMap.Empty();
  ExpandStartOffsetMap.Empty();
  HiddenFloors.Empty();
  SetComponentTickEnabled(false);

  BuildingRuntimeData.Root->ForEachNode(
      [](UBuildingRuntimeNode *Node)
      {
        if (!Node || !Node->RuntimeActor)
        {
          return;
        }

        Node->CurrentOffset = FVector::ZeroVector;
        Node->TargetOffset = FVector::ZeroVector;
        Node->bExpanded = false;
        Node->bHidden = false;

        Node->RuntimeActor->SetActorTransform(Node->OriginalTransform);
        Node->RuntimeActor->SetActorHiddenInGame(false);

        Node->CurrentTransform = Node->OriginalTransform;
      });

  // 重置拆楼只恢复结构模型；设备继续保持业务侧设置的显隐状态。
  SetNodeStructuralTreeVisibility(BuildingRuntimeData.Root.Get(), true);

  RestoreAllFloorCeilings();
}

void UBuildingFloorComponent::EnterVisibilityDisassemble()
{
  if (!BuildingRuntimeData.Config.bCanDisassemble)
  {
    return;
  }

  // 进入可见性拆楼时，目标楼栋只显示 Space 管理的
  // OrdinaryActors 和 Structs，AEffectActor 等排除对象保持隐藏。
  SetIsolationBuildingVisibility(true);

  if (BuildingRuntimeData.Config.Visibility.bShowAllFloorsOnEnter)
  {
    ShowAll();
  }
}

void UBuildingFloorComponent::ExitVisibilityDisassemble()
{
  SetWholeBuildingVisibility(true);
  ShowAll();
  RestoreAllFloorCeilings();
}

void UBuildingFloorComponent::SetWholeBuildingVisibility(
    bool bVisible)
{
  UBuildingRuntimeNode *Root =
      BuildingRuntimeData.Root.Get();

  if (!Root)
  {
    return;
  }

  if (bVisible)
  {
    /*
     * 拆楼退出/楼栋恢复时，只恢复组件明确管理的内容：
     * - Space 普通结构模型
     * - 楼层 TH 天花、WQ 外墙
     * - Adapter 收集到 ControlledActors 的设备
     *
     * 由 DistinguishAttachActor 排除的 Effect 等业务 Actor
     * 不属于拆楼模块，不能在这里被强制显示。
     */
    SetNodeIsolationContentVisibility(Root, true);
  }
  else
  {
    // 隐藏其他楼栋时仍需隐藏整棵 Actor 树，包含楼栋内设备和附属对象。
    SetNodeTreeVisibility(Root, false);
  }
}

void UBuildingFloorComponent::SetIsolationBuildingVisibility(
    bool bVisible)
{
  UBuildingRuntimeNode *Root = BuildingRuntimeData.Root.Get();
  if (!Root)
  {
    return;
  }

  // 先隐藏整棵 Actor 树，确保 AEffectActor 等未纳入 Space 管理的对象不显示。
  SetNodeTreeVisibility(Root, false);

  if (bVisible)
  {
    // 当前楼栋只恢复 OrdinaryActors 和 SpaceStruct。
    SetNodeIsolationContentVisibility(Root, true);
  }
}

bool UBuildingFloorComponent::CanDisassembleBuilding() const
{
  return BuildingRuntimeData.Config.bCanDisassemble;
}

bool UBuildingFloorComponent::IsDrawerDisassembleMode() const
{
  return BuildingRuntimeData.Config.bCanDisassemble &&
         BuildingRuntimeData.Config.DisassembleMode == EDisassembleMode::Drawer;
}

bool UBuildingFloorComponent::IsVisibilityDisassembleMode() const
{
  return BuildingRuntimeData.Config.bCanDisassemble &&
         BuildingRuntimeData.Config.DisassembleMode == EDisassembleMode::Visibility;
}

bool UBuildingFloorComponent::ShouldHideOtherBuildingsOnVisibilityEnter() const
{
  return BuildingRuntimeData.Config.Visibility.bHideOtherBuildingsOnEnter;
}

bool UBuildingFloorComponent::ShouldRestoreOtherBuildingsOnVisibilityExit() const
{
  return BuildingRuntimeData.Config.Visibility.bRestoreOtherBuildingsOnExit;
}

AActor *UBuildingFloorComponent::GetBuildingActor() const
{
  return BuildingRuntimeData.BuildingActor;
}

FString UBuildingFloorComponent::GetBuildingId() const
{
  return BuildingRuntimeData.BuildingId;
}

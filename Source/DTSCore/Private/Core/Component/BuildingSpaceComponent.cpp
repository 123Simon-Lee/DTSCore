#include "Core/Component/BuildingSpaceComponent.h"
#include "Core/Effect/BuildingStencilChannels.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Space/Space.h"
#include "Space/SpaceStruct.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
  int32 ParseFloorIndexFromString(const FString &InText)
  {
    FString Text = InText.TrimStartAndEnd().ToUpper();
    Text.ReplaceInline(TEXT(" "), TEXT(""));
    if (Text.IsEmpty())
    {
      return BUILDING_INVALID_FLOOR_INDEX;
    }

    const int32 UnderscoreIndex = Text.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 HyphenIndex = Text.Find(TEXT("-"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 SeparatorIndex = FMath::Max(UnderscoreIndex, HyphenIndex);
    if (SeparatorIndex != INDEX_NONE && SeparatorIndex + 1 < Text.Len())
    {
      Text = Text.Mid(SeparatorIndex + 1);
    }

    if (Text == TEXT("RF") || Text == TEXT("ROOF"))
    {
      return 9999;
    }

    if (Text.EndsWith(TEXT("F")))
    {
      Text.LeftChopInline(1);
    }
    if (Text.StartsWith(TEXT("B")))
    {
      const FString Basement = Text.RightChop(1);
      return Basement.IsNumeric() ? -FCString::Atoi(*Basement) : BUILDING_INVALID_FLOOR_INDEX;
    }
    return Text.IsNumeric() ? FCString::Atoi(*Text) : BUILDING_INVALID_FLOOR_INDEX;
  }

  /**
   * 楼层动画移动 RuntimeActor 时，附加在它下面的 Datasmith Actor 也会跟随移动。
   * Mobility 不会从父 Actor 继承，因此要在动画开始前将整棵附加层级设为 Movable。
   */
  /**
   * 楼层动画移动 RuntimeActor 时，
   * 附加在它下面的 Room、Datasmith Actor 和 StaticMeshActor
   * 都必须允许继承父节点位移。
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
      const TArray<TObjectPtr<UFloorSpaceComponent>> &FloorNodes)
  {
    TSet<AActor *> VisitedActors;

    for (const TObjectPtr<UFloorSpaceComponent> &FloorNodePtr : FloorNodes)
    {
      const UFloorSpaceComponent *FloorNode = FloorNodePtr.Get();
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
          TEXT("[BuildingSpaceComponent] SetFloorMeshHidden has no NewHidden parameter: %s"),
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
          TEXT("[BuildingSpaceComponent] SetFloorCeilingHidden has no NewHidden parameter: %s"),
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
UBuildingSpaceComponent::UBuildingSpaceComponent()
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

void UBuildingSpaceComponent::BeginPlay()
{
  Super::BeginPlay();
}

void UBuildingSpaceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
  // timeline tick
  LayeringTimeline.TickTimeline(DeltaTime);
  FloorExpandTimeline.TickTimeline(DeltaTime);
  LiftDropTimeline.TickTimeline(DeltaTime);
}
void UBuildingSpaceComponent::OnSpaceSelected()
{
  UE_LOG(LogTemp, Warning, TEXT("[BuildingSpaceComponent] OnSpaceSelected"));
  Super::OnSpaceSelected();
}
void UBuildingSpaceComponent::OnSpaceDeselected()
{
  UE_LOG(LogTemp, Warning, TEXT("[BuildingSpaceComponent] OnSpaceDeselected"));
  Super::OnSpaceDeselected();
}

void UBuildingSpaceComponent::SelectSingle()
{
  if (!IsValid(BuildingSpace.Get()))
  {
    return;
  }

  UWorld *World = GetWorld();

  if (!IsValid(World))
  {
    return;
  }

  USpaceSubsystem *SpaceSubsystem =
      World->GetSubsystem<USpaceSubsystem>();

  if (!IsValid(SpaceSubsystem))
  {
    return;
  }

  SpaceSubsystem->ToggleSingleSelect(BuildingSpace.Get());
}

void UBuildingSpaceComponent::DeselectAll()
{
  UWorld *World = GetWorld();

  if (!IsValid(World))
  {
    return;
  }

  USpaceSubsystem *SpaceSubsystem =
      World->GetSubsystem<USpaceSubsystem>();

  if (!IsValid(SpaceSubsystem))
  {
    return;
  }

  SpaceSubsystem->ClearSelection();
}
// ============================================================
// Initialize初始化
// ============================================================
void UBuildingSpaceComponent::Initialize(
    ASpace *InBuildingSpace,
    const FBuildingFloorConfig &InConfig,
    const FString &InFloorSpaceMark)
{
  BuildingSpace = InBuildingSpace;
  BuildingId = IsValid(InBuildingSpace)
                   ? (InBuildingSpace->Code.IsEmpty() ? InBuildingSpace->GetActorNameOrLabel() : InBuildingSpace->Code)
                   : FString();
  Config = InConfig;
  FloorSpaceMark = InFloorSpaceMark;
  // Building、Floor、Room、Device 以及普通模型都可能参与位移动画，
  // 初始化时一次性把整棵附加层级设为 Movable。
  TSet<AActor *> PreparedBuildingActors;
  SetActorHierarchyMovable(
      BuildingSpace.Get(),
      PreparedBuildingActors);
  // Building 自身移动时，整棵附加层级必须显式跟随。
  CacheBuildingAnimatedActors();
  // 缓存运行数据
  BuildRuntimeCache();
  // Datasmith 子 Actor 的 Mobility 不会继承自楼层 Actor，动画前统一处理一次。
  PrepareAnimatedFloorActors(FloorNodes);
  // 最高层没有更高楼层可抬，LiftDrop 会单独移动它的 TH。
  CacheLiftDropCeilingOriginalLocations();
  // 初始化节点的Transform
  InitializeTransforms();
  // 计算楼层的总高度和最大尺寸
  ComputeFloorMetrics();
  // 初始化Timeline和绑定对应方法到timeline
  InitializeTimelines();
}

void UBuildingSpaceComponent::CacheBuildingAnimatedActors()
{
  BuildingAnimatedActors.Reset();
  BuildingActorRelativeTransforms.Reset();

  if (!IsValid(BuildingSpace))
  {
    return;
  }

  BuildingOriginalTransform =
      BuildingSpace->GetActorTransform();

  TArray<AActor *> AttachedActors;
  BuildingSpace->GetAttachedActors(
      AttachedActors,
      true,
      true);

  for (AActor *Actor : AttachedActors)
  {
    if (!IsValid(Actor))
    {
      continue;
    }

    const TWeakObjectPtr<AActor> ActorPtr(Actor);
    BuildingAnimatedActors.Add(ActorPtr);
    BuildingActorRelativeTransforms.Add(
        ActorPtr,
        Actor->GetActorTransform().GetRelativeTransform(
            BuildingOriginalTransform));
  }

  // 父 Actor 必须先于子 Actor 更新。
  auto GetAttachmentDepth = [this](const AActor *Actor)
  {
    int32 Depth = 0;

    for (const AActor *Current = Actor;
         IsValid(Current) && Current != BuildingSpace;
         Current = Current->GetAttachParentActor())
    {
      ++Depth;
    }

    return Depth;
  };

  BuildingAnimatedActors.Sort(
      [&GetAttachmentDepth](
          const TWeakObjectPtr<AActor> &Left,
          const TWeakObjectPtr<AActor> &Right)
      {
        return GetAttachmentDepth(Left.Get()) <
               GetAttachmentDepth(Right.Get());
      });
}

void UBuildingSpaceComponent::SetBuildingAnimatedLocation(
    const FVector &NewLocation)
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  FTransform NewTransform = BuildingSpace->GetActorTransform();
  NewTransform.SetLocation(NewLocation);
  SetBuildingAnimatedTransform(NewTransform);
}

void UBuildingSpaceComponent::SetBuildingAnimatedTransform(
    const FTransform &NewTransform)
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  BuildingSpace->SetActorTransform(
      NewTransform,
      false,
      nullptr,
      ETeleportType::TeleportPhysics);

  SyncBuildingAnimatedActors();
}

void UBuildingSpaceComponent::SyncBuildingAnimatedActors()
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  const FTransform BuildingTransform =
      BuildingSpace->GetActorTransform();

  for (const TWeakObjectPtr<AActor> &ActorPtr :
       BuildingAnimatedActors)
  {
    AActor *Actor = ActorPtr.Get();
    if (!IsValid(Actor))
    {
      continue;
    }

    const FTransform *RelativeTransform =
        BuildingActorRelativeTransforms.Find(ActorPtr);
    if (!RelativeTransform)
    {
      continue;
    }

    Actor->SetActorTransform(
        *RelativeTransform * BuildingTransform,
        false,
        nullptr,
        ETeleportType::TeleportPhysics);
  }
}

void UBuildingSpaceComponent::InitializeTransforms()
{
  for (UFloorSpaceComponent *Node : FloorNodes)
  {
    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }
    Node->OriginalTransform = Node->RuntimeActor->GetActorTransform();
    Node->CurrentTransform = Node->OriginalTransform;
    Node->CurrentOffset = FVector::ZeroVector;
    Node->TargetOffset = FVector::ZeroVector;
    Node->bExpanded = false;
    Node->bHidden = Node->RuntimeActor->IsHidden();
  }
}

void UBuildingSpaceComponent::InitializeTimelines()
{
  // 从配置获取对应的曲线资产
  UCurveFloat *Curve = Config.Animation.Curve
                           ? Config.Animation.Curve.Get()
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
  LayeringUpdate.BindDynamic(this, &UBuildingSpaceComponent::OnLayeringUpdate);

  FOnTimelineEvent LayeringFinished;
  LayeringFinished.BindDynamic(this, &UBuildingSpaceComponent::OnLayeringFinished);

  LayeringTimeline.AddInterpFloat(Curve, LayeringUpdate);
  LayeringTimeline.SetTimelineFinishedFunc(LayeringFinished);
  LayeringTimeline.SetLooping(false);

  // 楼层抽出推入动画
  FOnTimelineFloat FloorExpandUpdate;
  FloorExpandUpdate.BindDynamic(this, &UBuildingSpaceComponent::OnFloorExpandUpdate);

  FOnTimelineEvent FloorExpandFinished;
  FloorExpandFinished.BindDynamic(this, &UBuildingSpaceComponent::OnFloorExpandFinished);

  FloorExpandTimeline.AddInterpFloat(Curve, FloorExpandUpdate);
  FloorExpandTimeline.SetTimelineFinishedFunc(FloorExpandFinished);
  FloorExpandTimeline.SetLooping(false);

  // List Drop动画
  FOnTimelineFloat LiftDropUpdate;
  LiftDropUpdate.BindDynamic(this, &UBuildingSpaceComponent::OnLiftDropUpdate);

  FOnTimelineEvent LiftDropFinished;
  LiftDropFinished.BindDynamic(this, &UBuildingSpaceComponent::OnLiftDropFinished);

  LiftDropTimeline.AddInterpFloat(Curve, LiftDropUpdate);

  LiftDropTimeline.SetTimelineFinishedFunc(LiftDropFinished);

  LiftDropTimeline.SetLooping(false);
}
// ============================================================
// Runtime Cache
// ============================================================
void UBuildingSpaceComponent::BuildRuntimeCache()
{
  // 先清空缓存
  FloorMap.Empty();
  ActorMap.Empty();
  FloorNodes.Empty();
  AllFloorComponents.Empty();
  HiddenFloors.Empty();
  FloorCeilingActorMap.Empty();
  FloorWallActorMap.Empty();

  if (IsValid(BuildingSpace))
  {
    TArray<ASpace *> FloorSpaces;
    CollectFloorSpaces(BuildingSpace, FloorSpaces);
    for (ASpace *FloorSpace : FloorSpaces)
    {
      if (!IsValid(FloorSpace))
      {
        continue;
      }

      const int32 FloorIndex = ResolveFloorIndex(FloorSpace);
      if (FloorIndex == BUILDING_INVALID_FLOOR_INDEX || FloorMap.Contains(FloorIndex))
      {
        UE_LOG(LogTemp, Warning, TEXT("[BuildingSpace] Skip invalid or duplicate floor: %s (%d)"),
               *FloorSpace->GetActorNameOrLabel(), FloorIndex);
        continue;
      }

      UFloorSpaceComponent *FloorComponent = FloorSpace->FindComponentByClass<UFloorSpaceComponent>();
      if (!FloorComponent)
      {
        FloorComponent = NewObject<UFloorSpaceComponent>(FloorSpace, NAME_None, RF_Transient);
        FloorComponent->RegisterComponent();
      }
      FloorComponent->InitializeFloor(FloorSpace, FloorIndex, IsFloorDisassemblable(FloorSpace, FloorIndex));
      AllFloorComponents.Add(FloorComponent);
      for (AActor *Actor : FloorComponent->GetManagedActors())
      {
        ActorMap.Add(Actor, FloorComponent);
      }
      if (FloorComponent->bCanDisassemble)
      {
        FloorMap.Add(FloorIndex, FloorComponent);
        FloorNodes.Add(FloorComponent);
      }
    }
    FloorNodes.Sort(
        [](const UFloorSpaceComponent &A, const UFloorSpaceComponent &B)
        {
          return A.FloorIndex < B.FloorIndex;
        });
    BuildNamedActorCache();
  }
}

void UBuildingSpaceComponent::CollectFloorSpaces(ASpace *ParentSpace, TArray<ASpace *> &OutFloorSpaces) const
{
  if (!IsValid(ParentSpace))
  {
    return;
  }
  for (ASpace *Child : ParentSpace->GetSubspaces())
  {
    if (!IsValid(Child))
    {
      continue;
    }
    if (Child->HasMark(FloorSpaceMark))
    {
      OutFloorSpaces.AddUnique(Child);
    }
    else
    {
      CollectFloorSpaces(Child, OutFloorSpaces);
    }
  }
}

int32 UBuildingSpaceComponent::ResolveFloorIndex(const ASpace *FloorSpace) const
{
  if (!IsValid(FloorSpace))
  {
    return BUILDING_INVALID_FLOOR_INDEX;
  }
  const FBuildingFloorIndexSettings &Settings = Config.FloorIndex;
  const FString Code = FloorSpace->Code.TrimStartAndEnd();
  const FString DisplayName = FloorSpace->DisplayName.TrimStartAndEnd();
  const FString ActorName = FloorSpace->GetActorNameOrLabel();
  auto FindMapped = [&Settings](const FString &Key)
  {
    const int32 *Found = Settings.FloorIndexMap.Find(Key);
    return Found ? *Found : BUILDING_INVALID_FLOOR_INDEX;
  };
  auto AutoParse = [&]()
  {
    int32 Result = ParseFloorIndexFromString(Code);
    if (Result == BUILDING_INVALID_FLOOR_INDEX)
      Result = ParseFloorIndexFromString(DisplayName);
    if (Result == BUILDING_INVALID_FLOOR_INDEX)
      Result = ParseFloorIndexFromString(ActorName);
    return Result;
  };
  switch (Settings.ResolveMode)
  {
  case EFloorIndexResolveMode::CodeMap:
    return FindMapped(Code);
  case EFloorIndexResolveMode::DisplayNameMap:
    return FindMapped(DisplayName);
  case EFloorIndexResolveMode::ActorNameMap:
    return FindMapped(ActorName);
  case EFloorIndexResolveMode::CodeMapThenAuto:
  {
    const int32 Result = FindMapped(Code);
    return Result != BUILDING_INVALID_FLOOR_INDEX ? Result : AutoParse();
  }
  case EFloorIndexResolveMode::DisplayNameMapThenAuto:
  {
    const int32 Result = FindMapped(DisplayName);
    return Result != BUILDING_INVALID_FLOOR_INDEX ? Result : AutoParse();
  }
  case EFloorIndexResolveMode::ActorNameMapThenAuto:
  {
    const int32 Result = FindMapped(ActorName);
    return Result != BUILDING_INVALID_FLOOR_INDEX ? Result : AutoParse();
  }
  default:
    return AutoParse();
  }
}

bool UBuildingSpaceComponent::IsFloorDisassemblable(const ASpace *FloorSpace, int32 FloorIndex) const
{
  return Config.bCanDisassemble && IsValid(FloorSpace) &&
         !Config.Disassemble.DisabledSpaceCodes.Contains(FloorSpace->Code.TrimStartAndEnd()) &&
         !Config.Disassemble.DisabledFloors.Contains(FloorIndex) &&
         (Config.Disassemble.DisableDisassembleMark.IsEmpty() ||
          !FloorSpace->HasMark(Config.Disassemble.DisableDisassembleMark));
}

void UBuildingSpaceComponent::BuildNamedActorCache()
{
  const FBuildingNameSettings &NameSettings = Config.Name;

  for (const TObjectPtr<UFloorSpaceComponent> &FloorNodePtr : FloorNodes)
  {
    UFloorSpaceComponent *FloorNode = FloorNodePtr.Get();
    if (!FloorNode)
    {
      continue;
    }

    TSet<AActor *> Candidates;
    for (AActor *Actor : FloorNode->GetManagedActors())
    {
      Candidates.Add(Actor);
    }

    // 楼层组件通过所属 Space 收集 OrdinaryActors；普通模型只要挂在楼层
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

bool UBuildingSpaceComponent::MatchesActorNameRule(
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

  /*
   * 分段精确匹配：按 '_' 拆分后任一段完全等于 MatchString。
   * 覆盖 GZZL_RF_TH_001 这类命名，规则即使配置为 Equals("TH") 也能命中，
   * 且不会像 segment 前后缀匹配那样误伤 MONTH 等包含 TH 的单词段。
   */
  auto MatchesSegmentEquals = [&Rule](const FString &Value)
  {
    TArray<FString> Segments;
    Value.ParseIntoArray(Segments, TEXT("_"), true);
    for (const FString &Segment : Segments)
    {
      if (Segment.Equals(Rule.MatchString, ESearchCase::IgnoreCase))
      {
        return true;
      }
    }
    return false;
  };

  return MatchesValue(Label) ||
         MatchesValue(RemoveNumericSuffix(Label)) ||
         MatchesValue(ObjectName) ||
         MatchesValue(RemoveNumericSuffix(ObjectName)) ||
         MatchesSegmentEquals(Label) ||
         MatchesSegmentEquals(ObjectName);
}

void UBuildingSpaceComponent::SetNamedActorsHidden(
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

void UBuildingSpaceComponent::RestoreAllFloorCeilings()
{
  for (const TPair<int32, TArray<TWeakObjectPtr<AActor>>> &Pair :
       FloorCeilingActorMap)
  {
    SetNamedActorsHidden(
        FloorCeilingActorMap,
        Pair.Key,
        false);
  }
}
// 缓存最高层TH
void UBuildingSpaceComponent::CacheLiftDropCeilingOriginalLocations()
{
  LiftDropCeilingOriginalLocationMap.Empty();

  TSet<AActor *> VisitedActors;

  for (const TPair<int32, TArray<TWeakObjectPtr<AActor>>> &Pair :
       FloorCeilingActorMap)
  {
    for (const TWeakObjectPtr<AActor> &ActorPtr : Pair.Value)
    {
      AActor *CeilingActor = ActorPtr.Get();
      if (!IsValid(CeilingActor))
      {
        continue;
      }

      LiftDropCeilingOriginalLocationMap.FindOrAdd(
          CeilingActor,
          CeilingActor->GetActorLocation());

      SetActorHierarchyMovable(
          CeilingActor,
          VisitedActors);
    }
  }
}
// 还原最高层TH
void UBuildingSpaceComponent::RestoreLiftDropCeilingLocations()
{
  for (const TPair<TWeakObjectPtr<AActor>, FVector> &Pair :
       LiftDropCeilingOriginalLocationMap)
  {
    if (AActor *CeilingActor = Pair.Key.Get())
    {
      CeilingActor->SetActorLocation(Pair.Value);
    }
  }

  LiftDropCeilingStartLocationMap.Empty();
  LiftDropCeilingTargetLocationMap.Empty();
  LiftDropCeilingHideFloorOnFinished =
      BUILDING_INVALID_FLOOR_INDEX;
}

void UBuildingSpaceComponent::AddHighestFloorCeilingLiftAnimation(
    UFloorSpaceComponent *HighestFloorNode,
    float LiftHeight)
{
  if (!HighestFloorNode)
  {
    return;
  }

  const TArray<TWeakObjectPtr<AActor>> *CeilingActors =
      FloorCeilingActorMap.Find(HighestFloorNode->FloorIndex);
  if (!CeilingActors)
  {
    return;
  }

  // 最高层保持显示，只把 TH 抬到楼层上方。
  SetFloorCeilingHidden(
      HighestFloorNode->FloorIndex,
      false);

  LiftDropCeilingHideFloorOnFinished =
      HighestFloorNode->FloorIndex;

  for (const TWeakObjectPtr<AActor> &ActorPtr : *CeilingActors)
  {
    AActor *CeilingActor = ActorPtr.Get();
    if (!IsValid(CeilingActor))
    {
      continue;
    }

    const FVector *OriginalLocation =
        LiftDropCeilingOriginalLocationMap.Find(CeilingActor);
    if (!OriginalLocation)
    {
      continue;
    }

    LiftDropCeilingStartLocationMap.Add(
        CeilingActor,
        CeilingActor->GetActorLocation());

    LiftDropCeilingTargetLocationMap.Add(
        CeilingActor,
        *OriginalLocation + FVector::UpVector * LiftHeight);
  }
}

void UBuildingSpaceComponent::ComputeFloorMetrics()
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
  for (TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
  {
    UFloorSpaceComponent *Node = NodePtr.Get();
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
  if (Config.Drawer.MaxFloorSizeX > 0.f)
  {
    MaxFloorSizeX = Config.Drawer.MaxFloorSizeX;
  }

  if (Config.Drawer.MaxFloorSizeY > 0.f)
  {
    MaxFloorSizeY = Config.Drawer.MaxFloorSizeY;
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

FBox UBuildingSpaceComponent::GetNodeBounds(UFloorSpaceComponent *Node) const
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

FBuildingFocusTarget UBuildingSpaceComponent::GetBuildingFocusTarget() const
{
  if (!BuildingSpace)
  {
    return FBuildingFocusTarget();
  }

  TArray<UFloorSpaceComponent *> FocusRoots;
  FocusRoots.Reserve(AllFloorComponents.Num());
  for (const TObjectPtr<UFloorSpaceComponent> &FloorNode : AllFloorComponents)
  {
    if (FloorNode)
    {
      FocusRoots.Add(FloorNode.Get());
    }
  }

  return BuildFocusTarget(
      FocusRoots,
      BuildingSpace.Get());
}

FBuildingFocusTarget UBuildingSpaceComponent::GetFloorFocusTarget(
    int32 FloorIndex) const
{
  UFloorSpaceComponent *FloorNode = FindFloorWithIndex(FloorIndex);
  if (!FloorNode)
  {
    return FBuildingFocusTarget();
  }

  TArray<UFloorSpaceComponent *> FocusRoots;
  FocusRoots.Add(FloorNode);
  return BuildFocusTarget(FocusRoots, FloorNode->RuntimeActor.Get());
}

FBuildingFocusTarget UBuildingSpaceComponent::GetFloorWallFocusTarget(
    int32 FloorIndex) const
{
  UFloorSpaceComponent *FloorNode = FindFloorWithIndex(FloorIndex);
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
        TEXT("[BuildingSpaceComponent] Floor %d 没有匹配到 WQ 外墙，回退整层聚焦。"),
        FloorIndex);
  }

  // WQ 为空时 BuildFocusTargetFromActors 内部会用 FallbackActor 兜底。
  return BuildFocusTargetFromActors(
      WallActors,
      FloorNode->RuntimeActor.Get());
}

FBuildingFocusSettings UBuildingSpaceComponent::GetFloorFocusSettings(
    const int32 FloorIndex) const
{
  FBuildingFocusSettings Result = Config.Focus;
  const FBuildingFloorFocusSettings *Override =
      Config.FloorFocusOverrides.Find(FloorIndex);
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

TArray<AActor *> UBuildingSpaceComponent::GetFloorCeilingActors(
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

TArray<AActor *> UBuildingSpaceComponent::GetFloorWallActors(
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

void UBuildingSpaceComponent::SetFloorCeilingHidden(
    int32 FloorIndex,
    bool bHidden)
{
  /*
   * 先使用楼层 Actor 自己维护的 FloorCeilingSet。
   * 这一步覆盖单层仓库等业务直接登记的 TH Actor。
   */
  if (UFloorSpaceComponent *FloorNode = FindFloorWithIndex(FloorIndex))
  {
    TrySetFloorCeilingHidden(FloorNode->RuntimeActor, bHidden);
  }

  // 保留插件原有按 THRule 扫描的缓存，兼容非 ABuildingFloor 数据源。
  SetNamedActorsHidden(FloorCeilingActorMap, FloorIndex, bHidden);
}

FBuildingFocusTarget UBuildingSpaceComponent::BuildFocusTarget(
    const TArray<UFloorSpaceComponent *> &FocusRoots,
    AActor *FallbackActor) const
{
  if (!BuildingSpace)
  {
    return FBuildingFocusTarget();
  }

  // 楼层组件直接给出受控 Actor，不再展开复制的 Runtime 树。
  TArray<AActor *> FocusActors;
  for (UFloorSpaceComponent *FocusRoot : FocusRoots)
  {
    if (!FocusRoot)
    {
      continue;
    }

    FocusActors.Append(FocusRoot->GetManagedActors());
  }

  return BuildFocusTargetFromActors(FocusActors, FallbackActor);
}

FBuildingFocusTarget UBuildingSpaceComponent::BuildFocusTargetFromActors(
    const TArray<AActor *> &InActors,
    AActor *FallbackActor) const
{
  FBuildingFocusTarget Result;
  if (!BuildingSpace)
  {
    return Result;
  }

  const FTransform BuildingTransform =
      BuildingSpace->GetActorTransform();
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
                              : BuildingSpace.Get();
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
void UBuildingSpaceComponent::LayeringDisplay()
{
  if (!Config.bCanDisassemble)
  {
    return;
  }

  if (Config.DisassembleMode != EDisassembleMode::Drawer)
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
void UBuildingSpaceComponent::BackToNormal()
{
  if (Config.DisassembleMode != EDisassembleMode::Drawer)
  {
    return;
  }

  RestoreAllFloorCeilings();

  if (!bIsLayering && !bLayeringRunning)
  {
    return;
  }

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  for (TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
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
FVector UBuildingSpaceComponent::GetPullDirection() const
{
  FVector Direction = FVector::ZeroVector;

  switch (Config.Drawer.PullDirection)
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

  if (BuildingSpace)
  {
    Direction =
        BuildingSpace->GetActorTransform()
            .TransformVectorNoScale(Direction);
  }

  if (!Direction.IsNearlyZero())
  {
    Direction.Normalize();
  }

  return Direction;
}

void UBuildingSpaceComponent::ToggleFloor(int32 Floor)
{
  if (!Config.bCanDisassemble)
  {
    return;
  }

  const EDisassembleMode Mode = Config.DisassembleMode;

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

  UFloorSpaceComponent *Node = FindFloorWithIndex(Floor);
  if (!Node || !Node->bCanDisassemble)
  {
    return;
  }

  ToggleNode(Node);
}

void UBuildingSpaceComponent::ToggleNode(UFloorSpaceComponent *Node)
{
  if (!Node)
  {
    return;
  }

  if (!Node->bCanDisassemble)
  {
    return;
  }

  switch (Config.DisassembleMode)
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
void UBuildingSpaceComponent::DrawerNode(
    UFloorSpaceComponent *Node)
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

  for (TObjectPtr<UFloorSpaceComponent> &FloorNodePtr : FloorNodes)
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
    UFloorSpaceComponent *OldNode = FindFloorWithIndex(ActiveFloor);
    if (OldNode)
    {
      SetFloorCeilingHidden(OldNode->FloorIndex, false);
      OldNode->TargetOffset = FVector::ZeroVector;
      OldNode->bExpanded = false;
    }

    ActiveFloor = Node->FloorIndex;

    if (Config.Name.bHideCurrentFloorCeiling)
    {
      SetFloorCeilingHidden(Node->FloorIndex, true);
    }

    Node->TargetOffset = GetPullDirection() * GetPullDistance(Node);
    Node->bExpanded = true;
  }

  PlayFloorExpandTimeline();
}

void UBuildingSpaceComponent::SetNodeHighlight(
    UFloorSpaceComponent *Node,
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
bool UBuildingSpaceComponent::IsLiftDropDisassembleMode() const
{
  return Config.bCanDisassemble &&
         Config.DisassembleMode ==
             EDisassembleMode::LiftDrop;
}
void UBuildingSpaceComponent::EnterLiftDropDisassemble()
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
  for (const TObjectPtr<UFloorSpaceComponent> &NodePtr :
       FloorNodes)
  {
    UFloorSpaceComponent *Node = NodePtr.Get();

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    Node->SetAnimatedLocation(Node->OriginalTransform.GetLocation());

    Node->CurrentTransform = Node->OriginalTransform;

    Node->CurrentOffset = FVector::ZeroVector;

    Node->TargetOffset = FVector::ZeroVector;

    Node->bExpanded = false;

    ApplyFloorVisibility(Node->FloorIndex, true);
  }

  RestoreLiftDropCeilingLocations();
  RestoreAllFloorCeilings();
}
bool UBuildingSpaceComponent::SelectLiftDropFloor(
    int32 TargetFloor)
{
  if (!IsLiftDropDisassembleMode() || bLiftDropRunning)
  {
    return false;
  }

  UFloorSpaceComponent *TargetNode =
      FindFloorWithIndex(TargetFloor);

  if (!TargetNode ||
      !TargetNode->RuntimeActor ||
      !TargetNode->bCanDisassemble)
  {
    return false;
  }

  if (ActiveFloor == TargetFloor)
  {
    return false;
  }

  UFloorSpaceComponent *HighestFloorNode =
      GetHighestFloorNode();
  if (!HighestFloorNode)
  {
    return false;
  }

  int32 PreviousFloor = ActiveFloor;
  const bool bFirstSelection =
      PreviousFloor == BUILDING_INVALID_FLOOR_INDEX;

  // 初始状态等价于“当前截面在最高层”：第一次点击任意层都走上浮。
  if (bFirstSelection)
  {
    PreviousFloor = HighestFloorNode->FloorIndex;
  }

  const bool bMovingToHigherFloor =
      !bFirstSelection && TargetFloor > PreviousFloor;

  LiftDropTimeline.Stop();

  // 如果上一次选择的是最高层，它的 TH 仍停留在抬高位置。
  // 新状态计算前先恢复，避免位置偏移累积。
  RestoreLiftDropCeilingLocations();

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  const float DropStartHeight =
      Config.LiftDrop.DropStartHeight;

  const float LiftOutHeight =
      Config.LiftDrop.LiftOutHeight;

  if (bMovingToHigherFloor)
  {

    LiftDropTransition = ELiftDropTransition::DropIn;

    for (const TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
    {
      UFloorSpaceComponent *Node = NodePtr.Get();

      if (!Node || !Node->RuntimeActor)
      {
        continue;
      }

      const int32 FloorIndex = Node->FloorIndex;

      const FVector OriginalLocation = Node->OriginalTransform.GetLocation();

      Node->CurrentOffset = FVector::ZeroVector;

      Node->TargetOffset = FVector::ZeroVector;

      if (FloorIndex <= PreviousFloor)
      {
        // 原本已经显示的楼层保持原位。
        Node->SetAnimatedLocation(OriginalLocation);

        Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();

        ApplyFloorVisibility(FloorIndex, true);
      }
      else if (FloorIndex <= TargetFloor)
      {
        // 此前隐藏、现在进入显示范围的楼层一起从高处下降。
        const FVector StartLocation =
            OriginalLocation +
            FVector::UpVector *
                DropStartHeight;

        Node->SetAnimatedLocation(
            StartLocation);

        Node->CurrentTransform =
            Node->RuntimeActor->GetActorTransform();

        ApplyFloorVisibility(FloorIndex, true);

        LiftDropStartLocationMap.Add(
            Node,
            StartLocation);

        LiftDropTargetLocationMap.Add(
            Node,
            OriginalLocation);
      }
      else
      {
        // 新目标楼层以上继续保持隐藏。
        ApplyFloorVisibility(
            FloorIndex,
            false);

        Node->SetAnimatedLocation(
            OriginalLocation);

        Node->CurrentTransform =
            Node->RuntimeActor->GetActorTransform();
      }
    }
  }
  else
  {
    /*
     * 第一次选择，或者切换到更低楼层。
     *
     * 例如：
     * 初始原位状态 → 3
     * 3 → -1
     *
     * (TargetFloor, PreviousFloor] 内的楼层向上抬升，
     * 并在动画完成后隐藏；目标层及以下保持原位。
     */
    LiftDropTransition =
        ELiftDropTransition::LiftOut;

    for (const TObjectPtr<UFloorSpaceComponent> &NodePtr :
         FloorNodes)
    {
      UFloorSpaceComponent *Node = NodePtr.Get();

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
        // 新目标楼层以及以下楼层保持显示。
        Node->SetAnimatedLocation(
            OriginalLocation);

        Node->CurrentTransform =
            Node->RuntimeActor->GetActorTransform();

        ApplyFloorVisibility(
            FloorIndex,
            true);
      }
      else if (FloorIndex <= PreviousFloor)
      {
        const FVector LiftTargetLocation =
            OriginalLocation +
            FVector::UpVector *
                LiftOutHeight;

        Node->SetAnimatedLocation(
            OriginalLocation);

        Node->CurrentTransform =
            Node->RuntimeActor->GetActorTransform();

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
        // 原本就隐藏的更高楼层继续隐藏。
        ApplyFloorVisibility(
            FloorIndex,
            false);

        Node->SetAnimatedLocation(
            OriginalLocation);

        Node->CurrentTransform =
            Node->RuntimeActor->GetActorTransform();
      }
    }
  }

  ActiveFloor = TargetFloor;

  if (TargetFloor == HighestFloorNode->FloorIndex &&
      bFirstSelection)
  {
    // 初始状态选择最高层：单独抬高最高层 TH，完成后隐藏。
    AddHighestFloorCeilingLiftAnimation(
        HighestFloorNode,
        LiftOutHeight);
  }
  else if (TargetFloor == HighestFloorNode->FloorIndex)
  {
    // 从低层选择最高层时，TH 在下降过程中保持显示，
    // 到达屋面原位后隐藏；这次不再额外执行 TH 上抬。
    SetFloorCeilingHidden(
        TargetFloor,
        false);

    LiftDropCeilingHideFloorOnFinished =
        TargetFloor;
  }
  else if (Config.Name.bHideCurrentFloorCeiling)
  {
    SetFloorCeilingHidden(
        TargetFloor,
        true);
  }

  PlayLiftDropTimeline();

  return true;
}

void UBuildingSpaceComponent::ExitLiftDropDisassemble()
{
  LiftDropTimeline.Stop();

  bLiftDropRunning = false;
  LiftDropTransition = ELiftDropTransition::None;

  ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  for (const TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
  {
    UFloorSpaceComponent *Node = NodePtr.Get();

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    Node->SetAnimatedLocation(Node->OriginalTransform.GetLocation());

    Node->CurrentTransform = Node->OriginalTransform;

    Node->CurrentOffset = FVector::ZeroVector;

    Node->TargetOffset = FVector::ZeroVector;

    Node->bExpanded = false;

    ApplyFloorVisibility(Node->FloorIndex, true);
  }

  RestoreLiftDropCeilingLocations();
  RestoreAllFloorCeilings();

  if (!bLayeringRunning && !bExpandRunning)
  {
    SetComponentTickEnabled(false);
  }
}
// 抽屉动画更新/结束

void UBuildingSpaceComponent::OnLayeringUpdate(float Alpha)
{
  const int32 FloorCount = FloorNodes.Num();
  // 如果配置中有设置最大高度，则使用配置的最大高度，否则使用计算得到的总高度
  const float BaseHeight =
      Config.Drawer.CampusMaxBuildingHeight > 0.f
          ? Config.Drawer.CampusMaxBuildingHeight
          : TotalBuildingHeight;
  // 如果楼层数量大于1，则计算每层的高度间隔，否则间隔为0
  const float SpreadStep =
      FloorCount > 1
          ? (TotalBuildingHeight / static_cast<float>(FloorCount - 1)) *
                Config.Drawer.SpreadScale
          : 0.f;

  for (int32 i = 0; i < FloorNodes.Num(); ++i)
  {
    UFloorSpaceComponent *Node = FloorNodes[i].Get();
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

    Node->SetAnimatedLocation(NewLocation);
    Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();
  }
}

void UBuildingSpaceComponent::OnLayeringFinished()
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

    for (TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
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

    for (TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
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
void UBuildingSpaceComponent::OnFloorExpandUpdate(float Alpha)
{
  for (TObjectPtr<UFloorSpaceComponent> &NodePtr : FloorNodes)
  {
    UFloorSpaceComponent *Node = NodePtr.Get();

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
    Node->SetAnimatedLocation(NewLocation);

    Node->CurrentOffset = NewOffset;
    Node->CurrentTransform = Node->RuntimeActor->GetActorTransform();
  }
}

void UBuildingSpaceComponent::OnFloorExpandFinished()
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
void UBuildingSpaceComponent::SetNodeTreeVisibility(
    UFloorSpaceComponent *Node,
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
  // 设备
  for (TObjectPtr<AActor> &ControlledActor : Node->ControlledActors)
  {
    if (ControlledActor)
    {
      SetActorTreeHidden(ControlledActor.Get(), !bVisible);
    }
  }
}

void UBuildingSpaceComponent::SetNodeStructuralTreeVisibility(
    UFloorSpaceComponent *Node,
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
}

void UBuildingSpaceComponent::SetNodeIsolationContentVisibility(
    UFloorSpaceComponent *Node,
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

    // SpaceStruct 已由楼层 Space 收集到 ControlledActors，设备等结构正常恢复。
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
}

void UBuildingSpaceComponent::SetActorTreeHidden(
    AActor *Actor,
    bool bHidden)
{
  if (!Actor)
  {
    return;
  }
  if (Actor->ActorHasTag(
          SpaceVisibilityTags::SelfManagedVisibility))
  {
    return;
  }
  // 隐藏当前 Actor
  Actor->SetActorHiddenInGame(bHidden);

  TArray<AActor *> AttachedActors;
  Actor->GetAttachedActors(
      AttachedActors,
      true,
      false);
  for (AActor *ChildActor : AttachedActors)
  {
    SetActorTreeHidden(
        ChildActor,
        bHidden);
  }
}

void UBuildingSpaceComponent::PlayLiftDropTimeline()
{
  if (LiftDropStartLocationMap.Num() == 0 &&
      LiftDropCeilingStartLocationMap.Num() == 0)
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
// 每帧tick
void UBuildingSpaceComponent::OnLiftDropUpdate(float Alpha)
{
  // 普通楼层 RuntimeActor 动画。
  for (const TPair<UFloorSpaceComponent *, FVector> &Pair :
       LiftDropStartLocationMap)
  {
    UFloorSpaceComponent *Node = Pair.Key;

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    const FVector *TargetLocation =
        LiftDropTargetLocationMap.Find(Node);

    if (!TargetLocation)
    {
      continue;
    }

    const FVector NewLocation =
        FMath::Lerp(
            Pair.Value,
            *TargetLocation,
            Alpha);

    Node->SetAnimatedLocation(
        NewLocation);

    Node->CurrentOffset =
        NewLocation -
        Node->OriginalTransform.GetLocation();

    Node->CurrentTransform =
        Node->RuntimeActor->GetActorTransform();
  }

  // 最高层 TH 单独抬升动画。楼层 Actor 先更新，再设置 TH 世界位置，
  // 即使 TH 附加在正在下降的最高层下面，也不会叠加父级位移。
  for (const TPair<TWeakObjectPtr<AActor>, FVector> &Pair :
       LiftDropCeilingStartLocationMap)
  {
    AActor *CeilingActor = Pair.Key.Get();
    if (!IsValid(CeilingActor))
    {
      continue;
    }

    const FVector *TargetLocation =
        LiftDropCeilingTargetLocationMap.Find(Pair.Key);
    if (!TargetLocation)
    {
      continue;
    }

    CeilingActor->SetActorLocation(
        FMath::Lerp(Pair.Value, *TargetLocation, Alpha));
  }
}

void UBuildingSpaceComponent::OnLiftDropFinished()
{
  // 隐藏已经抬升出去的楼层，并在隐藏状态下恢复原位置。
  for (UFloorSpaceComponent *Node :
       LiftDropHideOnFinished)
  {
    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    ApplyFloorVisibility(
        Node->FloorIndex,
        false);

    Node->SetAnimatedLocation(
        Node->OriginalTransform.GetLocation());

    Node->CurrentTransform =
        Node->OriginalTransform;

    Node->CurrentOffset =
        FVector::ZeroVector;

    Node->TargetOffset =
        FVector::ZeroVector;
  }

  // 普通掉落楼层最终回到准确目标位置。
  for (const TPair<UFloorSpaceComponent *, FVector> &Pair :
       LiftDropTargetLocationMap)
  {
    UFloorSpaceComponent *Node = Pair.Key;

    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }

    if (!LiftDropHideOnFinished.Contains(Node))
    {
      Node->SetAnimatedLocation(
          Pair.Value);

      Node->CurrentTransform =
          Node->RuntimeActor->GetActorTransform();

      Node->CurrentOffset =
          FVector::ZeroVector;

      Node->TargetOffset =
          FVector::ZeroVector;
    }
  }

  // 将最高层 TH 对齐到准确的抬升终点。
  for (const TPair<TWeakObjectPtr<AActor>, FVector> &Pair :
       LiftDropCeilingTargetLocationMap)
  {
    if (AActor *CeilingActor = Pair.Key.Get())
    {
      CeilingActor->SetActorLocation(Pair.Value);
    }
  }

  if (LiftDropCeilingHideFloorOnFinished !=
      BUILDING_INVALID_FLOOR_INDEX)
  {
    SetFloorCeilingHidden(
        LiftDropCeilingHideFloorOnFinished,
        true);
  }

  bLiftDropRunning = false;

  LiftDropTransition =
      ELiftDropTransition::None;

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();
  LiftDropCeilingStartLocationMap.Empty();
  LiftDropCeilingTargetLocationMap.Empty();
  LiftDropCeilingHideFloorOnFinished =
      BUILDING_INVALID_FLOOR_INDEX;
  OnFloorExpandTimelineFinished.Broadcast();

  if (!bLayeringRunning &&
      !bExpandRunning &&
      !bLiftDropRunning)
  {
    SetComponentTickEnabled(false);
  }
}

float UBuildingSpaceComponent::GetPullDistance(UFloorSpaceComponent *Node) const
{
  if (!Node)
  {
    return 0.f;
  }

  float BaseDistance = 0.f;

  switch (Config.Drawer.PullDirection)
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

  float Scale = Config.Drawer.PullDistanceScale;

  if (const float *OverrideScale =
          Config.Drawer.FloorPullScaleOverride.Find(Node->FloorIndex))
  {
    Scale = *OverrideScale;
  }

  return BaseDistance * Scale;
}

void UBuildingSpaceComponent::PlayFloorExpandTimeline()
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

void UBuildingSpaceComponent::PlayLayeringTimeline(bool bForward)
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
void UBuildingSpaceComponent::VisibilityNode(
    UFloorSpaceComponent *Node)
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

void UBuildingSpaceComponent::ApplyNodeVisibility(
    UFloorSpaceComponent *Node,
    bool bVisible)
{
  /*
   * Floor 的显隐必须覆盖完整附加层级。
   * Room、Device、StaticMeshActor 都是独立 Actor，
   * 只隐藏 Floor Space 或 OrdinaryActors 不会让它们自动隐藏。
   */
  SetNodeTreeVisibility(Node, bVisible);
}

void UBuildingSpaceComponent::ApplyFloorVisibility(int32 Floor, bool bVisible)
{
  UFloorSpaceComponent *Node = FindFloorWithIndex(Floor);
  if (!Node)
  {
    return;
  }

  ApplyNodeVisibility(Node, bVisible);
  /*
   * 楼层显示时恢复该层TH；
   * 楼层隐藏时同步隐藏该层TH。
   *
   * 这样即使TH不在Space的OrdinaryActors中，
   * 也会通过FloorCeilingActorMap进行控制。
   */
  SetFloorCeilingHidden(Floor, !bVisible);

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

void UBuildingSpaceComponent::ToggleFloorVisibility(int32 Floor)
{
  if (!Config.bCanDisassemble)
  {
    return;
  }

  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    const int32 FloorIndex = Pair.Key;

    bool bVisible = true;

    switch (Config.Visibility.FloorDisplayMode)
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

  if (Config.Name.bHideCurrentFloorCeiling)
  {
    SetFloorCeilingHidden(Floor, true);
  }
}

void UBuildingSpaceComponent::ShowOnlyFloor(int32 Floor)
{
  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, Pair.Key == Floor);
  }
}

void UBuildingSpaceComponent::ShowFloorRange(int32 MinFloor, int32 MaxFloor)
{
  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    const int32 FloorIndex = Pair.Key;
    const bool bVisible = FloorIndex >= MinFloor && FloorIndex <= MaxFloor;

    ApplyFloorVisibility(FloorIndex, bVisible);
  }
}

void UBuildingSpaceComponent::ShowAll()
{
  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, true);
  }
}

void UBuildingSpaceComponent::HideAbove(int32 Floor)
{
  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    if (Pair.Key > Floor)
    {
      ApplyFloorVisibility(Pair.Key, false);
    }
  }
}

void UBuildingSpaceComponent::SetAllFloorsVisibility(bool bVisible)
{
  for (const TPair<int32, TObjectPtr<UFloorSpaceComponent>> &Pair : FloorMap)
  {
    ApplyFloorVisibility(Pair.Key, bVisible);
  }
}

bool UBuildingSpaceComponent::IsFloorVisible(int32 Floor) const
{
  return !HiddenFloors.Contains(Floor);
}

// ============================================================
// Query
// ============================================================

UFloorSpaceComponent *UBuildingSpaceComponent::FindFloorWithIndex(
    int32 FloorIndex) const
{
  if (const TObjectPtr<UFloorSpaceComponent> *Ptr =
          FloorMap.Find(FloorIndex))
  {
    return Ptr->Get();
  }

  return nullptr;
}

UFloorSpaceComponent *UBuildingSpaceComponent::FindFloorWithId(
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

  for (const TObjectPtr<UFloorSpaceComponent> &FloorNodePtr : FloorNodes)
  {
    UFloorSpaceComponent *FloorNode = FloorNodePtr.Get();
    if (!FloorNode)
    {
      continue;
    }

    if (Matches(FloorNode->GetFloorId()))
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

UFloorSpaceComponent *UBuildingSpaceComponent::GetHighestFloorNode() const
{
  UFloorSpaceComponent *Result = nullptr;
  for (const TObjectPtr<UFloorSpaceComponent> &FloorNodePtr : FloorNodes)
  {
    UFloorSpaceComponent *FloorNode = FloorNodePtr.Get();
    if (FloorNode &&
        (!Result || FloorNode->FloorIndex > Result->FloorIndex))
    {
      Result = FloorNode;
    }
  }
  return Result;
}

UFloorSpaceComponent *UBuildingSpaceComponent::FindNodeWithActor(
    AActor *Actor) const
{
  AActor *Current = Actor;

  while (Current)
  {
    TObjectPtr<AActor> Key = Current;

    if (const TObjectPtr<UFloorSpaceComponent> *Ptr =
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

void UBuildingSpaceComponent::Reset()
{
  LayeringTimeline.Stop();
  FloorExpandTimeline.Stop();
  LiftDropTimeline.Stop();

  bDrawerRunning = false;
  bVisibilityRunning = false;
  bIsLayering = false;
  bLayeringRunning = false;
  bExpandRunning = false;
  bLiftDropRunning = false;

  LiftDropTransition =
      ELiftDropTransition::None;

  ActiveFloor =
      BUILDING_INVALID_FLOOR_INDEX;

  LayeredBaseLocationMap.Empty();
  ExpandStartOffsetMap.Empty();
  HiddenFloors.Empty();

  LiftDropStartLocationMap.Empty();
  LiftDropTargetLocationMap.Empty();
  LiftDropHideOnFinished.Empty();

  SetComponentTickEnabled(false);

  for (UFloorSpaceComponent *Node : AllFloorComponents)
  {
    if (!Node || !Node->RuntimeActor)
    {
      continue;
    }
    Node->ResetPresentation();
  }

  // 重置拆楼时恢复 Floor 下的完整 Actor 层级。
  for (UFloorSpaceComponent *Node : AllFloorComponents)
  {
    SetNodeTreeVisibility(Node, true);
  }

  RestoreLiftDropCeilingLocations();
  RestoreAllFloorCeilings();
}

void UBuildingSpaceComponent::EnterVisibilityDisassemble()
{
  if (!Config.bCanDisassemble)
  {
    return;
  }

  // 进入可见性拆楼时，目标楼栋只显示 Space 管理的
  // OrdinaryActors 和 Structs，AEffectActor 等排除对象保持隐藏。
  SetIsolationBuildingVisibility(true);

  if (Config.Visibility.bShowAllFloorsOnEnter)
  {
    ShowAll();
  }
}

void UBuildingSpaceComponent::ExitVisibilityDisassemble()
{
  SetWholeBuildingVisibility(true);
  ShowAll();
  RestoreAllFloorCeilings();
}

void UBuildingSpaceComponent::SetWholeBuildingVisibility(
    bool bVisible)
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  if (bVisible)
  {
    /*
     * 恢复楼栋根 Space。
     */
    BuildingSpace->SetActorHiddenInGame(false);

    /*
     * 恢复直接挂在楼栋 Space 下的普通模型。
     *
     * 没有楼层的楼栋，模型通常直接挂在楼栋 Space 下面，
     * 不会进入 AllFloorComponents，所以必须单独恢复。
     */
    BuildingSpace->SetOrdinaryActorsHidden(false);

    /*
     * 恢复直接挂在楼栋 Space 下的设备。
     *
     * 例如：
     * A-DT_KT-001 的 SpaceCode 是 A，
     * 它属于楼栋直属设备，不属于任何楼层，
     * 因此不会被下面的 AllFloorComponents 循环恢复。
     *
     * 这里只获取 Device 分区，不会恢复 Effect。
     */
    const TArray<ASpaceStruct *> BuildingDevices =
        BuildingSpace->GetStructByMark(TEXT("Device"));

    for (ASpaceStruct *DeviceStruct : BuildingDevices)
    {
      if (!IsValid(DeviceStruct))
      {
        continue;
      }

      SetActorTreeHidden(DeviceStruct, false);
    }

    /*
     * 恢复各楼层下的普通模型和设备。
     */
    for (UFloorSpaceComponent *Floor :
         AllFloorComponents)
    {
      if (!Floor)
      {
        continue;
      }

      SetNodeIsolationContentVisibility(Floor, true);
    }
  }
  else
  {
    /*
     * 隐藏时继续隐藏整棵 Actor 树，
     * 包括有楼层和没有楼层的楼栋。
     */
    SetActorTreeHidden(
        BuildingSpace.Get(),
        true);
  }
}

void UBuildingSpaceComponent::SetIsolationBuildingVisibility(
    bool bVisible)
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  /*
   * 先隐藏整棵 Actor 树，
   * 防止上一次显隐状态残留。
   *
   * 此处也会隐藏楼栋直属设备，
   * 所以下面必须把 Device 分区恢复。
   */
  SetActorTreeHidden(
      BuildingSpace.Get(),
      true);

  if (!bVisible)
  {
    return;
  }

  /*
   * 恢复楼栋根 Space。
   */
  BuildingSpace->SetActorHiddenInGame(false);

  /*
   * 恢复直接挂在楼栋 Space 下的普通模型。
   * 主要用于没有楼层节点的楼栋。
   */
  BuildingSpace->SetOrdinaryActorsHidden(false);

  /*
   * 恢复直接挂在楼栋 Space 下的设备。
   *
   * A-DT_KT-001 这种设备不会进入楼层恢复循环，
   * 如果这里不恢复，它会一直保持
   * SetActorHiddenInGame(true)。
   *
   * 这里只处理 Device，不会处理 Effect。
   */
  const TArray<ASpaceStruct *> BuildingDevices =
      BuildingSpace->GetStructByMark(TEXT("Device"));

  for (ASpaceStruct *DeviceStruct : BuildingDevices)
  {
    if (!IsValid(DeviceStruct))
    {
      continue;
    }

    SetActorTreeHidden(DeviceStruct, false);
  }

  /*
   * 恢复各楼层下的普通模型和设备。
   */
  for (UFloorSpaceComponent *Floor :
       AllFloorComponents)
  {
    if (!Floor)
    {
      continue;
    }

    SetNodeIsolationContentVisibility(
        Floor,
        true);
  }
}

bool UBuildingSpaceComponent::CanDisassembleBuilding() const
{
  return Config.bCanDisassemble;
}

bool UBuildingSpaceComponent::IsDrawerDisassembleMode() const
{
  return Config.bCanDisassemble &&
         Config.DisassembleMode == EDisassembleMode::Drawer;
}

bool UBuildingSpaceComponent::IsVisibilityDisassembleMode() const
{
  return Config.bCanDisassemble &&
         Config.DisassembleMode == EDisassembleMode::Visibility;
}

bool UBuildingSpaceComponent::ShouldHideOtherBuildingsOnVisibilityEnter() const
{
  return Config.Visibility.bHideOtherBuildingsOnEnter;
}

bool UBuildingSpaceComponent::ShouldRestoreOtherBuildingsOnVisibilityExit() const
{
  return Config.Visibility.bRestoreOtherBuildingsOnExit;
}

bool UBuildingSpaceComponent::ShouldHideOtherBuildingsOnLiftDropEnter() const
{
  return Config.LiftDrop.bHideOtherBuildingsOnEnter;
}

AActor *UBuildingSpaceComponent::GetBuildingActor() const
{
  return BuildingSpace;
}

FString UBuildingSpaceComponent::GetBuildingId() const
{
  return BuildingId;
}

void UBuildingSpaceComponent::SetBuildingStructureVisibility(
    bool bVisible)
{
  if (!IsValid(BuildingSpace))
  {
    return;
  }

  /*
   * Space Actor 必须保持显示。
   * 如果隐藏父级 Space，设备 POI 也无法显示。
   */
  BuildingSpace->SetActorHiddenInGame(false);

  /*
   * 只控制直接挂在楼栋 Space 下的普通结构模型。
   * 不会处理 Device 等 SpaceStruct。
   */
  BuildingSpace->SetOrdinaryActorsHidden(!bVisible);

  /*
   * 处理楼栋中的所有楼层结构。
   */
  for (UFloorSpaceComponent *Floor : AllFloorComponents)
  {
    if (!Floor || !Floor->RuntimeActor)
    {
      continue;
    }

    ASpace *FloorSpace =
        Cast<ASpace>(Floor->RuntimeActor.Get());

    if (!IsValid(FloorSpace))
    {
      continue;
    }

    // 楼层 Space 本身保持显示，避免影响其下的设备。
    FloorSpace->SetActorHiddenInGame(false);

    // 只隐藏楼层的普通建筑模型。
    FloorSpace->SetOrdinaryActorsHidden(!bVisible);

    // 处理 TH 天花和 WQ 外墙等特殊结构模型。
    TrySetFloorMeshHidden(
        Floor->RuntimeActor.Get(),
        !bVisible);
  }
}
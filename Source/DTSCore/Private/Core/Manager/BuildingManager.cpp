// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/Manager/BuildingManager.h"
#include "Core/Component/BuildingSpaceComponent.h"
#include "Core/Component/CampusSpaceComponent.h"
#include "Core/Effect/BuildingFocusIsolationComponent.h"
#include "Core/Effect/BuildingStencilChannels.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Space/Space.h"
#include "Space/SpaceSubsystem.h"

#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"

// =========================================================
// Constructor / BeginPlay / Tick
// =========================================================

ABuildingManager::ABuildingManager()
{
  PrimaryActorTick.bCanEverTick = true;

  FocusIsolationComponent =
      CreateDefaultSubobject<UBuildingFocusIsolationComponent>(
          TEXT("BuildingFocusIsolation"));
}

void ABuildingManager::BeginPlay()
{
  Super::BeginPlay();
  // 延迟到下一帧让subsystem先注册好数据再初始化
  GetWorldTimerManager().SetTimerForNextTick(
      this,
      &ABuildingManager::InitializeBuildings);
  ////初始化
  // InitializeBuildings();
}

void ABuildingManager::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  /*
   * 暂停后不执行任何射线。
   */
  if (!bBuildingTraceRunning)
  {
    return;
  }

  FHitResult Hit;
  const bool bHit = DoTrace(Hit);

  AActor *NewHitActor =
      bHit
          ? Hit.GetActor()
          : nullptr;

  AActor *OldHitActor =
      CurrentBuildingTraceActor.Get();

  /*
   * 鼠标命中的Actor发生变化。
   */
  if (OldHitActor != NewHitActor)
  {
    if (IsValid(OldHitActor))
    {
      OnBuildingTraceEndHover.Broadcast(
          OldHitActor);
    }

    CurrentBuildingTraceActor =
        NewHitActor;

    if (IsValid(NewHitActor))
    {
      OnBuildingTraceBeginHover.Broadcast(
          NewHitActor);
    }
  }

  /*
   * 当前命中的Actor被点击。
   */
  if (IsValid(NewHitActor))
  {
    APlayerController *PC =
        UGameplayStatics::GetPlayerController(
            this,
            0);

    if (PC &&
        PC->WasInputKeyJustPressed(
            EKeys::LeftMouseButton))
    {
      OnBuildingTraceClicked.Broadcast(
          NewHitActor,
          EKeys::LeftMouseButton);
    }
  }
}

// =========================================================
// Initialize
// 可拆楼栋：
// 初始化 Component
// 加入 BuildingMap
// 可以拆楼

// 不可拆楼栋：
// 初始化 Component
// 加入 BuildingMap
// 不能拆楼
// 但可以被 Visibility 模式隐藏 / 显示
//  =========================================================

void ABuildingManager::InitializeBuildings()
{
  // 如果当前有正在拆楼的楼栋，先退出
  ExitActiveDisassemble(true);
  if (FocusIsolationComponent)
  {
    // 旧 Runtime Actor/Component 即将销毁，不能等待淡出结束后再清理。
    FocusIsolationComponent->DeactivateIsolation(true);
  }

  // 清空当前状态
  SceneScope.Reset();

  HoverNode = nullptr;
  HoverComponent = nullptr;

  /*
   * 如果 InitializeBuildings 可能被重复调用，
   * 这里要把之前动态创建的 UBuildingSpaceComponent 销毁。
   *
   * 否则只 BuildingMap.Empty() 的话，
   * 旧组件还挂在 BuildingSpace 上，后面可能出现重复组件。
   */
  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    UBuildingSpaceComponent *OldComponent = Pair.Value.Get();

    if (OldComponent)
    {
      OldComponent->DestroyComponent();
    }
  }

  BuildingMap.Empty();
  HitComponentMap.Empty();
  HitNodeMap.Empty();

  if (!GetWorld())
  {
    return;
  }

  USpaceSubsystem *SpaceSubsystem =
      GetWorld()->GetSubsystem<USpaceSubsystem>();

  if (!IsValid(SpaceSubsystem))
  {
    UE_LOG(
        LogTemp,
        Error,
        TEXT("BuildingManager: SpaceSubsystem unavailable"));
    return;
  }

  SpaceSubsystem->SetSelectMode(
      ESpaceSelectMode::Single);

  TArray<ASpace *> Buildings = SpaceSubsystem->GetSpacesByMark(BuildingMark);
  if (bRefreshSourceDataOnBeginPlay)
  {
    for (ASpace *Space : SpaceSubsystem->GetSpaces())
    {
      if (IsValid(Space))
      {
        Space->InitializeSpaceData();
      }
    }
    Buildings = SpaceSubsystem->GetSpacesByMark(BuildingMark);
  }

  ASpace *CampusSpace = FindCampusSpace(Buildings);
  if (IsValid(CampusSpace))
  {
    CampusComponent = CampusSpace->FindComponentByClass<UCampusSpaceComponent>();
    if (!CampusComponent)
    {
      CampusComponent = NewObject<UCampusSpaceComponent>(CampusSpace, NAME_None, RF_Transient);
      CampusComponent->RegisterComponent();
    }
    CampusComponent->InitializeCampus(CampusSpace);
  }

  for (ASpace *BuildingSpace : Buildings)
  {
    if (!IsValid(BuildingSpace))
    {
      continue;
    }

    FBuildingFloorConfig *Config = FindConfigForBuilding(BuildingSpace);

    if (!Config)
    {
      UE_LOG(
          LogTemp,
          Warning,
          TEXT("BuildingManager: 找不到建筑配置 Actor=%s Id=%s DisplayName=%s"),
          *BuildingSpace->GetName(),
          *BuildingSpace->Code,
          *BuildingSpace->DisplayName);

      continue;
    }

    /*
     * 注意：
     * 这里不要因为 Config->bCanDisassemble == false 就跳过。
     *
     * 不可拆楼栋也要初始化进 BuildingMap，
     * 这样 Visibility 模式下，当前楼栋拆楼时，
     * Manager 才能隐藏 / 显示其他楼栋。
     */
    InitializeBuilding(BuildingSpace, *Config);
  }

  UE_LOG(
      LogTemp,
      Warning,
      TEXT("BuildingManager: 初始化建筑数量 = %d"),
      BuildingMap.Num());
}

// 把配置表里面的数据初始化到对应建筑的组件上
bool ABuildingManager::InitializeBuilding(
    ASpace *BuildingSpace,
    const FBuildingFloorConfig &Config)
{
  if (!IsValid(BuildingSpace))
  {
    return false;
  }

  // 防止重复初始化同一栋楼
  if (BuildingMap.Contains(BuildingSpace))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("BuildingManager: 建筑已初始化，跳过 Actor=%s Id=%s"),
        *BuildingSpace->GetName(),
        *BuildingSpace->Code);

    return true;
  }

  UClass *ComponentClass =
      Config.Component.ComponentClass
          ? Config.Component.ComponentClass.Get()
          : UBuildingSpaceComponent::StaticClass();

  UBuildingSpaceComponent *Component =
      BuildingSpace->FindComponentByClass<UBuildingSpaceComponent>();
  if (!Component)
  {
    Component = NewObject<UBuildingSpaceComponent>(BuildingSpace, ComponentClass, NAME_None, RF_Transient);
  }

  if (!Component)
  {
    return false;
  }

  if (!Component->IsRegistered())
  {
    Component->RegisterComponent();
  }
  Component->Initialize(BuildingSpace, Config, FloorMark);

  BuildingMap.Add(BuildingSpace, Component);
  if (CampusComponent)
  {
    CampusComponent->RegisterBuilding(Component);
  }
  IndexComponentHitActors(Component);

  if (!Config.bCanDisassemble)
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("BuildingManager: 建筑已加入管理，但不可拆 Actor=%s Id=%s Component=%s"),
        *BuildingSpace->GetName(),
        *BuildingSpace->Code,
        *Component->GetName());

    return true;
  }

  UE_LOG(
      LogTemp,
      Warning,
      TEXT("BuildingManager: 初始化可拆建筑成功 Actor=%s Id=%s Component=%s"),
      *BuildingSpace->GetName(),
      *BuildingSpace->Code,
      *Component->GetName());

  return true;
}

FBuildingFloorConfig *ABuildingManager::FindConfigForBuilding(
    const ASpace *BuildingSpace) const
{
  if (!BuildingFloorConfigTable || !IsValid(BuildingSpace))
  {
    return nullptr;
  }

  static const FString Context = TEXT("BuildingManager::FindConfigForBuilding");

  const FString Code = BuildingSpace->Code.TrimStartAndEnd();
  // 只根据Code和默认配置来检测
  if (!Code.IsEmpty())
  {
    if (FBuildingFloorConfig *Row =
            BuildingFloorConfigTable->FindRow<FBuildingFloorConfig>(
                FName(*Code),
                Context,
                false))
    {
      return Row;
    }
  }

  // if (!DisplayName.IsEmpty())
  //{
  //     if (FBuildingFloorConfig* Row =
  //         BuildingFloorConfigTable->FindRow<FBuildingFloorConfig>(
  //             FName(*DisplayName),
  //             Context,
  //             false))
  //     {
  //         return Row;
  //     }
  // }

  // if (FBuildingFloorConfig* Row =
  //     BuildingFloorConfigTable->FindRow<FBuildingFloorConfig>(
  //         FName(*BuildingSpace->GetName()),
  //         Context,
  //         false))
  //{
  //     return Row;
  // }

  if (bUseDefaultConfigRow)
  {
    if (FBuildingFloorConfig *Row =
            BuildingFloorConfigTable->FindRow<FBuildingFloorConfig>(
                DefaultConfigRowName,
                Context,
                false))
    {
      return Row;
    }
  }

  return nullptr;
}

ASpace *ABuildingManager::FindCampusSpace(const TArray<ASpace *> &BuildingSpaces) const
{
  if (BuildingSpaces.IsEmpty() || !IsValid(BuildingSpaces[0]))
  {
    return nullptr;
  }

  for (ASpace *Candidate = BuildingSpaces[0]->GetParentSpace();
       IsValid(Candidate);
       Candidate = Candidate->GetParentSpace())
  {
    bool bContainsAll = true;
    for (ASpace *Building : BuildingSpaces)
    {
      ASpace *Current = Building;
      while (IsValid(Current) && Current != Candidate)
      {
        Current = Current->GetParentSpace();
      }
      if (Current != Candidate)
      {
        bContainsAll = false;
        break;
      }
    }
    if (bContainsAll)
    {
      return Candidate;
    }
  }
  return nullptr;
}

// =========================================================
// Input
// =========================================================
// 射线检测
bool ABuildingManager::DoTrace(
    FHitResult &OutHit)
{
  OutHit = FHitResult();

  APlayerController *PC =
      UGameplayStatics::GetPlayerController(
          this,
          0);

  if (!IsValid(PC))
  {
    return false;
  }

  /*
   * 使用鼠标位置发射射线。
   */
  return PC->GetHitResultUnderCursor(
      TraceChannel,
      true,
      OutHit);
}
// 处理鼠标射线点击事件
void ABuildingManager::HandleClick(const FHitResult &Hit)
{
  UBuildingSpaceComponent *Component = nullptr;
  UFloorSpaceComponent *Node = nullptr;

  if (!ResolveHitNode(Hit.GetActor(), Component, Node) ||
      !Component ||
      !Node)
  {
    return;
  }

  AActor *BuildingActor = Component->GetBuildingActor();
  if (!IsValid(BuildingActor))
  {
    return;
  }

  if (Component->IsDrawerDisassembleMode())
  {
    /*
     * Drawer：
     * - 命中楼层时执行楼层点击；
     * - 命中楼栋根节点时执行整栋分层。
     */
    if (Node->FloorIndex != BUILDING_INVALID_FLOOR_INDEX)
    {
      ToggleBuildingFloor(BuildingActor, Node->FloorIndex);
    }
    else
    {
      EnterDrawerBuilding(BuildingActor, Component);
    }
  }
  else if (Component->IsVisibilityDisassembleMode())
  {
    // Visibility 永远只进入整栋楼模式。
    EnterVisibilityBuilding(BuildingActor, Component);
  }
  else if (Component->IsLiftDropDisassembleMode())
  {
    // LiftDrop 永远只进入整栋楼模式。
    EnterLiftDropBuilding(BuildingActor, Component);
  }
  else
  {
    return;
  }

  // /*
  //  * Visibility/LiftDrop 广播的是根节点，
  //  * FloorIndex 为 BUILDING_INVALID_FLOOR_INDEX。
  //  * 只有 Drawer 才可能广播具体楼层号。
  //  */
  // OnBuildingNodeClicked.Broadcast(Node, Node->FloorIndex);
}

void ABuildingManager::HandleHover(const FHitResult &Hit)
{
  AActor *HitActor = Hit.GetActor();
  if (!HitActor)
  {
    ClearHover();
    return;
  }

  UBuildingSpaceComponent *Component = nullptr;
  UFloorSpaceComponent *Node = nullptr;

  if (!ResolveHitNode(HitActor, Component, Node))
  {
    ClearHover();
    return;
  }

  if (HoverNode == Node)
  {
    return;
  }

  ClearHover();

  HoverNode = Node;
  HoverComponent = Component;

  if (HoverComponent && HoverNode)
  {
    HoverComponent->SetNodeHighlight(HoverNode, true);
  }

  // OnBuildingNodeHovered.Broadcast(
  //     Node,
  //     Node ? Node->FloorIndex : BUILDING_INVALID_FLOOR_INDEX);
}

void ABuildingManager::ClearHover()
{
  if (HoverComponent && HoverNode)
  {
    HoverComponent->SetNodeHighlight(HoverNode, false);
  }

  HoverNode = nullptr;
  HoverComponent = nullptr;
}

bool ABuildingManager::ResolveHitNode(
    AActor *HitActor,
    UBuildingSpaceComponent *&OutComponent,
    UFloorSpaceComponent *&OutNode) const
{
  OutComponent = nullptr;
  OutNode = nullptr;

  if (!IsValid(HitActor))
  {
    return false;
  }

  AActor *Current = HitActor;

  while (Current)
  {
    const TObjectPtr<UBuildingSpaceComponent> *ComponentPtr = HitComponentMap.Find(Current);

    const TObjectPtr<UFloorSpaceComponent> *NodePtr = HitNodeMap.Find(Current);

    if (ComponentPtr && NodePtr)
    {
      UBuildingSpaceComponent *Component = ComponentPtr->Get();
      UFloorSpaceComponent *Node = NodePtr->Get();

      if (!IsValid(Component) || !Node)
      {
        return false;
      }

      OutComponent = Component;

      OutNode = Node;

      return OutNode != nullptr;
    }

    // 命中子模型时继续向上查找所属拆楼节点。
    Current = Current->GetAttachParentActor();
  }

  return false;
}

void ABuildingManager::IndexComponentHitActors(
    UBuildingSpaceComponent *Component)
{
  if (!Component)
  {
    return;
  }

  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UFloorSpaceComponent>> &Pair :
       Component->ActorMap)
  {
    AActor *Actor = Pair.Key.Get();
    UFloorSpaceComponent *Node = Pair.Value.Get();
    if (IsValid(Actor) && Node)
    {
      HitComponentMap.Add(Actor, Component);
      HitNodeMap.Add(Actor, Node);
    }
  }
}

void ABuildingManager::SetActorHighlight(
    AActor *Actor,
    bool bEnable)
{
  if (!Actor)
  {
    return;
  }

  TArray<UPrimitiveComponent *> PrimComps;
  Actor->GetComponents<UPrimitiveComponent>(PrimComps);

  for (UPrimitiveComponent *Prim : PrimComps)
  {
    if (!Prim)
    {
      continue;
    }

    uint8 StencilValue =
        static_cast<uint8>(Prim->CustomDepthStencilValue);
    if (bEnable)
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

void ABuildingManager::ExitActiveDisassemble(bool bRestoreCamera)
{
  GetWorldTimerManager().ClearTimer(LiftDropFocusReadyTimer);

  bLiftDropFocusReady = false;

  UBuildingSpaceComponent *ActiveComponent =
      SceneScope.NowComponent.Get();

  if (!ActiveComponent)
  {
    if (FocusIsolationComponent)
    {
      FocusIsolationComponent->DeactivateIsolation(false);
    }

    ClearSceneScope();

    if (bRestoreCamera)
    {
      RestoreCameraFocusView();
    }

    return;
  }

  const bool bShouldRestoreCamera =
      bRestoreCamera &&
      ActiveComponent->GetFocusSettings().bRestoreViewOnExit;

  ActiveComponent->OnLayeringTimeLineFinished.RemoveDynamic(
      this,
      &ABuildingManager::HandleLayeringFocusFinished);

  ActiveComponent->OnFloorExpandTimelineFinished.RemoveDynamic(
      this,
      &ABuildingManager::HandleFloorFocusFinished);

  switch (SceneScope.NowMode)
  {
  case EBuildingActiveDisassembleMode::Drawer:
    ActiveComponent->BackToNormal();
    break;

  case EBuildingActiveDisassembleMode::Visibility:
    ActiveComponent->ExitVisibilityDisassemble();
    RestoreAllBuildingsVisibility();
    break;

  case EBuildingActiveDisassembleMode::LiftDrop:
    ActiveComponent->ExitLiftDropDisassemble();
    RestoreAllBuildingsVisibility();
    break;

  case EBuildingActiveDisassembleMode::None:
  default:
    break;
  }

  if (FocusIsolationComponent)
  {
    FocusIsolationComponent->DeactivateIsolation(false);
  }

  ClearSceneScope();

  if (bShouldRestoreCamera)
  {
    RestoreCameraFocusView();
  }
  else if (bRestoreCamera)
  {
    ClearCameraFocusView();
  }
}

void ABuildingManager::EnterDrawerBuilding(
    AActor *BuildingActor,
    UBuildingSpaceComponent *Component)
{
  if (!BuildingActor || !Component)
  {
    return;
  }

  if (SceneScope.NowComponent != Component ||
      SceneScope.NowMode != EBuildingActiveDisassembleMode::Drawer)
  {
    ExitActiveDisassemble(false);
    RestoreAllBuildingsVisibility();

    SaveCameraFocusViewIfNeeded();

    SetSceneScopeBuilding(
        BuildingActor,
        Component,
        EBuildingActiveDisassembleMode::Drawer);

    if (FocusIsolationComponent)
    {
      FocusIsolationComponent->ActivateIsolation(Component);
    }

    const FBuildingFocusSettings &FocusSettings =
        Component->GetFocusSettings();
    if (FocusSettings.bAutoFocusOnEnter)
    {
      if (FocusSettings.bFocusAfterLayering)
      {
        Component->OnLayeringTimeLineFinished.RemoveDynamic(
            this,
            &ABuildingManager::HandleLayeringFocusFinished);
        Component->OnLayeringTimeLineFinished.AddDynamic(
            this,
            &ABuildingManager::HandleLayeringFocusFinished);
      }
      else
      {
        RequestBuildingFocus(Component);
      }
    }
  }

  Component->LayeringDisplay();
}

void ABuildingManager::EnterVisibilityBuilding(
    AActor *BuildingActor,
    UBuildingSpaceComponent *Component)
{
  if (!BuildingActor || !Component)
  {
    return;
  }

  if (SceneScope.NowComponent != Component ||
      SceneScope.NowMode != EBuildingActiveDisassembleMode::Visibility)
  {
    ExitActiveDisassemble(false);

    SaveCameraFocusViewIfNeeded();

    SetSceneScopeBuilding(
        BuildingActor,
        Component,
        EBuildingActiveDisassembleMode::Visibility);

    if (FocusIsolationComponent)
    {
      FocusIsolationComponent->ActivateIsolation(Component);
    }

    if (Component->ShouldHideOtherBuildingsOnVisibilityEnter())
    {
      /*
       * 显隐拆楼配置要求隐藏其他楼栋时必须优先执行真实显隐。
       * 隔离后处理只负责压暗剩余可见场景，不能替代楼栋隐藏。
       */
      SetOnlyBuildingVisible(Component);
    }
    else
    {
      /*
       * 配置允许保留其他楼栋时，当前目标楼栋写入 Isolation Stencil，
       * 其他可见场景由后处理压黑。这里只恢复目标楼栋受 Space 管理
       * 的内容，避免错误显示业务 Effect。
       */
      Component->SetIsolationBuildingVisibility(true);
      Component->ShowAll();
    }

    Component->EnterVisibilityDisassemble();

    // Visibility 不改变楼层位置，完成显隐切换后即可聚焦整栋楼。
    RequestBuildingFocus(Component);
  }
}

UObject *ABuildingManager::ResolveCameraFocusProvider() const
{
  UObject *Provider = CameraFocusProvider.GetObject();
  if (IsValid(Provider) &&
      Provider->GetClass()->ImplementsInterface(
          UBuildingCameraFocusInterface::StaticClass()))
  {
    return Provider;
  }

  if (APlayerController *PlayerController =
          UGameplayStatics::GetPlayerController(this, 0))
  {
    APawn *Pawn = PlayerController->GetPawn();
    if (IsValid(Pawn) &&
        Pawn->GetClass()->ImplementsInterface(
            UBuildingCameraFocusInterface::StaticClass()))
    {
      return Pawn;
    }
  }

  return nullptr;
}

void ABuildingManager::SaveCameraFocusViewIfNeeded()
{
  if (bCameraFocusViewSaved)
  {
    return;
  }

  if (UObject *Provider = ResolveCameraFocusProvider())
  {
    IBuildingCameraFocusInterface::Execute_SaveBuildingFocusView(Provider);
    bCameraFocusViewSaved = true;
  }
}
// 聚焦楼栋
bool ABuildingManager::RequestBuildingFocus(
    UBuildingSpaceComponent *Component)
{
  if (!Component || !Component->GetFocusSettings().bAutoFocusOnEnter)
  {
    return false;
  }

  UObject *Provider = ResolveCameraFocusProvider();
  if (!Provider)
  {
    return false;
  }

  const FBuildingFocusTarget Target = Component->GetBuildingFocusTarget();
  if (!Target.bIsValid)
  {
    return false;
  }

  IBuildingCameraFocusInterface::Execute_FocusBuildingTarget(
      Provider,
      Target,
      Component->GetFocusSettings());

  return true;
}
// 聚焦楼层
void ABuildingManager::RequestFloorFocus(
    UBuildingSpaceComponent *Component,
    UFloorSpaceComponent *FloorNode)
{
  if (!Component || !FloorNode)
  {
    return;
  }

  const FBuildingFocusSettings FloorFocusSettings =
      Component->GetFloorFocusSettings(FloorNode->FloorIndex);
  if (!FloorFocusSettings.bAutoFocusFloorOnSelect)
  {
    return;
  }

  UObject *Provider = ResolveCameraFocusProvider();
  if (!Provider)
  {
    return;
  }

  const FBuildingFocusTarget Target =
      Component->GetFloorWallFocusTarget(FloorNode->FloorIndex);
  if (!Target.bIsValid)
  {
    return;
  }

  IBuildingCameraFocusInterface::Execute_FocusBuildingTarget(
      Provider,
      Target,
      FloorFocusSettings);
}

void ABuildingManager::RestoreCameraFocusView()
{
  if (!bCameraFocusViewSaved)
  {
    return;
  }

  if (UObject *Provider = ResolveCameraFocusProvider())
  {
    IBuildingCameraFocusInterface::Execute_RestoreBuildingFocusView(Provider);
  }
  bCameraFocusViewSaved = false;
}

void ABuildingManager::ClearCameraFocusView()
{
  if (!bCameraFocusViewSaved)
  {
    return;
  }

  if (UObject *Provider = ResolveCameraFocusProvider())
  {
    IBuildingCameraFocusInterface::Execute_ClearBuildingFocusView(Provider);
  }
  bCameraFocusViewSaved = false;
}

void ABuildingManager::HandleLayeringFocusFinished()
{
  UBuildingSpaceComponent *Component = SceneScope.NowComponent.Get();
  if (!Component ||
      SceneScope.NowMode != EBuildingActiveDisassembleMode::Drawer)
  {
    return;
  }

  Component->OnLayeringTimeLineFinished.RemoveDynamic(
      this,
      &ABuildingManager::HandleLayeringFocusFinished);
  RequestBuildingFocus(Component);
}

void ABuildingManager::HandleFloorFocusFinished()
{
  UBuildingSpaceComponent *Component =
      SceneScope.NowComponent.Get();

  const bool bCanFocusFloor =
      SceneScope.NowMode ==
          EBuildingActiveDisassembleMode::Drawer ||
      SceneScope.NowMode ==
          EBuildingActiveDisassembleMode::LiftDrop;

  if (!Component || !bCanFocusFloor)
  {
    return;
  }

  Component->OnFloorExpandTimelineFinished.RemoveDynamic(this, &ABuildingManager::HandleFloorFocusFinished);

  if (UFloorSpaceComponent *FloorNode = SceneScope.NowFloorNode.Get())
  {
    /*
     * 这里会根据 FloorIndex 查找 FloorFocusOverrides，
     * 然后使用对应楼层的 CameraLocation、Pitch、Yaw 等配置。
     */
    RequestFloorFocus(
        Component,
        FloorNode);
  }
  else if (Component->GetFocusSettings().bRefocusBuildingOnFloorClose)
  {
    RequestBuildingFocus(Component);
  }
}

void ABuildingManager::RestoreAllBuildingsVisibility()
{
  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    UBuildingSpaceComponent *Component = Pair.Value.Get();

    if (!Component)
    {
      continue;
    }

    Component->SetWholeBuildingVisibility(true);
    Component->ShowAll();
  }
}

// 设置某个楼栋显示其他楼栋隐藏的核心逻辑
void ABuildingManager::SetOnlyBuildingVisible(
    UBuildingSpaceComponent *VisibleComponent)
{
  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    UBuildingSpaceComponent *Component = Pair.Value.Get();

    if (!Component)
    {
      continue;
    }
    // 当前选中的楼栋为 true，其他楼栋为 false
    const bool bVisible = Component == VisibleComponent;

    Component->SetIsolationBuildingVisibility(bVisible);

    if (bVisible)
    {
      Component->ShowAll();
    }
  }
}

void ABuildingManager::EnterLiftDropBuilding(AActor *BuildingActor, UBuildingSpaceComponent *Component)
{
  if (!BuildingActor ||
      !Component ||
      !Component->IsLiftDropDisassembleMode())
  {
    return;
  }
  // 已经进入当前楼栋，不重复启动聚焦。
  if (SceneScope.NowComponent == Component && SceneScope.NowMode == EBuildingActiveDisassembleMode::LiftDrop)
  {
    return;
  }
  ExitActiveDisassemble(false);

  SaveCameraFocusViewIfNeeded();

  SetSceneScopeBuilding(BuildingActor, Component, EBuildingActiveDisassembleMode::LiftDrop);

  if (FocusIsolationComponent)
  {
    FocusIsolationComponent->ActivateIsolation(Component);
  }

  // LiftDrop 使用自己的配置决定是否隐藏其他未拆楼的楼栋。
  if (Component->ShouldHideOtherBuildingsOnLiftDropEnter())
  {
    SetOnlyBuildingVisible(Component);
  }
  else
  {
    RestoreAllBuildingsVisibility();

    Component->SetIsolationBuildingVisibility(true);
  }

  Component->EnterLiftDropDisassemble();

  bLiftDropFocusReady = false;

  GetWorldTimerManager().ClearTimer(LiftDropFocusReadyTimer);

  const FBuildingFocusSettings &FocusSettings = Component->GetFocusSettings();

  if (!RequestBuildingFocus(Component))
  {
    return;
  }

  GetWorldTimerManager().SetTimer(
      LiftDropFocusReadyTimer,
      this,
      &ABuildingManager::
          HandleLiftDropFocusReady,
      FMath::Max(
          FocusSettings.InterpTime,
          0.01f),
      false);
}

void ABuildingManager::HandleLiftDropFocusReady()
{
  if (SceneScope.NowMode !=
          EBuildingActiveDisassembleMode::LiftDrop ||
      !SceneScope.NowComponent)
  {
    bLiftDropFocusReady = false;
    return;
  }

  bLiftDropFocusReady = true;

  UE_LOG(
      LogTemp,
      Log,
      TEXT("BuildingManager: LiftDrop 聚焦完成，允许点击楼层。"));
}

// =========================================================
// Public API
// =========================================================

UBuildingSpaceComponent *ABuildingManager::GetBuildingComponent(
    AActor *BuildingActor) const
{
  if (!BuildingActor)
  {
    return nullptr;
  }

  if (const TObjectPtr<UBuildingSpaceComponent> *Ptr =
          BuildingMap.Find(BuildingActor))
  {
    return Ptr->Get();
  }

  return nullptr;
}

void ABuildingManager::SetAllBuildingsVisibility(bool bVisible)
{
  // 全部显隐属于场景级切换，不能保留旧的拆楼激活状态。
  ExitActiveDisassemble(false);

  if (bVisible)
  {
    RestoreAllBuildingsVisibility();
    return;
  }

  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    UBuildingSpaceComponent *Component = Pair.Value.Get();
    if (!Component)
    {
      continue;
    }

    // 先还原楼层位置和组件内部状态，再通过拆楼组件隐藏整栋楼。
    Component->Reset();
    Component->SetWholeBuildingVisibility(false);
  }
}

bool ABuildingManager::SetBuildingVisibilityByCode(
    const FString &BuildingCode,
    bool bVisible)
{
  AActor *BuildingActor =
      FindBuildingActorByCode(BuildingCode);

  UBuildingSpaceComponent *Component =
      GetBuildingComponent(BuildingActor);

  if (!IsValid(BuildingActor) ||
      !IsValid(Component))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "[BuildingManager] "
            "SetBuildingVisibilityByCode failed, Code=%s"),
        *BuildingCode);

    return false;
  }

  /*
   * 只修改目标楼栋。
   *
   * 不调用SetAllBuildingsVisibility，
   * 所以不会恢复其他楼栋，
   * 也不会清除当前SceneScope。
   */
  Component->SetWholeBuildingVisibility(bVisible);

  return true;
}

void ABuildingManager::HideAllBuildingContentPreserveDisassemble()
{
  /*
   * 设备聚焦已经等到拆楼、选层和拆楼相机全部结束后才调用这里。
   * 只隐藏各组件管理的 Actor 树，不调用 ExitActiveDisassemble、
   * Reset 或 ShowAll，避免清空 SceneScope，也避免旧楼层状态回写。
   */
  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    if (UBuildingSpaceComponent *Component = Pair.Value.Get())
    {
      Component->SetWholeBuildingVisibility(false);
    }
  }
}

void ABuildingManager::RestoreBuildingContentFromPreservedDisassemble()
{
  UBuildingSpaceComponent *ActiveComponent =
      SceneScope.NowComponent.Get();

  if (!ActiveComponent)
  {
    RestoreAllBuildingsVisibility();
    return;
  }

  if (SceneScope.NowMode ==
      EBuildingActiveDisassembleMode::Visibility)
  {
    if (ActiveComponent->ShouldHideOtherBuildingsOnVisibilityEnter())
    {
      SetOnlyBuildingVisible(ActiveComponent);
    }
    else
    {
      RestoreAllBuildingsVisibility();
    }

    ActiveComponent->EnterVisibilityDisassemble();

    int32 RestoreFloorIndex =
        SceneScope.NowFloorIndex;

    /*
     * 兼容单层业务页：
     * 旧逻辑可能只记录了当前楼栋，并由业务代码直接隐藏 TH，
     * 没有把唯一楼层写入 SceneScope。设备独显期间整栋内容被隐藏，
     * 右键返回时 EnterVisibilityDisassemble 会恢复楼层内容；如果此时
     * 不重新应用楼层显隐，TH 就会重新出现。
     */
    if (RestoreFloorIndex ==
            BUILDING_INVALID_FLOOR_INDEX &&
        ActiveComponent->GetFloorCount() == 1)
    {
      if (UFloorSpaceComponent *UniqueFloorNode =
              ActiveComponent->GetHighestFloorNode())
      {
        RestoreFloorIndex =
            UniqueFloorNode->FloorIndex;
        SetSceneScopeFloor(UniqueFloorNode);
      }
    }

    if (RestoreFloorIndex !=
        BUILDING_INVALID_FLOOR_INDEX)
    {
      ActiveComponent->ToggleFloorVisibility(
          RestoreFloorIndex);
    }
    return;
  }

  /*
   * Drawer 模式的楼层 Transform 在设备独显期间没有被重置。
   * 这里只恢复各楼栋内容显隐；展开/分层位置和 SceneScope 保持不变。
   */
  RestoreAllBuildingsVisibility();
}

// 设置当前选中楼栋
void ABuildingManager::SetSceneScopeBuilding(
    AActor *BuildingActor,
    UBuildingSpaceComponent *Component,
    EBuildingActiveDisassembleMode Mode)
{
  SceneScope.NowBuilding = BuildingActor;
  SceneScope.NowBuildingId =
      IsValid(Component)
          ? Component->GetBuildingId()
          : FString();

  SceneScope.NowComponent = Component;
  SceneScope.NowMode = Mode;

  if (CampusComponent)
  {
    CampusComponent->SelectBuilding(
        Component,
        false);
  }
  else if (IsValid(Component))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "[BuildingManager] CampusComponent为空，"
            "直接发送楼栋选择：%s"),
        *Component->GetBuildingId());

    Component->SelectSingle();
  }

  SceneScope.NowFloor = nullptr;
  SceneScope.NowFloorId.Reset();
  SceneScope.NowFloorNode = nullptr;
  SceneScope.NowFloorIndex =
      BUILDING_INVALID_FLOOR_INDEX;
}
// 设置当前选中楼层
void ABuildingManager::SetSceneScopeFloor(
    UFloorSpaceComponent *FloorNode)
{
  if (!IsValid(FloorNode))
  {
    ClearSceneScopeFloor();
    return;
  }

  SceneScope.NowFloorNode = FloorNode;
  SceneScope.NowFloor = FloorNode->RuntimeActor;
  SceneScope.NowFloorId = FloorNode->GetFloorId();
  SceneScope.NowFloorIndex = FloorNode->FloorIndex;

  if (CampusComponent)
  {
    CampusComponent->SelectFloor(
        FloorNode,
        false);
  }
  else
  {
    FloorNode->SelectSingle();
  }
}

void ABuildingManager::ClearSceneScope()
{
  if (CampusComponent)
  {
    CampusComponent->ClearActiveSelection();
  }
  else if (UWorld *World = GetWorld())
  {
    if (USpaceSubsystem *SpaceSubsystem =
            World->GetSubsystem<USpaceSubsystem>())
    {
      SpaceSubsystem->ClearSelection();
    }
  }

  SceneScope.Reset();
}

bool ABuildingManager::FocusBuildingByCode(const FString &BuildingCode)
{
  // 拿到楼栋角色
  AActor *BuildingActor = FindBuildingActorByCode(BuildingCode);
  // 拿到楼栋组件
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);
  if (!IsValid(BuildingActor) || !Component)
  {
    return false;
  }

  const bool bAlreadyFocused =
      SceneScope.NowComponent == Component &&
      SceneScope.NowMode ==
          EBuildingActiveDisassembleMode::None;

  if (!bAlreadyFocused)
  {
    // 退出上一个拆楼状态，但保留最初的镜头恢复记录。
    ExitActiveDisassemble(false);
    // // 确保没有楼栋受到上一个页面的显隐影响。
    // RestoreAllBuildingsVisibility();
    SaveCameraFocusViewIfNeeded();

    // 记录当前楼栋，但不进入任何拆楼模式。
    SetSceneScopeBuilding(
        BuildingActor,
        Component,
        EBuildingActiveDisassembleMode::None);
  }

  RequestBuildingFocus(Component);
  return true;
}

// 进入拆楼的模式分发：
void ABuildingManager::LayeringBuilding(AActor *BuildingActor)
{
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);

  if (!IsValid(BuildingActor) || !Component)
  {
    return;
  }

  if (Component->GetFloorCount() == 0)
  {
    FocusBuildingByCode(Component->GetBuildingId());
    return;
  }

  if (!Component ||
      !Component->CanDisassembleBuilding())
  {
    return;
  }

  if (Component->IsVisibilityDisassembleMode())
  {
    EnterVisibilityBuilding(BuildingActor, Component);
    return;
  }

  if (Component->IsLiftDropDisassembleMode())
  {
    EnterLiftDropBuilding(BuildingActor, Component);
    return;
  }

  if (Component->IsDrawerDisassembleMode())
  {
    EnterDrawerBuilding(BuildingActor, Component);
  }
}

void ABuildingManager::BackBuildingToNormal(AActor *BuildingActor)
{
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);

  if (!Component)
  {
    return;
  }

  if (Component != SceneScope.NowComponent)
  {
    return;
  }

  ExitActiveDisassemble(true);
  RestoreAllBuildingsVisibility();
}

// 拆楼主入口
void ABuildingManager::ToggleBuildingFloor(
    AActor *BuildingActor,
    int32 Floor)
{
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);

  if (!Component)
  {
    return;
  }
  // 是否能拆
  if (!Component->CanDisassembleBuilding())
  {
    return;
  }

  UFloorSpaceComponent *FloorNode = Component->FindFloorWithIndex(Floor);

  if (!FloorNode || !FloorNode->bCanDisassemble)
  {
    return;
  }
  // 看组件是哪个模式用哪个拆
  if (Component->IsVisibilityDisassembleMode())
  {
    EnterVisibilityBuilding(BuildingActor, Component);
    Component->ToggleFloorVisibility(Floor);
    SetSceneScopeFloor(FloorNode);
    // Visibility 没有楼层位移动画，显隐应用后立即收紧到当前楼层。
    RequestFloorFocus(Component, FloorNode);
    return;
  }

  if (Component->IsLiftDropDisassembleMode())
  {
    const bool bSameBuilding =
        SceneScope.NowComponent == Component &&
        SceneScope.NowMode ==
            EBuildingActiveDisassembleMode::LiftDrop;

    if (!bSameBuilding)
    {
      // 第一次点击只进入楼栋并聚焦，不立即拆楼。
      EnterLiftDropBuilding(BuildingActor, Component);

      return;
    }

    // 聚焦尚未完成。
    if (!bLiftDropFocusReady)
    {
      return;
    }

    // 上一次掉落/抬升动画尚未完成。
    if (Component->IsLiftDropRunning())
    {
      return;
    }

    if (!Component->SelectLiftDropFloor(Floor))
    {
      return;
    }

    UFloorSpaceComponent *ActiveFloorNode =
        Component->FindFloorWithIndex(Floor);

    SetSceneScopeFloor(ActiveFloorNode);

    /*
     * LiftDrop 楼层存在上下移动动画。
     *
     * 必须等动画结束后再聚焦，否则计算到的是楼层移动前，
     * 或者移动过程中的 Bounds。
     */
    if (Component->IsLiftDropRunning())
    {
      Component->OnFloorExpandTimelineFinished.RemoveDynamic(this, &ABuildingManager::HandleFloorFocusFinished);

      Component->OnFloorExpandTimelineFinished.AddDynamic(this, &ABuildingManager::HandleFloorFocusFinished);
    }
    else
    {
      /*
       * 没有移动对象、没有动画曲线或动画时间为 0 时，
       * SelectLiftDropFloor 会同步完成，此时直接聚焦。
       */
      RequestFloorFocus(Component, ActiveFloorNode);
    }

    return;
  }

  if (Component->IsDrawerDisassembleMode())
  {
    EnterDrawerBuilding(BuildingActor, Component);

    // 整栋分层仍在播放时不提前改变楼层状态；现有 UI 会在分层
    // 完成事件后重试本次选层。
    if (!Component->IsLayering() ||
        Component->IsLayeringRunning() ||
        Component->IsExpandRunning())
    {
      return;
    }

    Component->ToggleFloor(Floor);

    // ToggleFloor 可能因为分层动画仍在运行而拒绝本次操作
    // 再次点击当前楼层将其收回。以组件实际状态回写 SceneScope
    const int32 ActiveFloor = Component->GetActiveFloor();
    if (ActiveFloor == BUILDING_INVALID_FLOOR_INDEX)
    {
      ClearSceneScopeFloor();
    }
    else
    {
      SetSceneScopeFloor(Component->FindFloorWithIndex(ActiveFloor));
    }

    if (Component->IsExpandRunning())
    {
      // 楼层移动完成后再按移动后的 Bounds 聚焦。
      Component->OnFloorExpandTimelineFinished.RemoveDynamic(
          this,
          &ABuildingManager::HandleFloorFocusFinished);
      Component->OnFloorExpandTimelineFinished.AddDynamic(
          this,
          &ABuildingManager::HandleFloorFocusFinished);
    }
    else if (UFloorSpaceComponent *ActiveFloorNode =
                 SceneScope.NowFloorNode.Get())
    {
      // 没有曲线或零时长动画时直接聚焦。
      RequestFloorFocus(Component, ActiveFloorNode);
    }
    else if (Component->GetFocusSettings().bRefocusBuildingOnFloorClose)
    {
      RequestBuildingFocus(Component);
    }
    return;
  }
}

void ABuildingManager::LayeringCurrentBuilding()
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  if (!BuildingActor)
  {
    return;
  }

  LayeringBuilding(BuildingActor);
}

bool ABuildingManager::BackCurrentBuildingToNormal()
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  if (!IsValid(BuildingActor))
  {
    ExitActiveDisassemble(true);
    RestoreAllBuildingsVisibility();
    return false;
  }

  BackBuildingToNormal(BuildingActor);
  return true;
}

AActor *ABuildingManager::FindBuildingActorByCode(const FString &BuildingCode) const
{
  const FString TargetCode = BuildingCode.TrimStartAndEnd();

  if (TargetCode.IsEmpty())
  {
    return nullptr;
  }

  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    AActor *BuildingActor = Pair.Key.Get();
    UBuildingSpaceComponent *Component = Pair.Value.Get();

    if (!IsValid(BuildingActor) || !Component)
    {
      continue;
    }

    if (Component->GetBuildingId().Equals(TargetCode, ESearchCase::IgnoreCase))
    {
      return BuildingActor;
    }

    if (BuildingActor->GetName().Equals(TargetCode, ESearchCase::IgnoreCase))
    {
      return BuildingActor;
    }
  }

  return nullptr;
}

bool ABuildingManager::LayeringBuildingByCode(
    const FString &BuildingCode)
{
  AActor *BuildingActor =
      FindBuildingActorByCode(BuildingCode);

  UBuildingSpaceComponent *Component =
      GetBuildingComponent(BuildingActor);

  if (!IsValid(BuildingActor) ||
      !IsValid(Component))
  {
    return false;
  }

  LayeringBuilding(BuildingActor);

  const bool bEntered =
      SceneScope.NowBuilding == BuildingActor &&
      SceneScope.NowComponent == Component;

  UE_LOG(
      LogTemp,
      Warning,
      TEXT(
          "[BuildingManager] LayeringBuildingByCode "
          "Code=%s Entered=%s"),
      *BuildingCode,
      bEntered ? TEXT("true") : TEXT("false"));

  return bEntered;
}

bool ABuildingManager::EnterVisibilityBuildingByCode(
    const FString &BuildingCode)
{
  AActor *BuildingActor = FindBuildingActorByCode(BuildingCode);
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);

  if (!IsValid(BuildingActor) ||
      !Component ||
      !Component->CanDisassembleBuilding())
  {
    return false;
  }

  EnterVisibilityBuilding(BuildingActor, Component);

  return SceneScope.NowBuilding == BuildingActor &&
         SceneScope.NowComponent == Component &&
         SceneScope.NowMode == EBuildingActiveDisassembleMode::Visibility;
}

bool ABuildingManager::ToggleVisibilityBuildingFloorByCode(
    const FString &BuildingCode,
    int32 Floor)
{
  AActor *BuildingActor = FindBuildingActorByCode(BuildingCode);
  UBuildingSpaceComponent *Component = GetBuildingComponent(BuildingActor);

  if (!IsValid(BuildingActor) ||
      !Component ||
      !Component->CanDisassembleBuilding())
  {
    return false;
  }

  UFloorSpaceComponent *FloorNode = Component->FindFloorWithIndex(Floor);
  if (!FloorNode || !FloorNode->bCanDisassemble)
  {
    return false;
  }

  EnterVisibilityBuilding(BuildingActor, Component);
  Component->ToggleFloorVisibility(Floor);
  SetSceneScopeFloor(FloorNode);
  RequestFloorFocus(Component, FloorNode);
  return true;
}

bool ABuildingManager::BackBuildingByCode(const FString &BuildingCode)
{
  AActor *BuildingActor = FindBuildingActorByCode(BuildingCode);

  if (!IsValid(BuildingActor))
  {
    return false;
  }

  BackBuildingToNormal(BuildingActor);
  return true;
}

bool ABuildingManager::ToggleBuildingFloorByCode(
    const FString &BuildingCode,
    int32 Floor)
{
  AActor *BuildingActor = FindBuildingActorByCode(BuildingCode);

  if (!IsValid(BuildingActor))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("BuildingManager: ToggleBuildingFloorByCode 找不到楼栋 Code=%s Floor=%d"),
        *BuildingCode,
        Floor);

    return false;
  }

  ToggleBuildingFloor(BuildingActor, Floor);
  return true;
}

bool ABuildingManager::ToggleCurrentBuildingFloor(int32 Floor)
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  if (!IsValid(BuildingActor))
  {
    return false;
  }

  ToggleBuildingFloor(BuildingActor, Floor);
  return true;
}

void ABuildingManager::LayeringAllBuildings()
{
  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    if (UBuildingSpaceComponent *Component = Pair.Value.Get())
    {
      Component->LayeringDisplay();
    }
  }
}

void ABuildingManager::BackAllBuildingsToNormal()
{
  UBuildingSpaceComponent *ActiveComponent = SceneScope.NowComponent.Get();
  ExitActiveDisassemble(true);

  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    if (UBuildingSpaceComponent *Component = Pair.Value.Get())
    {
      if (Component != ActiveComponent)
      {
        Component->BackToNormal();
      }
    }
  }

  RestoreAllBuildingsVisibility();
}

// =========================================================

void ABuildingManager::HandleDebugHotkeys()
{
  if (!bEnableDebugHotkeys)
  {
    return;
  }

  APlayerController *PC = UGameplayStatics::GetPlayerController(this, 0);
  if (!PC)
  {
    return;
  }

  // F1：重新初始化
  /*if (PC->WasInputKeyJustPressed(EKeys::R))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] F1 InitializeBuildings"));
    InitializeBuildings();
    return;
  }*/

  //// F2：下一栋楼并拆楼
  // if (PC->WasInputKeyJustPressed(EKeys::Q))
  //{
  //     UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] F2 DebugNextBuilding"));
  //     DebugNextBuilding();
  //     return;
  // }

  // C：拆当前楼栋
  if (PC->WasInputKeyJustPressed(EKeys::C))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] F3 DebugLayeringCurrentBuilding"));
    DebugLayeringCurrentBuilding();
    return;
  }

  // R：还原当前楼栋
  if (PC->WasInputKeyJustPressed(EKeys::R))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] F4 DebugBackCurrentBuilding"));
    DebugBackCurrentBuilding();
    return;
  }

  // Q：测试 -1 层，例如 FSSJQ
  if (PC->WasInputKeyJustPressed(EKeys::Q))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] Q Toggle Floor -1"));
    DebugToggleCurrentFloor(1);
    return;
  }

  // T：测试 RF，按 9999
  if (PC->WasInputKeyJustPressed(EKeys::T))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] R Toggle RF 9999"));
    DebugToggleCurrentFloor(9999);
    return;
  }

  /* if (PC->WasInputKeyJustPressed(EKeys::One))
   {
     DebugToggleCurrentFloor(1);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Two))
   {
     DebugToggleCurrentFloor(2);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Three))
   {
     DebugToggleCurrentFloor(3);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Four))
   {
     DebugToggleCurrentFloor(4);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Five))
   {
     DebugToggleCurrentFloor(5);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Six))
   {
     DebugToggleCurrentFloor(6);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Seven))
   {
     DebugToggleCurrentFloor(7);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Eight))
   {
     DebugToggleCurrentFloor(8);
     return;
   }

   if (PC->WasInputKeyJustPressed(EKeys::Nine))
   {
     DebugToggleCurrentFloor(9);
     return;
   }*/
}

void ABuildingManager::GetDebugBuildingActors(
    TArray<AActor *> &OutBuildings) const
{
  OutBuildings.Reset();

  for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    AActor *BuildingActor = Pair.Key.Get();
    UBuildingSpaceComponent *Component = Pair.Value.Get();

    if (!IsValid(BuildingActor) || !Component)
    {
      continue;
    }

    OutBuildings.Add(BuildingActor);
  }
}

void ABuildingManager::DebugSelectBuildingByIndex(int32 Index)
{
  TArray<AActor *> Buildings;
  GetDebugBuildingActors(Buildings);

  if (Buildings.Num() == 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有可测试的楼栋"));
    return;
  }

  AActor *BuildingActor = Buildings[DebugBuildingIndex];
  if (!IsValid(BuildingActor))
  {
    return;
  }

  UE_LOG(
      LogTemp,
      Warning,
      TEXT("[BuildingDebug] Select Building Index=%d Name=%s Code=%s"),
      DebugBuildingIndex,
      *BuildingActor->GetName(),
      *GetBuildingComponent(BuildingActor)->GetBuildingId());

  // 统一走 Manager 入口
  LayeringBuilding(BuildingActor);
}
void ABuildingManager::DebugNextBuilding()
{
  TArray<AActor *> Buildings;
  GetDebugBuildingActors(Buildings);

  if (Buildings.Num() == 0)
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 没有可测试的楼栋"));
    return;
  }

  DebugBuildingIndex++;

  if (DebugBuildingIndex >= Buildings.Num())
  {
    DebugBuildingIndex = 0;
  }

  DebugSelectBuildingByIndex(DebugBuildingIndex);
}

void ABuildingManager::DebugLayeringCurrentBuilding()
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  // 如果当前没有楼栋，就默认拆 DebugBuildingIndex 对应楼栋
  if (!IsValid(BuildingActor))
  {
    DebugSelectBuildingByIndex(DebugBuildingIndex);
    return;
  }

  LayeringBuilding(BuildingActor);
}

void ABuildingManager::DebugBackCurrentBuilding()
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  if (!IsValid(BuildingActor))
  {
    RestoreAllBuildingsVisibility();
    SceneScope.Reset();
    return;
  }

  BackBuildingToNormal(BuildingActor);
}
void ABuildingManager::DebugToggleCurrentFloor(int32 Floor)
{
  AActor *BuildingActor = SceneScope.NowBuilding.Get();

  // 当前没有楼栋时，先进入默认楼栋
  if (!IsValid(BuildingActor))
  {
    DebugSelectBuildingByIndex(DebugBuildingIndex);
    BuildingActor = SceneScope.NowBuilding.Get();
  }

  if (!IsValid(BuildingActor))
  {
    UE_LOG(LogTemp, Warning, TEXT("[BuildingDebug] 当前没有有效楼栋，无法切换楼层"));
    return;
  }

  UE_LOG(
      LogTemp,
      Warning,
      TEXT("[BuildingDebug] Toggle Floor=%d Building=%s"),
      Floor,
      *BuildingActor->GetName());

  // 统一走 Manager 入口
  ToggleBuildingFloor(BuildingActor, Floor);
}

void ABuildingManager::ClearSceneScopeFloor()
{
  // 取消当前所有空间选择
  if (CampusComponent)
  {
    CampusComponent->ClearActiveSelection();
  }
  else if (UWorld *World = GetWorld())
  {
    if (USpaceSubsystem *SpaceSubsystem =
            World->GetSubsystem<USpaceSubsystem>())
    {
      SpaceSubsystem->ClearSelection();
    }
  }

  SceneScope.NowFloor = nullptr;
  SceneScope.NowFloorId.Reset();
  SceneScope.NowFloorNode = nullptr;
  SceneScope.NowFloorIndex =
      BUILDING_INVALID_FLOOR_INDEX;
}

void ABuildingManager::ResumeBuildingTrace()
{
  bBuildingTraceRunning = true;
}

void ABuildingManager::PauseBuildingTrace()
{
  if (!bBuildingTraceRunning)
  {
    return;
  }

  bBuildingTraceRunning = false;

  /*
   * 主动发送EndHover。
   *
   * BuildingBox收到后会立即隐藏，
   * 不再依赖NotifyActorEndCursorOver。
   */
  ClearBuildingTraceHover();
}

void ABuildingManager::SetAllBuildingStructureVisibility(
    bool bVisible)
{
  for (
      const TPair<
          TObjectPtr<AActor>,
          TObjectPtr<UBuildingSpaceComponent>> &Pair : BuildingMap)
  {
    UBuildingSpaceComponent *Component =
        Pair.Value.Get();

    if (!IsValid(Component))
    {
      continue;
    }

    Component->SetBuildingStructureVisibility(bVisible);
  }
}

void ABuildingManager::ClearBuildingTraceHover()
{
  AActor *OldHitActor = CurrentBuildingTraceActor.Get();

  CurrentBuildingTraceActor.Reset();

  if (IsValid(OldHitActor))
  {
    OnBuildingTraceEndHover.Broadcast(OldHitActor);
  }
}

#include "PassageSubsystem.h"

#include "PassageLaneActor.h"

#include "Components/PassageMovementComponent.h"
#include "Components/PersonPassageComponent.h"
#include "Components/CarPassageComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Templates/UnrealTemplate.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPassageSystem, Log, All);

// 从上下文对象解析 World 并返回其上的通行子系统单例。
UPassageSubsystem *UPassageSubsystem::GetPassageSubsystem(const UObject *WorldContextObject)
{
  if (!GEngine || !WorldContextObject)
  {
    return nullptr;
  }

  UWorld *World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);

  return World
             ? World->GetSubsystem<UPassageSubsystem>()
             : nullptr;
}

// 仅在游戏/PIE/预览世界启用，编辑器等世界不创建本子系统。
bool UPassageSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
  return WorldType == EWorldType::Game ||
         WorldType == EWorldType::PIE ||
         WorldType == EWorldType::GamePreview;
}

// 注册通道：校验配置并建立 Channels/DeviceToChannel 索引。
bool UPassageSubsystem::RegisterChannel(APassageLaneActor *Channel)
{
  if (!IsInGameThread() ||
      bShuttingDown ||
      !IsValid(Channel) ||
      Channel->IsActorBeingDestroyed() ||
      Channel->GetWorld() != GetWorld())
  {
    return false;
  }

  // 同一个 Lane 重复注册不重复插入。
  for (const auto &Pair : Channels)
  {
    if (Pair.Value.Channel.Get() == Channel)
    {
      return Pair.Key == Channel->ChannelId &&
             Pair.Value.PoolSource.Get() == Channel->GetPoolSource() &&
             Pair.Value.SubjectType ==
                 Channel->SubjectType;
    }
  }

  FString Error;

  if (!Channel->ValidateConfiguration(Error))
  {
    UE_LOG(
        LogPassageSystem,
        Error,
        TEXT("Cannot register lane '%s': %s"),
        *GetNameSafe(Channel),
        *Error);

    return false;
  }

  const FName ChannelId = Channel->ChannelId;
  const TWeakObjectPtr<UClass> PoolSource(Channel->GetPoolSource());

  if (Channels.Contains(ChannelId))
  {
    UE_LOG(
        LogPassageSystem,
        Error,
        TEXT("Duplicate ChannelId: %s"),
        *ChannelId.ToString());

    return false;
  }

  // 检查设备编码冲突。
  for (const FString &Code : Channel->DeviceCodes)
  {
    const FString Clean = Code.TrimStartAndEnd();

    if (DeviceToChannel.Contains(Clean))
    {
      UE_LOG(
          LogPassageSystem,
          Error,
          TEXT("DeviceCode already registered: %s"),
          *Clean);

      return false;
    }
  }

  FPassageActorPool *Pool = ActorPools.Find(PoolSource);
  if (Pool && Pool->SubjectType != Channel->SubjectType)
  {
    UE_LOG(LogPassageSystem, Error, TEXT("A passage pool source cannot mix person and car lanes."));
    return false;
  }
  const bool bNewPool = !Pool;
  if (!Pool)
  {
    FPassageActorPool NewPool;
    NewPool.ActorClass = Channel->ResolvePassageClass();
    NewPool.SubjectType = Channel->SubjectType;
    Pool = &ActorPools.Add(PoolSource, MoveTemp(NewPool));
  }
  ++Pool->RegisteredChannels;

  FPassageChannelRuntime Runtime;
  Runtime.Channel = Channel;
  Runtime.PoolSource = PoolSource;
  Runtime.SubjectType = Channel->SubjectType;

  Channels.Add(ChannelId, MoveTemp(Runtime));

  for (const FString &Code : Channel->DeviceCodes)
  {
    DeviceToChannel.Add(
        Code.TrimStartAndEnd(),
        ChannelId);
  }

  if (bNewPool)
  {
    // Lane 的 BeginPlay 期间世界可能还未完成启动，下一帧再预热实例。
    GetWorld()->GetTimerManager().SetTimerForNextTick(
        FTimerDelegate::CreateWeakLambda(this, [this, PoolSource]() { PrewarmPool(PoolSource); }));
  }

  UE_LOG(
      LogPassageSystem,
      Log,
      TEXT("Registered lane: %s"),
      *ChannelId.ToString());

  return true;
}

void UPassageSubsystem::PrewarmPool(TWeakObjectPtr<UClass> PoolSource)
{
  if (bShuttingDown || !GetWorld() || !GetWorld()->HasBegunPlay()) return;
  const int32 TargetSize = FMath::Clamp(InitialPoolSize, 0, FMath::Max(1, MaxPoolSize));
  while (const FPassageActorPool *Pool = ActorPools.Find(PoolSource))
  {
    if (Pool->RegisteredChannels <= 0 || Pool->Actors.Num() >= TargetSize) break;
    AActor *Actor = CreatePoolActor(PoolSource);
    if (!Actor) break;
    if (FPassageActorPool *Current = ActorPools.Find(PoolSource))
      Current->IdleActors.Add(Actor);
  }
}

AActor *UPassageSubsystem::CreatePoolActor(TWeakObjectPtr<UClass> PoolSource)
{
  FPassageActorPool *Pool = ActorPools.Find(PoolSource);
  UWorld *World = GetWorld();
  if (!Pool || !World || bShuttingDown || Pool->RegisteredChannels <= 0 ||
      Pool->Actors.Num() >= FMath::Max(1, MaxPoolSize) || !Pool->ActorClass.IsValid()) return nullptr;

  UClass *ActorClass = Pool->ActorClass.Get();
  const EPassageSubjectType SubjectType = Pool->SubjectType;
  if (!PoolSource.IsValid()) return nullptr;

  // Spawn/BeginPlay 可能回调业务；初始化期间不允许重入租用。
  TGuardValue<bool> Guard(bCleaningUp, true);
  FActorSpawnParameters Params;
  Params.OverrideLevel = World->PersistentLevel;
  Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  Params.ObjectFlags |= RF_Transient;
  AActor *Actor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, Params);
  if (!IsValid(Actor)) return nullptr;

  UPassageMovementComponent *Movement = SubjectType == EPassageSubjectType::Person
      ? static_cast<UPassageMovementComponent *>(Actor->FindComponentByClass<UPersonPassageComponent>())
      : static_cast<UPassageMovementComponent *>(Actor->FindComponentByClass<UCarPassageComponent>());
  // Spawn 回调可能改变注册表，不能继续使用原来的 TMap 指针。
  Pool = ActorPools.Find(PoolSource);
  if (!Pool || Pool->RegisteredChannels <= 0 || !IsValid(Movement) ||
      !Actor->GetRootComponent() || Actor->GetRootComponent()->Mobility != EComponentMobility::Movable)
  {
    UE_LOG(LogPassageSystem, Error,
        TEXT("Cannot create passage pool instance from '%s': require a Movable root and matching Person/CarPassageComponent."),
        *GetNameSafe(ActorClass));
    Actor->Destroy();
    return nullptr;
  }

  FPassagePoolActorState State;
  State.PoolSource = PoolSource;
  State.Movement = Movement;
  TArray<USkeletalMeshComponent *> Meshes;
  Actor->GetComponents<USkeletalMeshComponent>(Meshes);
  for (USkeletalMeshComponent *Mesh : Meshes)
  {
    State.Meshes.Add(Mesh);
    Mesh->Stop();
    Mesh->bPauseAnims = true;
    Mesh->SetComponentTickEnabled(false);
  }
  Actor->SetActorHiddenInGame(true);
  Actor->SetActorEnableCollision(false);
  Actor->SetActorTickEnabled(false);
  Movement->SetComponentTickEnabled(false);
  Pool->Actors.Add(Actor);
  PoolActorStates.Add(Actor, MoveTemp(State));
  PooledActors.Add(Actor);
  Actor->OnDestroyed.AddUniqueDynamic(this, &UPassageSubsystem::HandlePoolActorDestroyed);
  return Actor;
}

AActor *UPassageSubsystem::AcquirePoolActor(TWeakObjectPtr<UClass> PoolSource)
{
  while (FPassageActorPool *Pool = ActorPools.Find(PoolSource))
  {
    if (Pool->IdleActors.IsEmpty()) break;
    const TWeakObjectPtr<AActor> Actor = Pool->IdleActors.Pop(EAllowShrinking::No);
    const FPassagePoolActorState *State = PoolActorStates.Find(Actor);
    if (Actor.IsValid() && State && State->Movement.IsValid()) return Actor.Get();
    // 组件被业务销毁时剔除坏实例，不能永久占用池容量。
    Pool->Actors.Remove(Actor);
    PoolActorStates.Remove(Actor);
    if (AActor *InvalidActor = Actor.Get())
    {
      InvalidActor->OnDestroyed.RemoveDynamic(this, &UPassageSubsystem::HandlePoolActorDestroyed);
      PooledActors.RemoveSwap(InvalidActor, EAllowShrinking::No);
      InvalidActor->Destroy();
    }
  }
  return CreatePoolActor(PoolSource);
}

void UPassageSubsystem::HandlePoolActorDestroyed(AActor *Actor)
{
  const TWeakObjectPtr<AActor> Key(Actor);
  if (const FPassagePoolActorState *State = PoolActorStates.Find(Key))
  {
    if (FPassageActorPool *Pool = ActorPools.Find(State->PoolSource))
    {
      Pool->Actors.Remove(Key);
      Pool->IdleActors.RemoveSwap(Key, EAllowShrinking::No);
    }
  }
  PoolActorStates.Remove(Key);
  PooledActors.RemoveSwap(Actor, EAllowShrinking::No);
  if (const FName *ChannelId = ActorToChannel.Find(Key))
  {
    const FName Id = *ChannelId;
    FinishPassage(Id, false, TEXT("Pooled passage actor was destroyed."));
  }
}

void UPassageSubsystem::DestroyPool(TWeakObjectPtr<UClass> PoolSource)
{
  FPassageActorPool Pool;
  if (!ActorPools.RemoveAndCopyValue(PoolSource, Pool)) return;
  TGuardValue<bool> Guard(bCleaningUp, true);
  for (const TWeakObjectPtr<AActor> &Key : Pool.Actors)
  {
    PoolActorStates.Remove(Key);
    if (AActor *Actor = Key.Get())
    {
      Actor->OnDestroyed.RemoveDynamic(this, &UPassageSubsystem::HandlePoolActorDestroyed);
      PooledActors.RemoveSwap(Actor, EAllowShrinking::No);
      Actor->Destroy();
    }
  }
}

// 注销通道：按对象查找其注册编号并移除。
void UPassageSubsystem::UnregisterChannel(APassageLaneActor *Channel)
{
  if (!IsInGameThread() || !Channel)
  {
    return;
  }

  FName RegisteredId = NAME_None;

  for (const auto &Pair : Channels)
  {
    if (Pair.Value.Channel.Get(true) == Channel)
    {
      RegisteredId = Pair.Key;
      break;
    }
  }

  if (!RegisteredId.IsNone())
  {
    RemoveChannel(
        RegisteredId,
        TEXT("Lane was unregistered."));
  }
}

APassageLaneActor *UPassageSubsystem::FindChannelByDevice(const FString &DeviceCode) const
{
  const FString CleanCode = DeviceCode.TrimStartAndEnd();

  const FName *ChannelId = DeviceToChannel.Find(CleanCode);

  return ChannelId ? FindChannel(*ChannelId) : nullptr;
}
// 按通道编号查找 Lane Actor。
APassageLaneActor *UPassageSubsystem::FindChannel(FName ChannelId) const
{
  const FPassageChannelRuntime *Runtime = Channels.Find(ChannelId);

  return Runtime ? Runtime->Channel.Get() : nullptr;
}

// 人物通行入口：委托给 TryPlay。
bool UPassageSubsystem::PlayPersonPassage(const FString &DeviceCode, bool bEnter)
{
  return TryPlay(DeviceCode, EPassageSubjectType::Person, bEnter);
}

// 车辆通行入口：委托给 TryPlay。
bool UPassageSubsystem::PlayCarPassage(const FString &DeviceCode, bool bEnter)
{
  return TryPlay(DeviceCode, EPassageSubjectType::Car, bEnter);
}

// 记录拒绝原因日志并统一返回 false。
bool UPassageSubsystem::Reject(const FString &DeviceCode, const FString &Reason) const
{
  UE_LOG(
      LogPassageSystem,
      Warning,
      TEXT("Cannot play passage. Device=%s Reason=%s"),
      *DeviceCode,
      *Reason);

  return false;
}

// 判断该执行序号是否仍是目标通道的当前执行。
bool UPassageSubsystem::IsCurrentRun(FName ChannelId, uint64 RunSerial) const
{
  const FPassageChannelRuntime *Runtime = Channels.Find(ChannelId);

  return Runtime &&
         RunSerial != 0 &&
         Runtime->RunSerial == RunSerial;
}

bool UPassageSubsystem::ReserveChannel(FName ChannelId)
{
  FString Error;
  if (TryReserve(ChannelId, Error)) return true;
  return Reject(ChannelId.ToString(), Error);
}

bool UPassageSubsystem::TryReserve(FName ChannelId, FString &OutError)
{
  OutError.Reset();
  if (!IsInGameThread() || bShuttingDown || bCleaningUp ||
      !GetWorld() || !GetWorld()->HasBegunPlay())
  {
    OutError = TEXT("Passage world is not ready.");
    return false;
  }
  FPassageChannelRuntime *Runtime = Channels.Find(ChannelId);
  APassageLaneActor *Channel = Runtime ? Runtime->Channel.Get() : nullptr;
  if (!IsValid(Channel) || Channel->IsActorBeingDestroyed())
  {
    OutError = TEXT("Registered lane is invalid.");
    return false;
  }
  if (Runtime->RunSerial != 0)
  {
    OutError = TEXT("Lane already has a reserved or moving instance.");
    return false;
  }
  if (Channel->ChannelId != ChannelId || Channel->GetPoolSource() != Runtime->PoolSource.Get() ||
      Channel->SubjectType != Runtime->SubjectType)
  {
    OutError = TEXT("Lane binding changed; unregister and register again.");
    return false;
  }
  if (!Channel->ValidateConfiguration(OutError)) return false;

  const TWeakObjectPtr<UClass> PoolSource = Runtime->PoolSource;
  AActor *Actor = AcquirePoolActor(PoolSource);
  if (!Actor)
  {
    OutError = TEXT("Passage pool is full or could not spawn an instance.");
    return false;
  }
  // Spawn/BeginPlay 可触发注销，重新取得 Runtime，避免 TMap 指针失效。
  Runtime = Channels.Find(ChannelId);
  const FPassagePoolActorState *State = PoolActorStates.Find(Actor);
  if (!Runtime || Runtime->Channel.Get() != Channel || Runtime->PoolSource != PoolSource ||
      Runtime->RunSerial != 0 || !State || !State->Movement.IsValid() ||
      !Actor->HasActorBegunPlay())
  {
    ReleaseActor(Actor, State ? State->Movement.Get() : nullptr);
    OutError = TEXT("Lane or pooled actor changed during reservation.");
    return false;
  }

  if (++NextRunSerial == 0) ++NextRunSerial;
  Runtime->RunSerial = NextRunSerial;
  Runtime->Actor = Actor;
  Runtime->Movement = State->Movement;
  Runtime->bStarted = false;
  ActorToChannel.Add(Actor, ChannelId);
  return true;
}

// 通行启动核心：校验设备编码与通道绑定，启动组件并维护索引。
bool UPassageSubsystem::TryPlay(const FString &DeviceCode, EPassageSubjectType SubjectType, bool bEnter)
{
  // UObject 和场景操作必须在游戏线程执行。
  if (!IsInGameThread() || bShuttingDown || bCleaningUp)
  {
    return false;
  }

  const FString CleanCode = DeviceCode.TrimStartAndEnd();

  if (CleanCode.IsEmpty())
  {
    return Reject(CleanCode, TEXT("DeviceCode is empty."));
  }

  UWorld *World = GetWorld();

  if (!World || !World->HasBegunPlay())
  {
    return Reject(CleanCode, TEXT("World has not begun play."));
  }

  const FName *FoundId = DeviceToChannel.Find(CleanCode);

  if (!FoundId)
  {
    return Reject(
        CleanCode,
        TEXT("No registered lane for this device."));
  }

  const FName ChannelId = *FoundId;

  FPassageChannelRuntime *Runtime = Channels.Find(ChannelId);

  if (!Runtime)
  {
    return Reject(
        CleanCode,
        TEXT("Lane runtime is missing."));
  }

  APassageLaneActor *Channel = Runtime->Channel.Get();

  if (!IsValid(Channel) ||
      Channel->IsActorBeingDestroyed())
  {
    return Reject(
        CleanCode,
        TEXT("Lane is invalid."));
  }

  // 运行中更换绑定，需要先注销再重新注册。
  if (Channel->ChannelId != ChannelId ||
      Channel->GetPoolSource() != Runtime->PoolSource.Get() ||
      Channel->SubjectType != Runtime->SubjectType)
  {
    return Reject(
        CleanCode,
        TEXT("Lane binding changed; unregister and register again."));
  }

  if (Runtime->SubjectType != SubjectType)
  {
    return Reject(
        CleanCode,
        TEXT("Person/car type mismatch."));
  }

  if (Runtime->bStarted)
  {
    return Reject(
        CleanCode,
        TEXT("Lane is busy."));
  }

  FString Error;

  if (!Channel->ValidateConfiguration(Error))
  {
    return Reject(CleanCode, Error);
  }

  // TS 已在开闸前预留；直接从蓝图/C++ 播放也兼容自动租用。
  if (Runtime->RunSerial == 0)
  {
    if (!TryReserve(ChannelId, Error)) return Reject(CleanCode, Error);
  }
  Runtime = Channels.Find(ChannelId);
  if (!Runtime) return false;
  AActor *Actor = Runtime->Actor.Get();
  UPassageMovementComponent *Movement = Runtime->Movement.Get();
  if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || !IsValid(Movement))
  {
    FinishPassage(ChannelId, false, TEXT("Reserved passage instance is invalid."), false);
    return Reject(
        CleanCode,
        TEXT("Reserved passage instance is invalid."));
  }

  if (Movement->IsPassing() ||
      MovementToChannel.Contains(
          TWeakObjectPtr<UPassageMovementComponent>(Movement)))
  {
    FinishPassage(ChannelId, false, TEXT("Pooled instance is already moving."), false);
    return Reject(
        CleanCode,
        TEXT("Assigned actor is already moving."));
  }

  FVector Start;
  FVector End;

  Channel->GetRoute(
      bEnter
          ? EPassageDirection::Enter
          : EPassageDirection::Exit,
      Start,
      End);

  const uint64 RunSerial = Runtime->RunSerial;
  Runtime->bStarted = true;

  MovementToChannel.Add(
      TWeakObjectPtr<UPassageMovementComponent>(Movement),
      ChannelId);

  Movement->OnFinished.AddUniqueDynamic(
      this,
      &UPassageSubsystem::HandleMovementFinished);

  if (const FPassagePoolActorState *State = PoolActorStates.Find(Actor))
  {
    for (const TWeakObjectPtr<USkeletalMeshComponent> &Mesh : State->Meshes)
    {
      if (Mesh.IsValid())
      {
        Mesh->bPauseAnims = false;
        Mesh->SetComponentTickEnabled(true);
      }
    }
  }

  // StartPassage 自己负责：
  // 设置起点、朝向、显示对象、播放动画、开启移动 Tick。
  const bool bStarted = Movement->StartPassage(
      Start,
      End,
      Channel->SpeedOverride,
      Error);

  // 初始化期间对象可能被取消或注销。
  if (!IsCurrentRun(ChannelId, RunSerial))
  {
    return false;
  }

  if (!bStarted)
  {
    FinishPassage(
        ChannelId,
        false,
        Error,
        false);

    return Reject(CleanCode, Error);
  }

  OnPassageStarted.Broadcast(
      ChannelId,
      SubjectType,
      bEnter);

  return true;
}

// 查询通道是否正在通行。
bool UPassageSubsystem::IsChannelBusy(
    FName ChannelId) const
{
  const FPassageChannelRuntime *Runtime =
      Channels.Find(ChannelId);
  if (!Runtime) return false;
  if (Runtime->RunSerial != 0 || bCleaningUp) return true;
  const FPassageActorPool *Pool = ActorPools.Find(Runtime->PoolSource);
  return Pool && Pool->IdleActors.IsEmpty() && Pool->Actors.Num() >= FMath::Max(1, MaxPoolSize);
}

// 停止指定通道的通行。
bool UPassageSubsystem::StopPassage(
    FName ChannelId)
{
  if (!IsInGameThread() ||
      bShuttingDown ||
      bCleaningUp)
  {
    return false;
  }

  const FPassageChannelRuntime *Runtime =
      Channels.Find(ChannelId);

  if (!Runtime || Runtime->RunSerial == 0)
  {
    return false;
  }

  FinishPassage(
      ChannelId,
      false,
      TEXT("Passage was stopped."));

  return true;
}

// 归还池实例：取消通行、停止动画、隐藏并禁用所有通行相关 Tick。
void UPassageSubsystem::ReleaseActor(
    AActor *Actor,
    UPassageMovementComponent *Movement)
{
  // 清理期间不接收新的播放请求。
  // 对外的完成通知在清理结束后再发送。
  TGuardValue<bool> CleanupGuard(bCleaningUp, true);

  if (IsValid(Movement))
  {
    Movement->OnFinished.RemoveDynamic(
        this,
        &UPassageSubsystem::HandleMovementFinished);

    if (Movement->IsPassing())
    {
      Movement->CancelPassage(
          TEXT("Passage cleanup."));
    }

    Movement->SetComponentTickEnabled(false);
  }

  if (IsValid(Actor) &&
      !Actor->IsActorBeingDestroyed())
  {
    Actor->SetActorHiddenInGame(true);
    Actor->SetActorEnableCollision(false);
    Actor->SetActorTickEnabled(false);
    if (const FPassagePoolActorState *State = PoolActorStates.Find(Actor))
    {
      for (const TWeakObjectPtr<USkeletalMeshComponent> &Mesh : State->Meshes)
      {
        if (Mesh.IsValid())
        {
          Mesh->Stop();
          Mesh->bPauseAnims = true;
          Mesh->SetComponentTickEnabled(false);
        }
      }
      if (FPassageActorPool *Pool = ActorPools.Find(State->PoolSource))
      {
        Pool->IdleActors.AddUnique(Actor);
      }
    }
  }

  // 不调用 Destroy，也不把对象移回起点。
  // 下次开始时再重设位置。
}

// 结束一次通行：清状态、释放对象并广播完成事件。
void UPassageSubsystem::FinishPassage(
    FName ChannelId,
    bool bSuccess,
    const FString &Reason,
    bool bNotify)
{
  FPassageChannelRuntime *Runtime =
      Channels.Find(ChannelId);

  if (!Runtime || Runtime->RunSerial == 0)
  {
    return;
  }

  const TWeakObjectPtr<AActor> Actor =
      Runtime->Actor;

  const TWeakObjectPtr<UPassageMovementComponent> Movement =
      Runtime->Movement;

  // 清理前先解除本次执行状态。
  Runtime->RunSerial = 0;
  Runtime->Movement.Reset();
  Runtime->Actor.Reset();
  Runtime->bStarted = false;

  MovementToChannel.Remove(Movement);
  ActorToChannel.Remove(Actor);

  AActor *Instance = Actor.Get();
  if (!IsValid(Instance) || Instance->IsActorBeingDestroyed())
  {
    // 组件 EndPlay 先于 Actor.OnDestroyed；必须先释放容量再通知等待中的通道。
    if (const FPassagePoolActorState *State = PoolActorStates.Find(Actor))
    {
      if (FPassageActorPool *Pool = ActorPools.Find(State->PoolSource))
      {
        Pool->Actors.Remove(Actor);
        Pool->IdleActors.RemoveSwap(Actor, EAllowShrinking::No);
      }
    }
    PoolActorStates.Remove(Actor);
    PooledActors.RemoveAllSwap([Actor](const TObjectPtr<AActor> &Entry) {
      return TWeakObjectPtr<AActor>(Entry.Get()) == Actor;
    }, EAllowShrinking::No);
  }
  else
  {
    ReleaseActor(Instance, Movement.Get());
  }

  if (bNotify && !bShuttingDown)
  {
    OnPassageFinished.Broadcast(
        ChannelId,
        bSuccess,
        Reason);
  }
}

// 通行组件结束回调：按移动组件反查通道并结束。
void UPassageSubsystem::HandleMovementFinished(
    UPassageMovementComponent *Movement,
    bool bSuccess,
    const FString &Reason)
{
  if (bShuttingDown || !Movement)
  {
    return;
  }

  const TWeakObjectPtr<UPassageMovementComponent> Key(Movement);

  const FName *FoundId = MovementToChannel.Find(Key);

  if (!FoundId)
  {
    return;
  }

  const FName ChannelId = *FoundId;

  const FPassageChannelRuntime *Runtime = Channels.Find(ChannelId);

  if (!Runtime ||
      Runtime->RunSerial == 0 ||
      Runtime->Movement != Key)
  {
    return;
  }

  FinishPassage(ChannelId, bSuccess, Reason);
}

// 移除通道：归还本通道实例，最后一个使用者注销时销毁整池。
void UPassageSubsystem::RemoveChannel(FName ChannelId, const FString &Reason)
{
  const FPassageChannelRuntime *Found =
      Channels.Find(ChannelId);

  if (!Found)
  {
    return;
  }

  // 使用副本，避免取消回调影响 TMap 中的指针。
  const FPassageChannelRuntime Runtime = *Found;

  Channels.Remove(ChannelId);
  MovementToChannel.Remove(Runtime.Movement);

  for (auto It = DeviceToChannel.CreateIterator();
       It;
       ++It)
  {
    if (It.Value() == ChannelId)
    {
      It.RemoveCurrent();
    }
  }

  if (Runtime.RunSerial != 0)
  {
    ActorToChannel.Remove(Runtime.Actor);
    ReleaseActor(
        Runtime.Actor.Get(),
        Runtime.Movement.Get());
  }

  if (FPassageActorPool *Pool = ActorPools.Find(Runtime.PoolSource))
  {
    if (--Pool->RegisteredChannels == 0) DestroyPool(Runtime.PoolSource);
  }

  if (Runtime.RunSerial != 0 && !bShuttingDown)
  {
    OnPassageFinished.Broadcast(
        ChannelId,
        false,
        Reason);
  }
}

// 子系统反初始化：停止并移除所有通道后清空索引。
void UPassageSubsystem::Deinitialize()
{
  bShuttingDown = true;

  TArray<FName> ChannelIds;
  Channels.GenerateKeyArray(ChannelIds);

  for (const FName ChannelId : ChannelIds)
  {
    RemoveChannel(
        ChannelId,
        TEXT("Passage subsystem is shutting down."));
  }

  Channels.Empty();
  DeviceToChannel.Empty();
  MovementToChannel.Empty();
  ActorToChannel.Empty();
  TArray<TWeakObjectPtr<UClass>> PoolSources;
  ActorPools.GenerateKeyArray(PoolSources);
  for (const TWeakObjectPtr<UClass> &Source : PoolSources) DestroyPool(Source);
  PoolActorStates.Empty();
  PooledActors.Empty();

  Super::Deinitialize();
}

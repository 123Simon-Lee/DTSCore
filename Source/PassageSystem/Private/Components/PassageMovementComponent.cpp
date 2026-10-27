#include "Components/PassageMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"

#include "Components/PrimitiveComponent.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"

UPassageMovementComponent::UPassageMovementComponent()
{
  // 默认不主动 Tick，开始通行时才开启。
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UPassageMovementComponent::BeginPlay()
{
  Super::BeginPlay();
  AActor *Owner = GetOwner();
  if (!IsValid(Owner) || !Owner->GetRootComponent()) return;
  PassageTrigger = NewObject<UBoxComponent>(Owner, TEXT("PassageTrigger"), RF_Transient);
  Owner->AddInstanceComponent(PassageTrigger);
  PassageTrigger->SetupAttachment(Owner->GetRootComponent());
  PassageTrigger->SetMobility(EComponentMobility::Movable);
  PassageTrigger->PrimaryComponentTick.bCanEverTick = false;
  PassageTrigger->SetBoxExtent(TriggerBoxExtent.GetAbs(), false);
  PassageTrigger->SetRelativeLocation(TriggerBoxOffset);
  PassageTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  PassageTrigger->SetCollisionObjectType(ECC_Pawn);
  PassageTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
  PassageTrigger->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
  PassageTrigger->SetGenerateOverlapEvents(true);
  PassageTrigger->SetCanEverAffectNavigation(false);
  PassageTrigger->RegisterComponent();
}

// 查找 Owner 的骨骼网格组件：Character 优先取 Mesh，否则按类型查找。
USkeletalMeshComponent *UPassageMovementComponent::FindOwnerMesh() const
{
  AActor *Owner = GetOwner();
  if (!IsValid(Owner))
  {
    return nullptr;
  }

  if (ACharacter *Character = Cast<ACharacter>(Owner))
  {
    return Character->GetMesh();
  }

  return Owner->FindComponentByClass<USkeletalMeshComponent>();
}

// 开始通行：计算起终点/方向/距离并启动组件Tick。
bool UPassageMovementComponent::StartPassage(FVector GroundStart, FVector GroundEnd, float SpeedOverride, FString &OutError)
{
  OutError.Reset();

  AActor *Owner = GetOwner();

  // 已在通行或 Owner 无效时不允许重复启动。
  if (bRunning || !IsValid(Owner))
  {
    OutError = TEXT("Movement is busy or owner is invalid.");
    return false;
  }

  // 玩家控制的 Pawn 不能作为通行对象。
  if (const APawn *Pawn = Cast<APawn>(Owner))
  {
    if (Pawn->IsPlayerControlled())
    {
      OutError = TEXT("A player-controlled pawn cannot be a passage actor.");
      return false;
    }
  }

  // 根组件必须可移动，否则无法被 Teleport 定位。
  if (!Owner->GetRootComponent() || Owner->GetRootComponent()->Mobility != EComponentMobility::Movable)
  {
    OutError = TEXT("Passage actor root must be Movable.");
    return false;
  }

  // 校验坐标与数值参数的合法性。
  if (GroundStart.ContainsNaN() || GroundEnd.ContainsNaN() || !FMath::IsFinite(HeightOffset) || !FMath::IsFinite(SpeedOverride) || SpeedOverride < 0.0f || !FMath::IsFinite(Acceleration) || Acceleration < 0.0f)
  {
    OutError = TEXT("Invalid position or movement settings.");
    return false;
  }

  // SpeedOverride 无效时回退到默认速度。
  TargetSpeed = SpeedOverride > 0.0f
                    ? SpeedOverride
                    : DefaultSpeed;

  if (!FMath::IsFinite(TargetSpeed) || TargetSpeed <= 0.0f)
  {
    OutError = TEXT("Movement speed must be positive.");
    return false;
  }

  float ZOffset = HeightOffset;

  // 需要时把通行高度抬到胶囊体底以上。
  if (bUseCharacterCapsuleHeight)
  {
    if (const ACharacter *Character = Cast<ACharacter>(Owner))
    {
      if (const UCapsuleComponent *Capsule = Character->GetCapsuleComponent())
      {
        ZOffset += Capsule->GetScaledCapsuleHalfHeight();
      }
    }
  }
  // 计算通行起点和终点
  StartLocation = GroundStart + FVector(0.0, 0.0, ZOffset);
  EndLocation = GroundEnd + FVector(0.0, 0.0, ZOffset);
  // 计算通行距离
  TotalDistance = static_cast<float>(FVector::Distance(StartLocation, EndLocation));

  if (!FMath::IsFinite(TotalDistance) || TotalDistance <= 1.0f)
  {
    OutError = TEXT("Passage endpoints are too close.");
    return false;
  }
  // 计算通行方向
  MoveDirection = (EndLocation - StartLocation).GetSafeNormal();

  // 子类表现准备失败则终止启动。
  if (!PreparePresentation(OutError))
  {
    return false;
  }

  MovedDistance = 0.0f;

  // 加速模式下从 0 起步，否则直接使用目标速度。
  CurrentSpeed = Acceleration > 0.0f
                     ? 0.0f
                     : TargetSpeed;

  bRunning = true;

  // 暂停 Owner 自身的移动/物理，避免与通行位移冲突。
  SuspendOwnerMovement();
  // 先设置位置和旋转
  ApplyPose();

  Owner->SetActorHiddenInGame(false);

  // 通知子类开始表现，并补一帧初始表现。
  BeginPresentation();
  UpdatePresentation(0.0f, 0.0f);

  // 先定位、设置方向和 bRunning，最后才启用查询，避免回收点/旧位置误触发门。
  if (IsValid(PassageTrigger))
  {
    // 必须先开 Actor 总开关。关闭状态下设置 QueryOnly，UE 不会为 Box 创建查询物理状态；
    // 后续只打开 Actor 只会刷新过滤数据，Box 仍可能没有可用于 Overlap 的形状。
    Owner->SetActorEnableCollision(true);
    PassageTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  }

  SetComponentTickEnabled(true);
  return true;
}

// 每帧推进通行进度：更新速度、累计距离、刷新位置朝向并驱动表现。
void UPassageMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (!bRunning || DeltaTime <= 0.0f)
  {
    return;
  }

  // Owner 被销毁则强制失败结束。
  if (!IsValid(GetOwner()) || !GetOwner()->GetRootComponent())
  {
    FinishMovement(false, TEXT("Passage actor was removed."));
    return;
  }

  // 加速模式按固定步长逼近目标速度，否则恒定速度。
  CurrentSpeed = Acceleration > 0.0f
                     ? FMath::FInterpConstantTo(
                           CurrentSpeed,
                           TargetSpeed,
                           DeltaTime,
                           Acceleration)
                     : TargetSpeed;

  const float PreviousDistance = MovedDistance;

  // 前进距离不能超过总距离。
  MovedDistance = FMath::Min(
      MovedDistance + CurrentSpeed * DeltaTime,
      TotalDistance);

  const float DeltaDistance =
      MovedDistance - PreviousDistance;

  // 以实际前进距离反算本帧速度。
  CurrentSpeed = DeltaDistance / DeltaTime;

  ApplyPose();
  UpdatePresentation(DeltaTime, DeltaDistance);

  // 走完则成功结束。
  if (MovedDistance >= TotalDistance)
  {
    FinishMovement(true, FString());
  }
}

// 按已走距离设置 Owner 的位置与朝向（可选是否跟随俯仰角）。
void UPassageMovementComponent::ApplyPose()
{
  AActor *Owner = GetOwner();

  if (!IsValid(Owner))
  {
    return;
  }

  const FVector Location = StartLocation + MoveDirection * MovedDistance;

  FRotator Rotation = MoveDirection.Rotation();

  // 不跟随俯仰时强制水平朝向。
  if (!bFollowPitch)
  {
    Rotation.Pitch = 0.0f;
  }

  Rotation.Roll = 0.0f;

  Owner->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
}

// 暂停 Owner 的移动组件与物理模拟，并记录以便结束后恢复。
void UPassageMovementComponent::SuspendOwnerMovement()
{
  AActor *Owner = GetOwner();

  SuspendedMovement.Reset();
  SuspendedPhysics.Reset();
  SuspendedCollision.Reset();

  bPreviousCollisionEnabled = Owner->GetActorEnableCollision();

  Owner->SetActorEnableCollision(false);

  // 停用所有处于活动状态的移动组件。
  TArray<UMovementComponent *> MovementComponents;
  Owner->GetComponents<UMovementComponent>(MovementComponents);

  for (UMovementComponent *Component : MovementComponents)
  {
    if (IsValid(Component) && Component->IsActive())
    {
      SuspendedMovement.Add(Component);
      Component->Deactivate();
    }
  }

  // 关闭所有正在模拟物理的原语组件。
  TArray<UPrimitiveComponent *> Primitives;
  Owner->GetComponents<UPrimitiveComponent>(Primitives);

  for (UPrimitiveComponent *Component : Primitives)
  {
    if (IsValid(Component) && Component->IsSimulatingPhysics())
    {
      SuspendedPhysics.Add(Component);
      Component->SetSimulatePhysics(false);
    }
    if (IsValid(Component) && Component != PassageTrigger.Get() &&
        Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
    {
      SuspendedCollision.Emplace(Component, Component->GetCollisionEnabled());
      Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
  }
}

// 恢复通行前被暂停的移动组件、物理模拟与碰撞开关。
void UPassageMovementComponent::RestoreOwnerMovement()
{
  AActor *Owner = GetOwner();

  if (IsValid(Owner) && !Owner->IsActorBeingDestroyed())
  {
    Owner->SetActorEnableCollision(bPreviousCollisionEnabled);

    for (const auto& Entry : SuspendedCollision)
      if (Entry.Key.IsValid()) Entry.Key->SetCollisionEnabled(Entry.Value);

    for (const TWeakObjectPtr<UPrimitiveComponent> &Component : SuspendedPhysics)
    {
      if (Component.IsValid())
      {
        Component->SetSimulatePhysics(true);
      }
    }

    for (const TWeakObjectPtr<UMovementComponent> &Component : SuspendedMovement)
    {
      if (Component.IsValid())
      {
        Component->Activate();
      }
    }
  }

  SuspendedMovement.Reset();
  SuspendedPhysics.Reset();
  SuspendedCollision.Reset();
}

// 结束通行：关闭 Tick、结束表现、恢复移动并广播结果。
void UPassageMovementComponent::FinishMovement(bool bSuccess, const FString &Reason)
{
  if (!bRunning)
    return;

  bRunning = false;
  CurrentSpeed = 0.0f;

  // 关闭查询会产生 EndOverlap；门仍能在通行已经结束的情况下完成自动关门。
  if (IsValid(PassageTrigger)) PassageTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  SetComponentTickEnabled(false);

  EndPresentation();
  RestoreOwnerMovement();

  OnFinished.Broadcast(this, bSuccess, Reason);
}

// 取消通行：以失败结束。
void UPassageMovementComponent::CancelPassage(const FString &Reason)
{
  FinishMovement(false, Reason);
}

// 组件 EndPlay 时兜底结束，避免残留运行状态。
void UPassageMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  FinishMovement(false, TEXT("Passage actor ended play."));
  Super::EndPlay(EndPlayReason);
}

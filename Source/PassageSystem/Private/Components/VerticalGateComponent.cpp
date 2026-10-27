#include "Components/VerticalGateComponent.h"

#include "Components/BoxComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

UVerticalGateComponent::UVerticalGateComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;

  SetMobility(EComponentMobility::Movable);
}

void UVerticalGateComponent::BeginPlay()
{
  Super::BeginPlay();

  // 保留配置的 Yaw、Roll，只控制 Pitch。
  FixedRotation = GetRelativeRotation();
  FixedRotation.Pitch = 0.0f;

  // 初始关闭。
  CurrentPitch = 0.0f;
  TargetPitch = 0.0f;

  ApplyPitch();
  SetComponentTickEnabled(false);

  if (!bAutoTrigger)
    return;

  AActor *Owner = GetOwner();
  USceneComponent *Parent = GetAttachParent();

  if (!IsValid(Owner) || !IsValid(Parent))
  {
    UE_LOG(
        LogTemp,
        Error,
        TEXT("VerticalGate requires a fixed parent component."));
    return;
  }

  TriggerBox = NewObject<UBoxComponent>(Owner, NAME_None, RF_Transient);

  Owner->AddInstanceComponent(TriggerBox);

  // 与旋转组件同级，避免盒体随着抬杆旋转。
  TriggerBox->SetupAttachment(Parent);
  TriggerBox->SetMobility(EComponentMobility::Movable);

  const FTransform BoxWorldTransform = FTransform(FRotator::ZeroRotator, TriggerBoxOffset) * GetComponentTransform();

  TriggerBox->SetWorldTransform(BoxWorldTransform);
  TriggerBox->SetBoxExtent(TriggerBoxExtent.GetAbs(), false);

  TriggerBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  TriggerBox->SetCollisionObjectType(ECC_WorldDynamic);

  TriggerBox->SetCollisionResponseToAllChannels(ECR_Ignore);
  TriggerBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
  TriggerBox->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
  TriggerBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);

  TriggerBox->SetGenerateOverlapEvents(true);
  TriggerBox->SetCanEverAffectNavigation(false);

  TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &UVerticalGateComponent::HandleTriggerBegin);

  TriggerBox->OnComponentEndOverlap.AddDynamic(this, &UVerticalGateComponent::HandleTriggerEnd);

  TriggerBox->RegisterComponent();
  TriggerBox->UpdateOverlaps();

  // 处理开始运行时已经位于盒内的车辆。
  RefreshAutoState();
}

void UVerticalGateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  SetComponentTickEnabled(false);

  if (IsValid(TriggerBox))
  {
    TriggerBox->OnComponentBeginOverlap.RemoveDynamic(this, &UVerticalGateComponent::HandleTriggerBegin);

    TriggerBox->OnComponentEndOverlap.RemoveDynamic(this, &UVerticalGateComponent::HandleTriggerEnd);

    if (AActor *Owner = GetOwner())
    {
      Owner->RemoveInstanceComponent(TriggerBox);
    }

    TriggerBox->DestroyComponent();
    TriggerBox = nullptr;
  }

  Super::EndPlay(EndPlayReason);
}

void UVerticalGateComponent::Open()
{
  SetTargetPitch(90.0f);
}

void UVerticalGateComponent::Close()
{
  if (bAutoTrigger && HasVehicleInside())
  {
    return;
  }

  SetTargetPitch(0.0f);
}

bool UVerticalGateComponent::IsOpen() const
{
  return FMath::IsNearlyEqual(CurrentPitch, 90.0f);
}

void UVerticalGateComponent::SetTargetPitch(float NewTargetPitch)
{
  TargetPitch = NewTargetPitch;

  // 已在目标位置或时长为零，也通知完成，避免外部一直等待。
  if (Duration <= KINDA_SMALL_NUMBER ||
      FMath::IsNearlyEqual(CurrentPitch, TargetPitch))
  {
    FinishMovement();
    return;
  }

  // 中途反向调用时，从当前位置继续运动。
  SetComponentTickEnabled(true);
}

void UVerticalGateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (DeltaTime <= 0.0f)
  {
    return;
  }

  const float Speed =
      90.0f / FMath::Max(Duration, KINDA_SMALL_NUMBER);

  CurrentPitch = FMath::FInterpConstantTo(
      CurrentPitch,
      TargetPitch,
      DeltaTime,
      Speed);

  if (FMath::IsNearlyEqual(CurrentPitch, TargetPitch))
  {
    FinishMovement();
  }
  else
  {
    ApplyPitch();
  }
}

void UVerticalGateComponent::ApplyPitch()
{
  FRotator Rotation = FixedRotation;
  Rotation.Pitch = CurrentPitch;

  SetRelativeRotation(Rotation);
}

void UVerticalGateComponent::FinishMovement()
{
  CurrentPitch = TargetPitch;
  ApplyPitch();
  SetComponentTickEnabled(false);

  // UE_LOG(
  //     LogTemp,
  //     Log,
  //     TEXT("[VerticalGate] Finished Gate=%s Pitch=%.1f"),
  //     *GetNameSafe(GetOwner()),
  //     GetRelativeRotation().Pitch);

  OnMotionFinished.Broadcast();
}

bool UVerticalGateComponent::IsAllowedActor(AActor *Actor) const
{
  const APawn *Pawn = Cast<APawn>(Actor);

  if (!IsValid(Pawn) ||
      Pawn == GetOwner() ||
      Pawn->IsActorBeingDestroyed())
  {
    return false;
  }

  for (const TSubclassOf<APawn> &AllowedClass : AllowList)
  {
    // IsA 支持子类，不能直接比较 GetClass() 是否相等。
    if (AllowedClass && Pawn->IsA(AllowedClass.Get()))
    {
      return true;
    }
  }

  return false;
}

bool UVerticalGateComponent::HasVehicleInside() const
{
  if (!IsValid(TriggerBox))
  {
    return false;
  }

  TArray<AActor *> Actors;
  TriggerBox->GetOverlappingActors(Actors);

  for (AActor *Actor : Actors)
  {
    if (IsAllowedActor(Actor))
    {
      return true;
    }
  }

  return false;
}

void UVerticalGateComponent::RefreshAutoState()
{
  if (!bAutoTrigger || !IsValid(TriggerBox))
  {
    return;
  }

  const float NewTarget = HasVehicleInside() ? 90.0f : 0.0f;

  // 多辆车或多个碰撞组件重叠时，不重复启动相同动作。
  if (!FMath::IsNearlyEqual(TargetPitch, NewTarget))
  {
    SetTargetPitch(NewTarget);
  }
}

void UVerticalGateComponent::HandleTriggerBegin(
    UPrimitiveComponent *OverlappedComponent,
    AActor *OtherActor,
    UPrimitiveComponent *OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult &SweepResult)
{
  UE_LOG(
      LogTemp,
      Log,
      TEXT("[VerticalGate] BeginOverlap Gate=%s Actor=%s Allowed=%d"),
      *GetNameSafe(GetOwner()),
      *GetNameSafe(OtherActor),
      IsAllowedActor(OtherActor) ? 1 : 0);
  RefreshAutoState();
}

void UVerticalGateComponent::HandleTriggerEnd(
    UPrimitiveComponent *OverlappedComponent,
    AActor *OtherActor,
    UPrimitiveComponent *OtherComp,
    int32 OtherBodyIndex)
{
  // 重新检查全部重叠对象，不能某一个组件离开就直接关门。
  RefreshAutoState();
}
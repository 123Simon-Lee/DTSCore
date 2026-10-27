#include "PassageLaneActor.h"

#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

#include "Components/PersonPassageComponent.h"
#include "Components/CarPassageComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "PassageSubsystem.h"
// 自定义log日志输出类型
DEFINE_LOG_CATEGORY_STATIC(LogPassageLane, Log, All);

// 构造：创建场景根与内外侧路径点箭头组件。
APassageLaneActor::APassageLaneActor()
{
  PrimaryActorTick.bCanEverTick = false;

  // 根节点。
  USceneComponent *SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

  SetRootComponent(SceneRoot);
  // 设置可移动的
  SceneRoot->SetMobility(EComponentMobility::Movable);

  // 外侧路径点。
  OutsidePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("OutsidePoint"));

  OutsidePoint->SetupAttachment(SceneRoot);

  OutsidePoint->SetMobility(EComponentMobility::Movable);

  OutsidePoint->SetRelativeLocation(FVector(-200.0, 0.0, 0.0));
  OutsidePoint->ArrowColor = FColor::Blue;

  // 内侧路径点。
  InsidePoint = CreateDefaultSubobject<UArrowComponent>(TEXT("InsidePoint"));

  InsidePoint->SetupAttachment(SceneRoot);

  InsidePoint->SetMobility(EComponentMobility::Movable);
  InsidePoint->ArrowColor = FColor::Green;

  InsidePoint->SetRelativeLocation(FVector(200.0, 0.0, 0.0));

  InsidePoint->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
  this->SetActorRotation(FRotator(0.0, 90.0, 0.0));
}

// 校验通道配置：编号、类型、速度、端点、设备编码与绑定对象。
UClass *APassageLaneActor::ResolvePassageClass() const
{
  return PassageActorClass.Get();
}

UClass *APassageLaneActor::GetPoolSource() const
{
  return PassageActorClass.Get();
}

bool APassageLaneActor::ValidateConfiguration(FString &OutError) const
{
  OutError.Reset();

  // ---------------------------
  // 通道基础配置
  // ---------------------------

  if (ChannelId.IsNone())
  {
    OutError = TEXT("ChannelId is required.");
    return false;
  }

  if (SubjectType != EPassageSubjectType::Person &&
      SubjectType != EPassageSubjectType::Car)
  {
    OutError = TEXT("Invalid SubjectType.");
    return false;
  }

  if (!FMath::IsFinite(SpeedOverride) ||
      SpeedOverride < 0.0f)
  {
    OutError = TEXT("SpeedOverride must be zero or positive.");

    return false;
  }

  // ---------------------------
  // 路径端点
  // ---------------------------

  if (!IsValid(OutsidePoint.Get()) ||
      !IsValid(InsidePoint.Get()))
  {
    OutError = TEXT("OutsidePoint or InsidePoint is invalid.");

    return false;
  }

  const FVector Outside = OutsidePoint->GetComponentLocation();

  const FVector Inside = InsidePoint->GetComponentLocation();

  if (Outside.ContainsNaN() || Inside.ContainsNaN() || FVector::Distance(Outside, Inside) <= 1.0)
  {
    OutError = TEXT("Channel endpoints are invalid or too close.");

    return false;
  }

  // ---------------------------
  // 后端设备编码
  // ---------------------------

  if (DeviceCodes.IsEmpty())
  {
    OutError = TEXT("DeviceCodes is empty.");
    return false;
  }

  TSet<FString> UniqueCodes;

  for (const FString &Code : DeviceCodes)
  {
    const FString Clean = Code.TrimStartAndEnd();

    if (Clean.IsEmpty())
    {
      OutError = TEXT("DeviceCodes contains an empty entry.");

      return false;
    }

    if (UniqueCodes.Contains(Clean))
    {
      OutError = FString::Printf(
          TEXT("Duplicate DeviceCode in this lane: %s"),
          *Clean);

      return false;
    }

    UniqueCodes.Add(Clean);
  }

  // 对象池只接受轻量 Pawn 类，使用门原有的 Pawn 白名单。
  UClass *ActorClass = PassageActorClass.Get();
  if (!IsValid(ActorClass) || ActorClass->HasAnyClassFlags(CLASS_Abstract) ||
      !ActorClass->IsChildOf(APawn::StaticClass()))
  {
    OutError = TEXT("Assign a concrete Passage Class derived from PassagePersonActor/PassageCarActor.");
    return false;
  }
  // SCS 根/通行组件在真实池实例创建时校验。
  return GetWorld() != nullptr;
}

// 按进出方向返回世界坐标路线（进入：外→内；离开：内→外）。
void APassageLaneActor::GetRoute(EPassageDirection Direction, FVector &OutStart, FVector &OutEnd) const
{
  // 无效配置时输出零长度路线，
  // 后续 StartPassage 会拒绝执行。
  OutStart = FVector::ZeroVector;
  OutEnd = FVector::ZeroVector;

  if (!IsValid(OutsidePoint.Get()) ||
      !IsValid(InsidePoint.Get()))
  {
    return;
  }

  const FVector Outside = OutsidePoint->GetComponentLocation();

  const FVector Inside = InsidePoint->GetComponentLocation();

  switch (Direction)
  {
  case EPassageDirection::Enter:
    // 进入：外侧 -> 内侧。
    OutStart = Outside;
    OutEnd = Inside;
    break;

  case EPassageDirection::Exit:
    // 离开：内侧 -> 外侧。
    OutStart = Inside;
    OutEnd = Outside;
    break;

  default:
    break;
  }
}

// 开始运行时注册到子系统，按类建立对象池。
void APassageLaneActor::BeginPlay()
{
  Super::BeginPlay();

  UWorld *World = GetWorld();

  if (!World)
  {
    return;
  }

  UPassageSubsystem *Subsystem = World->GetSubsystem<UPassageSubsystem>();

  if (!IsValid(Subsystem))
  {
    UE_LOG(
        LogPassageLane,
        Error,
        TEXT("PassageSubsystem is unavailable for lane '%s'."),
        *GetName());

    return;
  }

  // RegisterChannel 内部会调用 ValidateConfiguration。
  if (!Subsystem->RegisterChannel(this))
  {
    UE_LOG(
        LogPassageLane,
        Error,
        TEXT(
            "Failed to register lane '%s'. "
            "Check the PassageSystem log for details."),
        *GetName());

    return;
  }

  // 子系统在下一帧按蓝图类预热独立展示实例。
}

// 结束运行时从子系统注销本通道。
void APassageLaneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  if (UWorld *World = GetWorld())
  {
    if (UPassageSubsystem *Subsystem =
            World->GetSubsystem<UPassageSubsystem>())
    {
      // 取消本通道租用；最后一个通道注销时由子系统清理整池。
      Subsystem->UnregisterChannel(this);
    }
  }

  Super::EndPlay(EndPlayReason);
}

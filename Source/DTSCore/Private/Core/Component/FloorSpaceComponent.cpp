#include "Core/Component/FloorSpaceComponent.h"

#include "Space/Space.h"

UFloorSpaceComponent::UFloorSpaceComponent()
{
  PrimaryComponentTick.bCanEverTick = false;
}

void UFloorSpaceComponent::InitializeFloor(
    ASpace *InFloorSpace,
    int32 InFloorIndex,
    bool bInCanDisassemble)
{
  FloorSpace = InFloorSpace;
  RuntimeActor = InFloorSpace;
  FloorIndex = InFloorIndex;
  bCanDisassemble = bInCanDisassemble;
  ControlledActors.Reset();
  AnimatedActors.Reset();
  AnimatedActorRelativeTransforms.Reset();

  if (!IsValid(InFloorSpace))
  {
    return;
  }

  OriginalTransform = InFloorSpace->GetActorTransform();
  CurrentTransform = OriginalTransform;
  bHidden = InFloorSpace->IsHidden();

  TArray<AActor *> AttachedActors;

  // 递归取得楼层下的所有附加 Actor。
  InFloorSpace->GetAttachedActors(
      AttachedActors,
      true,
      true);

  const FTransform FloorTransform =
      InFloorSpace->GetActorTransform();

  for (AActor *Actor : AttachedActors)
  {
    if (!IsValid(Actor))
    {
      continue;
    }

    /*
     * Floor 层级下面的所有 Actor 都属于该楼层。
     * 包括 Room、Device、StaticMeshActor 和其他 Actor。
     */
    ControlledActors.AddUnique(Actor);

    const TWeakObjectPtr<AActor> ActorPtr(Actor);

    AnimatedActors.Add(ActorPtr);

    AnimatedActorRelativeTransforms.Add(
        ActorPtr,
        Actor->GetActorTransform().GetRelativeTransform(
            FloorTransform));
  }

  /*
   * 必须先更新父 Actor，再更新子 Actor。
   * 否则更新 Room 后会再次改变已经设置过的 Room 子模型位置。
   */
  auto GetAttachmentDepth = [InFloorSpace](const AActor *Actor)
  {
    int32 Depth = 0;

    for (const AActor *Current = Actor;
         IsValid(Current) && Current != InFloorSpace;
         Current = Current->GetAttachParentActor())
    {
      ++Depth;
    }

    return Depth;
  };

  AnimatedActors.Sort(
      [&GetAttachmentDepth](
          const TWeakObjectPtr<AActor> &Left,
          const TWeakObjectPtr<AActor> &Right)
      {
        return GetAttachmentDepth(Left.Get()) <
               GetAttachmentDepth(Right.Get());
      });
}

FString UFloorSpaceComponent::GetFloorId() const
{
  if (!IsValid(FloorSpace))
  {
    return FString();
  }
  return FloorSpace->Code.IsEmpty() ? FloorSpace->GetActorNameOrLabel() : FloorSpace->Code;
}

TArray<AActor *> UFloorSpaceComponent::GetManagedActors() const
{
  TArray<AActor *> Result;
  if (IsValid(FloorSpace))
  {
    Result.Add(FloorSpace.Get());
  }
  for (AActor *Actor : ControlledActors)
  {
    if (IsValid(Actor))
    {
      Result.AddUnique(Actor);
    }
  }
  return Result;
}

bool UFloorSpaceComponent::ContainsActor(const AActor *Actor) const
{
  if (!IsValid(Actor))
  {
    return false;
  }
  for (const AActor *Current = Actor; IsValid(Current); Current = Current->GetAttachParentActor())
  {
    if (Current == FloorSpace || ControlledActors.Contains(Current))
    {
      return true;
    }
    if (Current->IsA<ASpace>() && Current != FloorSpace)
    {
      break;
    }
  }
  return false;
}

void UFloorSpaceComponent::SetAnimatedLocation(
    const FVector &NewLocation)
{
  if (!IsValid(RuntimeActor))
  {
    return;
  }

  FTransform NewTransform = RuntimeActor->GetActorTransform();
  NewTransform.SetLocation(NewLocation);
  SetAnimatedTransform(NewTransform);
}

void UFloorSpaceComponent::SetAnimatedTransform(
    const FTransform &NewTransform)
{
  if (!IsValid(RuntimeActor))
  {
    return;
  }

  RuntimeActor->SetActorTransform(
      NewTransform,
      false,
      nullptr,
      ETeleportType::TeleportPhysics);

  CurrentTransform = RuntimeActor->GetActorTransform();
  SyncAnimatedActors();
}

void UFloorSpaceComponent::SyncAnimatedActors()
{
  if (!IsValid(RuntimeActor))
  {
    return;
  }

  const FTransform FloorTransform =
      RuntimeActor->GetActorTransform();

  // AnimatedActors 已按父到子的附加深度排序。
  for (const TWeakObjectPtr<AActor> &ActorPtr : AnimatedActors)
  {
    AActor *Actor = ActorPtr.Get();
    if (!IsValid(Actor))
    {
      continue;
    }

    const FTransform *RelativeTransform =
        AnimatedActorRelativeTransforms.Find(ActorPtr);
    if (!RelativeTransform)
    {
      continue;
    }

    const FTransform TargetTransform =
        *RelativeTransform * FloorTransform;

    Actor->SetActorTransform(
        TargetTransform,
        false,
        nullptr,
        ETeleportType::TeleportPhysics);
  }
}

void UFloorSpaceComponent::ResetPresentation()
{
  CurrentOffset = FVector::ZeroVector;
  TargetOffset = FVector::ZeroVector;
  CurrentTransform = OriginalTransform;
  bExpanded = false;
  bHidden = false;
  if (IsValid(RuntimeActor))
  {
    SetAnimatedTransform(OriginalTransform);
    RuntimeActor->SetActorHiddenInGame(false);
  }
}

void UFloorSpaceComponent::OnSpaceSelected()
{
  Super::OnSpaceSelected();
  bSelected = true;
}

void UFloorSpaceComponent::OnSpaceDeselected()
{
  Super::OnSpaceDeselected();
  bSelected = false;
}
// 主动单选
void UFloorSpaceComponent::SelectSingle()
{
  ASpace *FloorsubSpace = Cast<ASpace>(RuntimeActor.Get());

  if (!IsValid(FloorsubSpace))
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "FloorSpaceComponent: RuntimeActor不是有效的ASpace"));
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

  SpaceSubsystem->ToggleSingleSelect(FloorSpace);
}
// 取消所有选择
void UFloorSpaceComponent::DeselectAll()
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

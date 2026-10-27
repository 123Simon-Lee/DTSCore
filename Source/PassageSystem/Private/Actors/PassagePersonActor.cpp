#include "Actors/PassagePersonActor.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PersonPassageComponent.h"

APassagePersonActor::APassagePersonActor()
{
  PrimaryActorTick.bCanEverTick = false;
  AutoPossessPlayer = EAutoReceiveInput::Disabled;
  AutoPossessAI = EAutoPossessAI::Disabled;
  AIControllerClass = nullptr;

  // 根节点代表脚底位置。
  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));

  SetRootComponent(SceneRoot);

  SceneRoot->SetMobility(EComponentMobility::Movable);

  BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));

  BodyMesh->SetupAttachment(SceneRoot);
  BodyMesh->SetMobility(EComponentMobility::Movable);

  // 纯展示，不参与物理和碰撞。
  BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  BodyMesh->SetGenerateOverlapEvents(false);
  BodyMesh->SetSimulatePhysics(false);
  BodyMesh->SetEnableGravity(false);

  BodyMesh->SetOwnerNoSee(false);
  BodyMesh->SetOnlyOwnerSee(false);

  BodyMesh->SetRelativeLocation(FVector::ZeroVector);

  // 适用于常见 UE Manny / Quinn 模型。
  // 其他模型在蓝图中调整，使正面朝 Actor 本地 +X。
  BodyMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

  PassageComponent = CreateDefaultSubobject<UPersonPassageComponent>(TEXT("PersonPassageComponent"));

  PassageComponent->DefaultSpeed = 140.0f;
  PassageComponent->ReferenceWalkSpeed = 140.0f;

  // 根节点就是脚底。
  PassageComponent->bUseCharacterCapsuleHeight = false;
  PassageComponent->HeightOffset = 0.0f;
  PassageComponent->bFollowPitch = false;

  SetActorEnableCollision(false);
}

void APassagePersonActor::BeginPlay()
{
  // 只隐藏 Actor；开始通行时由移动组件恢复显示。
  SetActorHiddenInGame(true);
  Super::BeginPlay();
}

void APassagePersonActor::SetMeshhidden(bool bHided)
{
  BodyMesh->SetHiddenInGame(bHided);
}

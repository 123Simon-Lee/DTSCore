#include "Actors/PassageCarActor.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CarPassageComponent.h"

APassageCarActor::APassageCarActor()
{
  PrimaryActorTick.bCanEverTick = false;
  AutoPossessPlayer = EAutoReceiveInput::Disabled;
  AutoPossessAI = EAutoPossessAI::Disabled;
  AIControllerClass = nullptr;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));

  SetRootComponent(SceneRoot);

  SceneRoot->SetMobility(EComponentMobility::Movable);

  BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));

  BodyMesh->SetupAttachment(SceneRoot);
  BodyMesh->SetMobility(EComponentMobility::Movable);

  BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

  BodyMesh->SetGenerateOverlapEvents(false);
  BodyMesh->SetSimulatePhysics(false);
  BodyMesh->SetEnableGravity(false);

  BodyMesh->SetOwnerNoSee(false);
  BodyMesh->SetOnlyOwnerSee(false);

  // 不同车辆的模型轴向和原点可能不同。
  // 在蓝图中调整 BodyMesh，使车头朝 Actor 本地 +X。
  BodyMesh->SetRelativeLocation(FVector::ZeroVector);
  BodyMesh->SetRelativeRotation(FRotator::ZeroRotator);

  PassageComponent = CreateDefaultSubobject<UCarPassageComponent>(TEXT("CarPassageComponent"));

  PassageComponent->DefaultSpeed = 300.0f;
  PassageComponent->AnimationPlayRate = 1.0f;

  PassageComponent->bUseCharacterCapsuleHeight = false;
  PassageComponent->HeightOffset = 0.0f;
  PassageComponent->bFollowPitch = true;

  SetActorEnableCollision(false);
}

void APassageCarActor::BeginPlay()
{
  // 只隐藏 Actor，避免 Mesh 独立隐藏后无法随 Actor 一起显示。
  SetActorHiddenInGame(true);
  Super::BeginPlay();
}

void APassageCarActor::SetMeshhidden(bool bHided)
{
  BodyMesh->SetHiddenInGame(bHided);
}

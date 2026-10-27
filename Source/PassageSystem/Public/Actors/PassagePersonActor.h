#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PassagePersonActor.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UPersonPassageComponent;

/**
 * 仅用于通行展示的人物。
 * 由 PersonPassageComponent 播放动画并沿通道移动。
 */
UCLASS(BlueprintType, Blueprintable)
class PASSAGESYSTEM_API APassagePersonActor : public APawn
{
  GENERATED_BODY()

public:
  APassagePersonActor();

  // 根节点位于人物脚底，直接对应通道的地面端点。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USceneComponent> SceneRoot;

  // 在蓝图子类中设置人物骨骼网格。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USkeletalMeshComponent> BodyMesh;

  // 在此组件中设置 WalkAnimation、DefaultSpeed 等参数。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UPersonPassageComponent> PassageComponent;

protected:
  virtual void BeginPlay() override;

public:
  void SetMeshhidden(bool bHided);
};

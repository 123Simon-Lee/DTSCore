#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PassageCarActor.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UCarPassageComponent;

/**
 * 仅用于通行展示的车辆。
 *
 * 由 CarPassageComponent 播放车辆动画并沿通道移动。
 */
UCLASS(BlueprintType, Blueprintable)
class PASSAGESYSTEM_API APassageCarActor : public APawn
{
  GENERATED_BODY()

public:
  APassageCarActor();

  // 根节点对应车道地面上的路径位置。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USceneComponent> SceneRoot;

  // 在蓝图子类中设置车辆骨骼网格。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USkeletalMeshComponent> BodyMesh;

  // 在此组件中设置 DriveAnimation、DefaultSpeed 等参数。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage", meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UCarPassageComponent> PassageComponent;

protected:
  virtual void BeginPlay() override;

public:
  void SetMeshhidden(bool bHided);
};

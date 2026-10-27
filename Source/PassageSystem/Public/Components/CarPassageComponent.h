// 车辆通行组件
#pragma once

#include "CoreMinimal.h"
#include "Components/PassageMovementComponent.h"

#include "CarPassageComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * 车辆通行组件。
 *
 * 继承 UPassageMovementComponent，在其直线位移基础上叠加“车辆行驶”表现：
 * 通行时循环播放 DriveAnimation，并按 AnimationPlayRate 设定播放速率。
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Passage), meta = (BlueprintSpawnableComponent))
class PASSAGESYSTEM_API UCarPassageComponent : public UPassageMovementComponent
{
  GENERATED_BODY()
public:
  UCarPassageComponent();

  /** 车辆行驶动画序列。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  TObjectPtr<UAnimSequence> DriveAnimation;

  /** 行驶动画播放速率。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", Meta = (ClampMin = "0.01"))
  float AnimationPlayRate = 1.0f;

protected:
  virtual bool PreparePresentation(FString &OutError) override;
  virtual void BeginPresentation() override;
  virtual void EndPresentation() override;

private:
  /** 缓存骨骼网格组件，避免重复查找。 */
  TWeakObjectPtr<USkeletalMeshComponent> CachedMesh;
};

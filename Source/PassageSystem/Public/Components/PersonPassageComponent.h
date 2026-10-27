// 公共移动逻辑
#pragma once

#include "CoreMinimal.h"
#include "Components/PassageMovementComponent.h"

#include "PersonPassageComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

/**
 * 人物通行组件。
 *
 * 继承 UPassageMovementComponent，在其直线位移基础上叠加“人物行走”表现：
 * 通行时按当前速度与 ReferenceWalkSpeed 的比例换算动画播放速率，循环播放
 * WalkAnimation；不播放动画时仅做位移。
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Passage), meta = (BlueprintSpawnableComponent))
class PASSAGESYSTEM_API UPersonPassageComponent : public UPassageMovementComponent
{
  GENERATED_BODY()

public:
  UPersonPassageComponent();

  /** 是否播放行走动画；关闭后仅位移、不驱动动画。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  bool bPlayWalkAnimation = true;

  /** 绑定人物动画序列（行走动画）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  TObjectPtr<UAnimSequence> WalkAnimation;

  /** 人物走动速度（作为动画播放速率的基准）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "1.0"))
  float ReferenceWalkSpeed = 140.0f;

protected:
  virtual bool PreparePresentation(FString &OutError) override;
  virtual void BeginPresentation() override;

  virtual void UpdatePresentation(float DeltaTime, float DeltaDistance) override;
  virtual void EndPresentation() override;

private:
  /** 缓存骨骼网格组件，避免每次调用 FindOwnerMesh。 */
  TWeakObjectPtr<USkeletalMeshComponent> CachedMesh;
};

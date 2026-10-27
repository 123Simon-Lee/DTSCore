// Base组件
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "PassageTypes.h"

#include "PassageMovementComponent.generated.h"

class UMovementComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UBoxComponent;

/**
 * 通行移动组件（抽象基类）
 */
UCLASS(Abstract, BlueprintType, Blueprintable, ClassGroup = (Passage), meta = (BlueprintSpawnableComponent))
class PASSAGESYSTEM_API UPassageMovementComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  UPassageMovementComponent();

  /** 默认通行速度（单位/秒）。当 SpeedOverride 无效或未提供时使用。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "1.0"))
  float DefaultSpeed = 140.0f;

  /** 加速度（单位/秒²）。为 0 时使用 DefaultSpeed 匀速通行。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "0.0"))
  float Acceleration = 0.0f;

  /** 在地面路径高度基础上附加的偏移。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  float HeightOffset = 0.0f;

  /** 是否自动增加 Character 胶囊体半高（用于把通行高度抬到胶囊底以上）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  bool bUseCharacterCapsuleHeight = false;

  /** 是否跟随路径俯仰角旋转；人物关闭，车辆通常开启。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  bool bFollowPitch = false;

  /** 通行时只启用此查询框；模型仍不阻挡场景或其他人物。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage|Trigger")
  FVector TriggerBoxExtent = FVector(35.0, 35.0, 90.0);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage|Trigger")
  FVector TriggerBoxOffset = FVector(0.0, 0.0, 90.0);


  /** 当前实际速度（只读，运行期由 Tick 更新）。 */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage")
  float CurrentSpeed = 0.0f;

  /**
   * 开始通行。
   *
   * @param GroundStart   地面起点。
   * @param GroundEnd     地面终点。
   * @param SpeedOverride 速度覆盖；<=0 时使用 DefaultSpeed。
   * @param OutError      失败时输出错误原因。
   * @return 是否成功启动通行。
   */
  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool StartPassage(
      FVector GroundStart,
      FVector GroundEnd,
      float SpeedOverride,
      FString &OutError);

  /** 取消通行（等价于以失败结束，广播 OnFinished）。 */
  UFUNCTION(BlueprintCallable, Category = "Passage")
  void CancelPassage(const FString &Reason);

  /** 是否正在通行中。 */
  UFUNCTION(BlueprintCallable, Category = "Passage")
  bool IsPassing() const { return bRunning; }

  /** 通行结束事件；成功或失败都会广播一次。 */
  UPROPERTY(BlueprintAssignable, Category = "Passage")
  FPassageMovementFinishedEvent OnFinished;

  virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

protected:
  virtual void BeginPlay() override;
  /** 查找 Owner 的骨骼网格组件：Character 优先取 Mesh，否则按类型查找。 */
  USkeletalMeshComponent *FindOwnerMesh() const;

  /** 开始前校验/准备，失败可返回 false 终止通行。 */
  virtual bool PreparePresentation(FString &OutError) { return true; }

  /** 正式开始时调用一次。 */
  virtual void BeginPresentation() {};

  /** 每帧驱动表现，DeltaDistance 为本帧前进距离。 */
  virtual void UpdatePresentation(float DeltaTime, float DeltaDistance) {}

  /** 结束（成功或失败）时调用。 */
  virtual void EndPresentation() {};

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  /** 按当前进度（MovedDistance）设置 Owner 的位置与朝向。 */
  void ApplyPose();

  /** 暂停 Owner 的移动组件与物理模拟，避免与通行位移冲突。 */
  void SuspendOwnerMovement();

  /** 恢复被暂停的移动组件与物理模拟。 */
  void RestoreOwnerMovement();

  /** 结束通行：更新状态、关闭 Tick 并广播 OnFinished。 */
  void FinishMovement(bool bSuccess, const FString &Reason);

  /** 通行起点（已叠加高度偏移后的实际位置）。 */
  FVector StartLocation = FVector::ZeroVector;
  /** 通行终点（已叠加高度偏移后的实际位置）。 */
  FVector EndLocation = FVector::ZeroVector;

  /** 通行方向（单位向量）。 */
  FVector MoveDirection = FVector::ForwardVector;

  /** 起终点总距离。 */
  float TotalDistance = 0.0f;
  /** 已走过的距离。 */
  float MovedDistance = 0.0f;
  /** 目标速度（加速模式下最终要达到的速度）。 */
  float TargetSpeed = 0.0f;

  /** 是否正在通行。 */
  bool bRunning = false;
  /** 通行前 Owner 的碰撞开关状态，用于结束后恢复。 */
  bool bPreviousCollisionEnabled = false;

  UPROPERTY(Transient)
  TObjectPtr<UBoxComponent> PassageTrigger;


  /** 暂停的移动组件（结束后恢复激活）。 */
  TArray<TWeakObjectPtr<UMovementComponent>> SuspendedMovement;
  /** 暂停的物理组件（结束后恢复物理模拟）。 */
  TArray<TWeakObjectPtr<UPrimitiveComponent>> SuspendedPhysics;
  // 通行时除专用查询框之外的原语都不参与碰撞，结束后恢复原配置。
  TArray<TPair<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type>> SuspendedCollision;
};

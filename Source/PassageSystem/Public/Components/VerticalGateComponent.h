#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "VerticalGateComponent.generated.h"

class APawn;
class UBoxComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVerticalGateMotionFinished);

UCLASS(BlueprintType, Blueprintable, classGroup = (Passage), meta = (BlueprintSpawnableComponent))
class PASSAGESYSTEM_API UVerticalGateComponent : public USceneComponent
{
  GENERATED_BODY()
public:
  UVerticalGateComponent();

  // 完整开启或关闭需要的时间，单位：秒。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VerticalGate", meta = (ClampMin = "0.0", Units = "s"))
  float Duration = 1.0f;
  // 开启、关闭都会触发这个关闭事件
  UPROPERTY(BlueprintAssignable, Category = "VerticalGate")
  FVerticalGateMotionFinished OnMotionFinished;

  UFUNCTION(BlueprintCallable, Category = "VerticalGate")
  void Open();

  UFUNCTION(BlueprintCallable, Category = "VerticalGate")
  void Close();

  UFUNCTION(BlueprintPure, Category = "VerticalGate")
  bool IsOpen() const;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VerticalGateTrigger")
  bool bAutoTrigger = true;

  // 半尺寸，单位厘米，按实际车道调整。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VerticalGateTrigger")
  FVector TriggerBoxExtent = FVector(300.0, 600.0, 150.0);

  // 相对于关闭状态下旋转组件的位置偏移。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VerticalGateTrigger")
  FVector TriggerBoxOffset = FVector(250.0, 0.0, 0.0);

  // 运行时创建。
  UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "VerticalGateTrigger")
  TObjectPtr<UBoxComponent> TriggerBox = nullptr;

  // 允许触发道闸的 Pawn 类，支持它们的蓝图子类。
  // 空白名单不允许任何对象触发。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VerticalGateTrigger")
  TSet<TSubclassOf<APawn>> AllowList;

protected:
  virtual void BeginPlay() override;

  virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  void SetTargetPitch(float NewTargetPitch);
  void ApplyPitch();
  void FinishMovement();

  FRotator FixedRotation = FRotator::ZeroRotator;

  float CurrentPitch = 0.0f;
  float TargetPitch = 0.0f;

  bool HasVehicleInside() const;
  void RefreshAutoState();

  UFUNCTION()
  void HandleTriggerBegin(
      UPrimitiveComponent *OverlappedComponent,
      AActor *OtherActor,
      UPrimitiveComponent *OtherComp,
      int32 OtherBodyIndex,
      bool bFromSweep,
      const FHitResult &SweepResult);

  UFUNCTION()
  void HandleTriggerEnd(
      UPrimitiveComponent *OverlappedComponent,
      AActor *OtherActor,
      UPrimitiveComponent *OtherComp,
      int32 OtherBodyIndex);

  bool IsAllowedActor(AActor *Actor) const;
};
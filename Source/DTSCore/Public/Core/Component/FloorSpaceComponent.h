#pragma once

#include "CoreMinimal.h"
#include "Space/Component/SpaceSelectorComponent.h"
#include "Space/SpaceSubsystem.h"
#include "FloorSpaceComponent.generated.h"

class ASpace;

static constexpr int32 BUILDING_INVALID_FLOOR_INDEX = -999999;

/** 楼层空间的表现组件。ASpace 是唯一数据源，不再复制 RuntimeNode 树。 */
UCLASS(ClassGroup = (Space), meta = (BlueprintSpawnableComponent), BlueprintType)
class DTSCORE_API UFloorSpaceComponent : public USpaceSelectorComponent
{
  GENERATED_BODY()

public:
  UFloorSpaceComponent();

  UFUNCTION(BlueprintCallable, Category = "Space Selection")
  void SelectSingle();

  UFUNCTION(BlueprintCallable, Category = "Space Selection")
  void DeselectAll();

public:
  virtual void OnSpaceSelected() override;
  virtual void OnSpaceDeselected() override;

  void InitializeFloor(ASpace *InFloorSpace, int32 InFloorIndex, bool bInCanDisassemble);

  UFUNCTION(BlueprintPure, Category = "Floor")
  ASpace *GetFloorSpace() const { return FloorSpace.Get(); }

  UFUNCTION(BlueprintPure, Category = "Floor")
  FString GetFloorId() const;

  UFUNCTION(BlueprintPure, Category = "Floor")
  int32 GetFloorIndex() const { return FloorIndex; }

  TArray<AActor *> GetManagedActors() const;
  bool ContainsActor(const AActor *Actor) const;

  /**
   * 设置楼层动画位置，并显式同步楼层下的 Room 与模型 Actor。
   *
   * 不依赖附件组件是否启用了 Absolute Location，确保所有附属 Actor
   * 都使用初始化时相对楼层的空间关系跟随移动。
   */
  void SetAnimatedLocation(const FVector &NewLocation);

  /** 设置楼层动画变换，并同步全部附属 Actor。 */
  void SetAnimatedTransform(const FTransform &NewTransform);

  /** 按当前楼层变换重新同步全部附属 Actor。 */
  void SyncAnimatedActors();

  void ResetPresentation();

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  TObjectPtr<ASpace> FloorSpace = nullptr;

  /** 动画根，始终指向 FloorSpace。 */
  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  TObjectPtr<AActor> RuntimeActor = nullptr;

  /** Floor 附加层级下的全部子 Actor，包括 Room、Device 和普通模型。 */
  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  TArray<TObjectPtr<AActor>> ControlledActors;

  /**
   * 楼层下全部附属 Actor，按附加深度从父到子排列。
   *
   * 使用弱引用，避免拆楼组件延长场景 Actor 生命周期。
   */
  TArray<TWeakObjectPtr<AActor>> AnimatedActors;

  /** 每个附属 Actor 初始化时相对于楼层的变换。 */
  TMap<TWeakObjectPtr<AActor>, FTransform> AnimatedActorRelativeTransforms;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  int32 FloorIndex = BUILDING_INVALID_FLOOR_INDEX;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  bool bCanDisassemble = true;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  FTransform OriginalTransform;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  FTransform CurrentTransform;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  FBox Bounds;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  FVector CurrentOffset = FVector::ZeroVector;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  FVector TargetOffset = FVector::ZeroVector;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  bool bHidden = false;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  bool bExpanded = false;

  UPROPERTY(BlueprintReadOnly, Category = "Floor")
  bool bSelected = false;
};

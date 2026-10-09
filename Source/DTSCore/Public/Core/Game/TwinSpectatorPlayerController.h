#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "TwinSpectatorPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnControllerReady);

class UInputMappingContext;
class ATwinSpectatorPawn;
UENUM(BlueprintType)
enum class EViewMode : uint8
{
  Spectator UMETA(DisplayName = "Spectator（俯视漫游）"),
  Roaming UMETA(DisplayName = "Roaming（角色漫游）")
};

/**
 * 相机开始或停止移动时触发。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnCameraMovingChanged,
    bool,
    bIsMoving);

/**
 * 视图模式镜头混合结束事件。
 *
 * 此事件只会在目标 Pawn 已经被 PlayerController Possess、视图目标和输入映射
 * 均切换完成后广播。依赖目标 Pawn Controller 的业务（例如设备聚焦）应从这里继续。
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnViewModeBlendFinished,
    EViewMode,
    ViewMode);

UCLASS()
class DTSCORE_API ATwinSpectatorPlayerController : public APlayerController
{
  GENERATED_BODY()

public:
  UPROPERTY(BlueprintAssignable, Category = "Init")
  FOnControllerReady OnControllerReady;

  /** 目标 Pawn 完成 Possess、视图和输入映射切换后广播。 */
  UPROPERTY(BlueprintAssignable, Category = "ViewMode")
  FOnViewModeBlendFinished OnViewModeBlendFinished;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  APawn *TwinSpectatorPawn;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  APawn *CharacterPawn;

  UPROPERTY(BlueprintReadOnly, Category = "ViewMode")
  EViewMode CurrentViewMode = EViewMode::Spectator;

  ATwinSpectatorPlayerController();

  virtual void BeginPlay() override;
  virtual void Tick(float DeltaTime) override;

  // 按模式切换，外部统一调这一个
  UFUNCTION(BlueprintCallable, Category = "ViewMode")
  void SwitchViewMode(EViewMode NewMode);

  // 获取当前模式
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewMode")
  EViewMode GetCurrentViewMode() const { return CurrentViewMode; }

  UFUNCTION(BlueprintCallable, Category = "Switch")
  void SwitchToSpectator();
  UFUNCTION(BlueprintCallable, Category = "Switch")
  void SwitchToCharacter();

  /**
   * 从当前画面单段过渡到设备最终聚焦镜头。
   * TransitionCamera 保持当前画面，后台立即 Possess Spectator Pawn 并准备好
   * 最终设备姿态，因此不会经过 Spectator Pawn 上一次停留的位置。
   */
  UFUNCTION(BlueprintCallable, Category = "Device Focus")
  bool BeginDeviceFocusTransition(
      AActor *TargetDevice,
      float TransitionDuration = 0.5f,
      float ZoomScale = 1.f);

  /** 最终画面相机移动状态发生变化时触发。 */
  UPROPERTY(BlueprintAssignable, Category = "Camera Movement")
  FOnCameraMovingChanged OnCameraMovingChanged;

  /**
   * 当前最终画面相机是否正在移动。
   */
  UFUNCTION(BlueprintPure, Category = "Camera Movement")
  bool IsMoving() const
  {
    return bCameraMoving;
  }

private:
  bool bBlending = false;

  FVector BlendStartLoc;
  FRotator BlendStartRot;

  float BlendTime = 0.f;
  float BlendDuration = 1.f;
  float ActiveBlendDuration = 1.f;

  UPROPERTY()
  APawn *BlendTargetPawn = nullptr;

  // 本次 Blend 锁定的路径选择：false=短弧，true=长弧。
  // 仅在 Blend 被新请求打断时，根据上一帧实际方向判断一次。
  bool bUseLongRotationPath = false;

  // 上一帧世界空间旋转增量的单位轴方向，不包含角速度大小。
  // 用于比较新目标短弧是否会造成视觉上的反向掉头。
  FVector LastBlendAngularDirection = FVector::ZeroVector;

  // 开始插值
  void StartBlend(APawn *NewPawn, float DurationOverride = -1.f);
  // 更新插值
  void UpdateBlend(float DeltaTime);
  // 结束插值
  void FinishBlend();
  // 切换输入方式
  void SwitchInputContext(UInputMappingContext *NewContext);
  // 临时相机，用于设备聚焦过渡
  UPROPERTY()
  class ACameraActor *TransitionCamera;

  /** 更新最终画面相机的移动状态。 */
  void UpdateCameraMovingState();

  /** 上一帧最终画面相机Transform。 */
  FTransform LastCameraTransform = FTransform::Identity;

  /** 是否已经记录过相机Transform。 */
  bool bHasLastCameraTransform = false;

  /** 最终画面相机当前是否正在移动。 */
  bool bCameraMoving = false;

  /** 位置变化阈值，单位厘米。 */
  UPROPERTY(
      EditAnywhere,
      Category = "Camera Movement",
      meta = (ClampMin = "0.0",
              AllowPrivateAccess = "true"))
  float CameraLocationTolerance = 0.01f;

  /** 旋转变化阈值，单位度。 */
  UPROPERTY(
      EditAnywhere,
      Category = "Camera Movement",
      meta = (ClampMin = "0.0",
              AllowPrivateAccess = "true"))
  float CameraRotationTolerance = 0.001f;
};

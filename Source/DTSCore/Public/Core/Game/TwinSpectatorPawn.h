#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include <Components/TimelineComponent.h>
#include "Core/Camera/BuildingCameraFocusInterface.h"
#include "TwinSpectatorPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
class APlayerController;

// 相机控制模式
UENUM(BlueprintType)
enum class ECameraControlMode : uint8
{
  // 鸟瞰模式：禁用WASD，启用鼠标右键拖动位移，左键旋转
  BirdsEye UMETA(DisplayName = "鸟瞰视角"),
  // 设备定位模式：启用WASD，禁用鼠标右键拖动位移，左键旋转锁定
  DeviceFocus UMETA(DisplayName = "设备定位"),
  // 自由移动模式：启用WASD，鼠标滚轮进行速度调整，space向上，shift向下
  FreeMove UMETA(DisplayName = "自由移动"),
};

/** 设备正面所使用的轴。 */
UENUM(BlueprintType)
enum class EDeviceFocusAxis : uint8
{
  PositiveX UMETA(DisplayName = "X 正方向"),
  NegativeX UMETA(DisplayName = "X 负方向"),
  PositiveY UMETA(DisplayName = "Y 正方向"),
  NegativeY UMETA(DisplayName = "Y 负方向"),
  PositiveZ UMETA(DisplayName = "Z 正方向"),
  NegativeZ UMETA(DisplayName = "Z 负方向")
};

/** 设备正面轴使用设备局部坐标还是固定世界坐标。 */
UENUM(BlueprintType)
enum class EDeviceFocusAxisSpace : uint8
{
  DeviceLocal UMETA(DisplayName = "设备局部坐标"),
  World UMETA(DisplayName = "世界坐标")
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCameraMoveFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeviceFocusReturnRequested);

UCLASS()
class DTSCORE_API ATwinSpectatorPawn : public APawn, public IBuildingCameraFocusInterface
{
  GENERATED_BODY()

public:
  ATwinSpectatorPawn();

protected:
  virtual void BeginPlay() override;

public:
  virtual void Tick(float DeltaTime) override;
  virtual void SetupPlayerInputComponent(UInputComponent *PlayerInputComponent) override;

  void OnLeftDown(const FInputActionValue &);
  void OnLeftUp(const FInputActionValue &);
  void OnRightDown(const FInputActionValue &);
  void OnRightUp(const FInputActionValue &);

public:
  UPROPERTY(BlueprintAssignable, Category = "Camera")
  FOnCameraMoveFinished OnCameraMoveFinished;

  /** 设备聚焦模式下按下鼠标右键时，请求业务层退出设备聚焦。 */
  UPROPERTY(BlueprintAssignable, Category = "Device Focus")
  FOnDeviceFocusReturnRequested OnDeviceFocusReturnRequested;

  // ===== 控制模式 =====
  UPROPERTY(BlueprintReadOnly, Category = "Camera Control")
  ECameraControlMode CameraControlMode = ECameraControlMode::BirdsEye;

  /** 设备聚焦时 Pawn 对齐的设备局部轴；当前项目统一使用设备局部 Y 正方向。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Focus")
  EDeviceFocusAxis DeviceFocusAxis = EDeviceFocusAxis::PositiveY;

  /** 设备轴默认跟随目标 Actor 的世界旋转。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device Focus")
  EDeviceFocusAxisSpace DeviceFocusAxisSpace = EDeviceFocusAxisSpace::DeviceLocal;

  // 切换控制模式
  UFUNCTION(BlueprintCallable, Category = "Camera Control")
  void SetCameraControlMode(ECameraControlMode NewMode);

  /** 按当前控制模式同步 Pitch 限制；视图过渡期间可传入尚未接管此 Pawn 的控制器。 */
  void ApplyCameraPitchLimits(APlayerController *PC) const;

  // 各模式下的行为判断（内部使用）
  bool CanUseWASD() const;
  bool CanRightMousePan() const;
  bool CanLeftMouseRotate() const;

  // ===== Input =====
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputMappingContext *MappingContext;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_Zoom;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_LookAndMouse;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_Move; // WASD
  // Space/Shift
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_VerticalMove; // 垂直移动

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_RightMouse; // 右键鼠标

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_LeftMouse; // 左键鼠标

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
  UInputAction *IA_Touch;

  UPROPERTY(EditAnywhere, Category = "Input Config")
  FVector WASDMoveInput = FVector::ZeroVector;

  UPROPERTY(EditAnywhere, Category = "Input Config")
  float WASDMoveSpeed = 500.f;

  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float FreeMoveSpeed = 2000.f;

  /** 滚轮每次调整的速度。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float FreeMoveSpeedStep = 250.f;

  /** 自由移动最小速度。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float FreeMoveMinSpeed = 100.f;

  /** 自由移动最大速度。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float FreeMoveMaxSpeed = 20000.f;

  /** Space/Shift 垂直移动速度比例。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move",
      meta = (ClampMin = "0.0", UIMin = "0.0"))
  float FreeMoveVerticalSpeedScale = 1.f;

  /**
   * FreeMove 手动移动允许到达的最低世界高度。
   * 只限制手动移动，不影响拆楼相机 Timeline。
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Free Move")
  float FreeMoveMinAltitude = 500.f;

  /** Space 为 +1，Shift 为 -1。 */
  UPROPERTY(BlueprintReadOnly, Category = "Free Move")
  float VerticalMoveInput = 0.f;

  // ===== 鼠标状态 =====
  UPROPERTY(BlueprintReadOnly)
  bool bRightMouseDown = false;
  UPROPERTY(BlueprintReadOnly)
  bool bLeftMouseDown = false;

  // ===== 速度（惯性）=====
  UPROPERTY(BlueprintReadOnly)
  float ZoomVelocity = 0.f;

  // ===== 参数 =====
  UPROPERTY(EditAnywhere)
  float MoveSpeed = -100.f;

  UPROPERTY(EditAnywhere)
  float ZoomSpeed = 2000.f;

  UPROPERTY(EditAnywhere)
  float RotateSpeed = 5.f;

  /**
   * CameraConfig.ini 的修改检测间隔。
   * Tick 只检查文件时间戳，文件变化后才重新读取内容，避免每帧访问磁盘。
   */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Runtime Config",
      meta = (ClampMin = "0.1", UIMin = "0.1"))
  float CameraConfigPollInterval = 0.5f;

  UPROPERTY(EditAnywhere)
  float Damping = 5.f;

  UPROPERTY(EditAnywhere)
  float MinZoom = 100.f;

  UPROPERTY(EditAnywhere)
  float MaxZoom = 60000.0f;

  /** 设备聚焦模式下滚轮允许到达的最近 SpringArm 距离。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Focus Zoom",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float DeviceFocusMinZoom = 80.f;

  /** 设备聚焦模式下滚轮允许到达的最远 SpringArm 距离。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Focus Zoom",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float DeviceFocusMaxZoom = 1200.f;

  /** 设备聚焦模式专用滚轮速度，避免沿用鸟瞰速度后单次缩放跨度过大。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Focus Zoom",
      meta = (ClampMin = "0.0", UIMin = "0.0"))
  float DeviceFocusZoomSpeed = 300.f;

  /** 设备拆解时使用的机械臂目标长度。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Dismantle Zoom",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float DeviceDismantleArmLength = 100.f;

  /** 设备拆解状态下滚轮允许到达的最近距离。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Dismantle Zoom",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float DeviceDismantleMinZoom = 50.f;

  /** 设备拆解状态下滚轮允许到达的最远距离。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Dismantle Zoom",
      meta = (ClampMin = "1.0", UIMin = "1.0"))
  float DeviceDismantleMaxZoom = 300.f;

  /** 聚焦视角和拆解视角之间切换所用时间。 */
  UPROPERTY(
      EditAnywhere,
      BlueprintReadWrite,
      Category = "Device Dismantle Zoom",
      meta = (ClampMin = "0.0", UIMin = "0.0"))
  float DeviceDismantleZoomInterpTime = 0.5f;

  // ===== Touch =====
  TMap<ETouchIndex::Type, FVector> TouchMap;
  UPROPERTY(BlueprintReadOnly)
  float LastPinchDistance = 0.f;

  // 输入状态
  /** WASD 是否正在产生移动输入 */
  UPROPERTY(BlueprintReadOnly, Category = "Input State")
  bool bKeyboardMoving = false;

  /** 鼠标是否正在拖动相机 */
  UPROPERTY(BlueprintReadOnly, Category = "Input State")
  bool bMouseMoving = false;

  /** 输入死区，防止鼠标微小抖动 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input State")
  float InputMoveDeadZone = 0.01f;

public:
  // ===== Components =====
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  USceneComponent *Root;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  USpringArmComponent *SpringArm;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  UCameraComponent *Camera;

  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FVector CameraStartLocation;
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FRotator CameraStartRotate;
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  float SpringStartLength;

  UPROPERTY(BlueprintReadOnly)
  FVector CameraLocation;
  UPROPERTY(BlueprintReadOnly)
  FRotator CameraRotate;
  UPROPERTY(BlueprintReadOnly)
  float SpringLen;

  // Timeline
  FTimeline CameraTimeline;
  UPROPERTY(EditAnywhere)
  UCurveFloat *CameraCurve;

  UPROPERTY(BlueprintReadOnly)
  FVector TargetLocation;
  UPROPERTY(BlueprintReadOnly)
  FRotator TargetRotation;
  UPROPERTY(BlueprintReadOnly)
  float TargetArmLength;
  // 最近时的移动速度比例，0.05 = 最近时速度是最远时的 5%
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Move")
  float MinMoveSpeedScale = 0.3f;

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void OnCameraTimelineUpdate(float Value);

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void OnCameraTimelineFinished();

  void MouseMove(const FInputActionValue &Value);
  void MouseMoveStop(const FInputActionValue &Value);
  void SetCameraMove(float Val, bool bX);
  void SetCameraRotate(float Val, bool bX);
  void Zoom(const FInputActionValue &Value);
  // WASD运动
  void WASDMove(const FInputActionValue &Value); // 新增WASD处理
  void WASDMoveStop(const FInputActionValue &Value);
  // 垂直运动
  void VerticalMove(const FInputActionValue &Value);
  void VerticalMoveStop(const FInputActionValue &Value);
  // 触屏
  void OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
  void OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);
  void OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void GetCameraLocation();

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void SetCameraNewLocation(float Time, FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength);

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void CameraMove(FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength);

  /** 使用指定时长播放相机移动。 */
  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void CameraMoveWithDuration(
      FVector CameraNewLocation,
      FRotator CameraNewRotate,
      float SpringNewLength,
      float Duration);

  /** 设置机械臂长度；InterpTime 为 0 时立即生效。 */
  UFUNCTION(BlueprintCallable, Category = "Camera")
  void SetSpringArmLength(
      float NewArmLength,
      float InterpTime = 0.5f);

  /** 设置设备聚焦模式的滚轮缩放范围。 */
  UFUNCTION(BlueprintCallable, Category = "Camera")
  void SetDeviceFocusZoomRange(
      float NewMinZoom,
      float NewMaxZoom,
      bool bClampCurrentArm = true);

  /**
   * 进入或退出设备拆解视角。
   * 进入时保存聚焦距离和滚轮范围，退出时自动恢复。
   */
  UFUNCTION(BlueprintCallable, Category = "Device Dismantle Zoom")
  void SetDeviceDismantleViewEnabled(bool bEnabled);

  /** NewRotation 使用世界旋转。 */
  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void FocusOnActor(AActor *TargetActor, float InterpTime, float ZoomScale, FRotator NewRotation);

  /** 聚焦设备并切换到设备定位控制模式。 */
  UFUNCTION(BlueprintCallable, Category = "Device Focus")
  void FocusOnDevice(AActor *TargetDevice, float InterpTime, float ZoomScale);

  /**
   * 立即把 Pawn 准备到设备最终聚焦姿态，不播放 Pawn 自身的相机 Timeline。
   * 供 PlayerController 的专用过渡相机使用，避免先飞到 Pawn 旧位置再二次聚焦。
   */
  UFUNCTION(BlueprintCallable, Category = "Device Focus")
  void FocusOnDeviceImmediate(AActor *TargetDevice, float ZoomScale = 1.f);

  /** 恢复到进入设备聚焦前保存的拆楼层/场景相机视角。 */
  UFUNCTION(BlueprintCallable, Category = "Device Focus")
  void RestoreDeviceFocusView(float InterpTime = 0.5f);

  /**
   * 设备聚焦状态重置 用于直接切换业务逻辑
   * 清除设备聚焦的高优先级隔离请求
   * 切换业务页面时丢弃设备聚焦返回视角，不播放返回动画。
   * 新业务页面会立即提交自己的相机目标。
   */
  UFUNCTION(BlueprintCallable, Category = "Device Focus")
  void DiscardDeviceFocusView();

  /** 将设备聚焦轴转换成 Pawn 的绝对世界旋转。 */
  UFUNCTION(BlueprintPure, Category = "Device Focus")
  FRotator GetDeviceFocusRotation(AActor *TargetActor) const;

  /** 相机移动 Timeline 当前是否正在播放。 */
  UFUNCTION(BlueprintPure, Category = "Camera Move Event")
  bool IsCameraMoveRunning() const;

  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  void ResetCameraLocation();

  // 拿到此时相机的fov
  UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
  float GetFOV();

  UFUNCTION(BlueprintPure, Category = "Input State")
  bool IsKeyboardMoving() const
  {
    return bKeyboardMoving;
  }

  UFUNCTION(BlueprintPure, Category = "Input State")
  bool IsMouseMoving() const
  {
    return bMouseMoving;
  }

  /** 鼠标或键盘任意一种正在控制相机 */
  UFUNCTION(BlueprintPure, Category = "Input State")
  bool IsUserInputMoving() const
  {
    return bKeyboardMoving || bMouseMoving;
  }

  // IBuildingCameraFocusInterface 相机聚焦接口
  virtual void SaveBuildingFocusView_Implementation() override;
  virtual void FocusBuildingTarget_Implementation(
      const FBuildingFocusTarget &Target,
      const FBuildingFocusSettings &Settings) override;
  virtual void RestoreBuildingFocusView_Implementation() override;
  virtual void ClearBuildingFocusView_Implementation() override;

private:
  /** 根据水平和垂直输入刷新键盘移动状态。 */
  void UpdateKeyboardMovingState();
  /** 返回可在运行时修改的相机配置文件绝对路径。 */
  FString GetCameraConfigFilePath() const;

  /** 文件发生变化时重新读取速度和分辨率配置。 */
  void ReloadCameraConfig(bool bForceReload = false);

  /** 将 INI 中的新分辨率应用到游戏视口/像素流送后备缓冲区。 */
  void ApplyConfiguredResolution(int32 Width, int32 Height);

  /** 根据当前相机控制模式返回有效缩放下限。 */
  float GetActiveMinZoom() const;

  /** 根据当前相机控制模式返回有效缩放上限。 */
  float GetActiveMaxZoom() const;

  void FocusOnActorInternal(
      AActor *TargetActor,
      float InterpTime,
      float ZoomScale,
      FRotator NewRotation,
      bool bUseActorPivotAsCenter = false);
  // 存储当前聚焦设备的布尔变量
  bool bHasSavedBuildingFocusView = false;
  FVector SavedBuildingFocusLocation = FVector::ZeroVector;
  FRotator SavedBuildingFocusRotation = FRotator::ZeroRotator;
  float SavedBuildingFocusArmLength = 0.f;
  float SavedBuildingFocusInterpTime = 0.8f;

  /** 第一次进入设备聚焦时保存；连续切换设备不会覆盖原拆楼层视角。 */
  bool bHasSavedDeviceFocusView = false;
  FVector SavedDeviceFocusLocation = FVector::ZeroVector;
  FRotator SavedDeviceFocusRotation = FRotator::ZeroRotator;
  float SavedDeviceFocusArmLength = 0.f;

  bool bHasSavedDeviceDismantleZoom = false;
  float SavedPreDismantleArmLength = 0.f;
  float SavedPreDismantleMinZoom = 0.f;
  float SavedPreDismantleMaxZoom = 0.f;

  /** 进入拆楼聚焦前使用的相机控制模式。 */
  ECameraControlMode SavedBuildingFocusControlMode = ECameraControlMode::BirdsEye;
  /** 运行时配置轮询累计时间。 */
  float CameraConfigPollElapsed = 0.f;

  /** 上次成功检测到的 CameraConfig.ini 文件时间。 */
  FDateTime LastCameraConfigTimestamp = FDateTime::MinValue();

  /** 防止配置文件缺失时每次轮询都刷警告。 */
  bool bCameraConfigMissingLogged = false;

  /** 上次已应用的分辨率，避免重复触发视口重建。 */
  FIntPoint LastAppliedConfigResolution = FIntPoint::ZeroValue;

  /** 第一次进入FreeMove前保存的机械臂长度。 */
  bool bHasSavedPreFreeMoveArmLength = false;

  float SavedPreFreeMoveArmLength = 0.f;
  /**
   * 是否已经保存进入FreeMove前的完整视角。
   *
   * 重复设置FreeMove、楼栋聚焦和楼层聚焦时，
   * 不能覆盖第一次进入FreeMove前的记录。
   */
  bool bHasSavedPreFreeMoveView = false;

  /** 进入FreeMove前Pawn的完整世界Transform。 */
  FTransform SavedPreFreeMoveTransform =
      FTransform::Identity;

  /** 进入FreeMove前的Controller Rotation。 */
  FRotator SavedPreFreeMoveControlRotation =
      FRotator::ZeroRotator;

  /** 进入FreeMove前是否启用Camera Lag。 */
  bool bSavedPreFreeMoveCameraLag = true;

  /** 进入FreeMove前是否启用Camera Rotation Lag。 */
  bool bSavedPreFreeMoveCameraRotationLag = true;

  /**
   * 保存原鸟瞰视角，
   * 将Pawn移动到当前Camera位置并收起机械臂。
   */
  void CollapseSpringArmForFreeMove();

  /**
   * 切回BirdsEye时恢复进入FreeMove前的完整视角。
   */
  void RestoreSpringArmAfterFreeMove();
};

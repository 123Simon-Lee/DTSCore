#include "Core/Game/TwinSpectatorPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "InputModifiers.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/GameUserSettings.h"
#include <Kismet/KismetMathLibrary.h>
#include "Core/FunctionLibrary/UDSTCoreFunctionLibrary.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

ATwinSpectatorPawn::ATwinSpectatorPawn()
{
  PrimaryActorTick.bCanEverTick = true;

  Root = CreateDefaultSubobject<USceneComponent>("Root");
  SetRootComponent(Root);
  bUseControllerRotationYaw = true;
  bUseControllerRotationPitch = true;
  bUseControllerRotationRoll = false;

  SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
  SpringArm->SetupAttachment(Root);
  SpringArm->TargetArmLength = 6000.f;
  SpringArm->bDoCollisionTest = false;
  SpringArm->bEnableCameraLag = true;
  SpringArm->bEnableCameraRotationLag = true;
  SpringArm->CameraRotationLagSpeed = 10.f;
  SpringArm->bUsePawnControlRotation = true;

  Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
  Camera->SetupAttachment(SpringArm);
  Camera->bUsePawnControlRotation = false;
}

FString ATwinSpectatorPawn::GetCameraConfigFilePath() const
{
  return FPaths::ConvertRelativePathToFull(
      FPaths::Combine(
          FPaths::ProjectContentDir(),
          TEXT("Softwareconfig.ini")));
}

void ATwinSpectatorPawn::ReloadCameraConfig(const bool bForceReload)
{
  const FString ConfigPath = GetCameraConfigFilePath();
  IFileManager &FileManager = IFileManager::Get();

  if (!FileManager.FileExists(*ConfigPath))
  {
    if (!bCameraConfigMissingLogged)
    {
      UE_LOG(
          LogTemp,
          Warning,
          TEXT("[Softwareconfig] 配置文件不存在: %s"),
          *ConfigPath);
      bCameraConfigMissingLogged = true;
    }
    return;
  }

  bCameraConfigMissingLogged = false;
  const FDateTime CurrentTimestamp = FileManager.GetTimeStamp(*ConfigPath);
  if (!bForceReload && CurrentTimestamp == LastCameraConfigTimestamp)
  {
    return;
  }

  FConfigFile RuntimeConfig;
  RuntimeConfig.Read(ConfigPath);
  if (RuntimeConfig.Num() == 0)
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[CameraConfig] 配置文件为空或解析失败: %s"),
        *ConfigPath);
    return;
  }

  // 只有成功读到配置后才记录时间；解析失败时下次轮询仍会重试。
  LastCameraConfigTimestamp = CurrentTimestamp;

  // 兼容项目现有的 CameraSetttings（三个 t）以及标准 CameraSettings 拼写。
  const auto GetCameraFloat =
      [&RuntimeConfig](const TCHAR *Key, float &OutValue)
  {
    return RuntimeConfig.GetFloat(
               TEXT("CameraSetttings"),
               Key,
               OutValue) ||
           RuntimeConfig.GetFloat(
               TEXT("CameraSettings"),
               Key,
               OutValue);
  };

  bool bSpeedChanged = false;
  float NewValue = 0.f;

  if (GetCameraFloat(TEXT("ZoomSpeed"), NewValue) &&
      FMath::IsFinite(NewValue) &&
      NewValue >= 0.f &&
      !FMath::IsNearlyEqual(ZoomSpeed, NewValue))
  {
    ZoomSpeed = NewValue;
    ZoomVelocity = 0.f;
    bSpeedChanged = true;
  }

  // MoveSpeed 允许为负数：当前鸟瞰平移方向依靠其符号保持原有手感。
  if (GetCameraFloat(TEXT("MoveSpeed"), NewValue) &&
      FMath::IsFinite(NewValue) &&
      !FMath::IsNearlyEqual(MoveSpeed, NewValue))
  {
    MoveSpeed = NewValue;
    bSpeedChanged = true;
  }

  if (GetCameraFloat(TEXT("RotateSpeed"), NewValue) &&
      FMath::IsFinite(NewValue) &&
      NewValue >= 0.f &&
      !FMath::IsNearlyEqual(RotateSpeed, NewValue))
  {
    RotateSpeed = NewValue;
    bSpeedChanged = true;
  }

  if (GetCameraFloat(TEXT("PollInterval"), NewValue) &&
      FMath::IsFinite(NewValue) &&
      NewValue >= 0.1f)
  {
    CameraConfigPollInterval = NewValue;
  }

  int32 ConfigWidth = 0;
  int32 ConfigHeight = 0;
  const bool bHasWidth = RuntimeConfig.GetInt(
      TEXT("Resolution"),
      TEXT("Width"),
      ConfigWidth);
  const bool bHasHeight = RuntimeConfig.GetInt(
      TEXT("Resolution"),
      TEXT("Height"),
      ConfigHeight);

  if (bHasWidth && bHasHeight &&
      ConfigWidth >= 320 && ConfigHeight >= 200 &&
      ConfigWidth <= 16384 && ConfigHeight <= 16384)
  {
    ApplyConfiguredResolution(ConfigWidth, ConfigHeight);
  }
  else if (bHasWidth || bHasHeight)
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Softwareconfig] 分辨率无效，Width=%d Height=%d"),
        ConfigWidth,
        ConfigHeight);
  }

  UE_LOG(
      LogTemp,
      Log,
      TEXT("[Softwareconfig] 已加载: Zoom=%.3f Move=%.3f Rotate=%.3f Resolution=%dx%d%s"),
      ZoomSpeed,
      MoveSpeed,
      RotateSpeed,
      ConfigWidth,
      ConfigHeight,
      bSpeedChanged ? TEXT(" [SpeedChanged]") : TEXT(""));
}

void ATwinSpectatorPawn::ApplyConfiguredResolution(
    const int32 Width,
    const int32 Height)
{
  const FIntPoint RequestedResolution(Width, Height);
  if (RequestedResolution == LastAppliedConfigResolution)
  {
    return;
  }

  if (!GEngine)
  {
    return;
  }

  UGameUserSettings *UserSettings = GEngine->GetGameUserSettings();
  if (!UserSettings)
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[Softwareconfig] GameUserSettings 无效，无法应用分辨率 %dx%d"),
        Width,
        Height);
    return;
  }

  UserSettings->SetScreenResolution(RequestedResolution);
  UserSettings->ApplyResolutionSettings(false);
  LastAppliedConfigResolution = RequestedResolution;

  UE_LOG(
      LogTemp,
      Log,
      TEXT("[Softwareconfig] 已请求切换分辨率: %dx%d"),
      Width,
      Height);
}

void ATwinSpectatorPawn::BeginPlay()
{
  Super::BeginPlay();

  // 蓝图默认值初始化完成后读取外部配置，保证 INI 拥有最终优先级。
  ReloadCameraConfig(true);

  if (APlayerController *PC = Cast<APlayerController>(GetController()))
  {
    ApplyCameraPitchLimits(PC);

    if (UEnhancedInputLocalPlayerSubsystem *Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
    {
      Subsystem->AddMappingContext(MappingContext, 0);
    }
  }

  SetActorLocation(CameraStartLocation);

  if (APlayerController *PC = GetWorld()->GetFirstPlayerController())
  {
    PC->SetControlRotation(CameraStartRotate);
  }

  if (SpringArm)
  {
    SpringArm->TargetArmLength = SpringStartLength;
  }

  CameraLocation = CameraStartLocation;
  CameraRotate = CameraStartRotate;
  SpringLen = SpringStartLength;

  if (CameraCurve)
  {
    FOnTimelineFloat UpdateFunc;
    UpdateFunc.BindUFunction(this, FName("OnCameraTimelineUpdate"));

    FOnTimelineEvent FinishFunc;
    FinishFunc.BindUFunction(this, FName("OnCameraTimelineFinished"));

    CameraTimeline.AddInterpFloat(CameraCurve, UpdateFunc);
    CameraTimeline.SetTimelineFinishedFunc(FinishFunc);
    CameraTimeline.SetLooping(false);
  }
}

void ATwinSpectatorPawn::Tick(float DeltaTime)
{
  Super::Tick(DeltaTime);

  // 实时配置只轮询时间戳；文件未变化时不会重复解析 INI。
  CameraConfigPollElapsed += DeltaTime;
  if (CameraConfigPollElapsed >= FMath::Max(CameraConfigPollInterval, 0.1f))
  {
    CameraConfigPollElapsed = 0.f;
    ReloadCameraConfig(false);
  }

  CameraTimeline.TickTimeline(DeltaTime);

  /*
   * FreeMove下机械臂是硬性固定为0。
   *
   * 必须放在Timeline更新之后，
   * 防止本帧Timeline重新写入非零机械臂长度。
   */
  if (
      SpringArm &&
      CameraControlMode ==
          ECameraControlMode::FreeMove)
  {
    SpringArm->TargetArmLength = 0.f;

    SpringLen = 0.f;
    TargetArmLength = 0.f;
    ZoomVelocity = 0.f;
  }
  else if (
      SpringArm &&
      !CameraTimeline.IsPlaying())
  {
    ZoomVelocity =
        FMath::FInterpTo(
            ZoomVelocity,
            0.f,
            DeltaTime,
            Damping);

    const float NewLength =
        SpringArm->TargetArmLength -
        ZoomVelocity * DeltaTime;

    const float ClampedLength =
        FMath::Clamp(
            NewLength,
            GetActiveMinZoom(),
            GetActiveMaxZoom());

    SpringArm->TargetArmLength =
        ClampedLength;

    if (!FMath::IsNearlyEqual(
            NewLength,
            ClampedLength))
    {
      ZoomVelocity = 0.f;
    }
  }
  else
  {
    ZoomVelocity = 0.f;
  }

  /*
   * DeviceFocus 和 FreeMove 持续移动。
   *
   * Timeline 播放期间禁止手动移动，
   * 保证拆楼聚焦时 Pawn 可以正确飞到目标位置。
   */
  if (CanUseWASD() && !CameraTimeline.IsPlaying())
  {
    FRotator MoveRotation = GetActorRotation();

    if (const AController *PawnController = GetController())
    {
      MoveRotation = PawnController->GetControlRotation();
    }

    /*
     * WASD 始终是水平移动。
     * 即使镜头朝天或朝地，也不会沿 Pitch 钻入地面。
     */
    MoveRotation.Pitch = 0.f;
    MoveRotation.Roll = 0.f;

    const FVector Forward = FRotationMatrix(MoveRotation).GetUnitAxis(EAxis::X);

    const FVector Right = FRotationMatrix(MoveRotation).GetUnitAxis(EAxis::Y);

    FVector MoveDirection = Forward * WASDMoveInput.X + Right * WASDMoveInput.Y;

    float ActiveMoveSpeed = WASDMoveSpeed;

    if (CameraControlMode == ECameraControlMode::FreeMove)
    {
      // Space/Shift 使用固定世界 Z 轴。
      MoveDirection +=
          FVector::UpVector *
          VerticalMoveInput *
          FreeMoveVerticalSpeedScale;

      ActiveMoveSpeed = FreeMoveSpeed;
    }

    /*
     * 防止同时按 W+D 或 W+Space 时速度变快。
     */
    MoveDirection = MoveDirection.GetClampedToMaxSize(1.f);

    if (!MoveDirection.IsNearlyZero())
    {
      FVector NewLocation = GetActorLocation() + MoveDirection * ActiveMoveSpeed * DeltaTime;

      if (CameraControlMode == ECameraControlMode::FreeMove)
      {
        // FreeMove 手动移动时保持在最低飞行高度以上。
        NewLocation.Z =
            FMath::Max(
                NewLocation.Z,
                FreeMoveMinAltitude);
      }

      SetActorLocation(NewLocation);
    }
  }
}

void ATwinSpectatorPawn::RestoreSpringArmAfterFreeMove()
{
  if (!SpringArm ||
      !Camera ||
      !bHasSavedPreFreeMoveView)
  {
    return;
  }

  CameraTimeline.Stop();

  const FTransform RestoreTransform =
      SavedPreFreeMoveTransform;

  const FRotator RestoreControlRotation =
      SavedPreFreeMoveControlRotation;

  const float RestoreArmLength =
      SavedPreFreeMoveArmLength;

  /*
   * 恢复Pawn完整世界Transform。
   */
  SetActorTransform(
      RestoreTransform,
      false,
      nullptr,
      ETeleportType::TeleportPhysics);

  /*
   * 恢复Controller Rotation。
   * Pawn Transform只包含Actor旋转，
   * Camera Pitch主要保存在Controller中。
   */
  if (AController *PawnController =
          GetController())
  {
    PawnController->SetControlRotation(
        RestoreControlRotation);
  }

  /*
   * 恢复机械臂和Lag配置。
   */
  SpringArm->TargetArmLength =
      RestoreArmLength;

  SpringArm->bEnableCameraLag =
      bSavedPreFreeMoveCameraLag;

  SpringArm->bEnableCameraRotationLag =
      bSavedPreFreeMoveCameraRotationLag;

  /*
   * 同步Timeline内部缓存。
   */
  CameraLocation =
      RestoreTransform.GetLocation();

  CameraRotate =
      RestoreControlRotation;

  TargetLocation =
      RestoreTransform.GetLocation();

  TargetRotation =
      RestoreControlRotation;

  SpringLen =
      RestoreArmLength;

  TargetArmLength =
      RestoreArmLength;

  ZoomVelocity = 0.f;

  UE_LOG(
      LogTemp,
      Log,
      TEXT(
          "[FreeMove] Restore View: "
          "Location=%s ControlRotation=%s Arm=%.2f"),
      *RestoreTransform
           .GetLocation()
           .ToCompactString(),
      *RestoreControlRotation
           .ToCompactString(),
      RestoreArmLength);

  /*
   * 恢复完成后清空。
   * 下次进入FreeMove时重新保存。
   */
  bHasSavedPreFreeMoveView = false;

  SavedPreFreeMoveTransform =
      FTransform::Identity;

  SavedPreFreeMoveControlRotation =
      FRotator::ZeroRotator;

  SavedPreFreeMoveArmLength = 0.f;
}

// ===== 模式切换 =====
void ATwinSpectatorPawn::SetCameraControlMode(
    ECameraControlMode NewMode)
{
  const ECameraControlMode OldMode =
      CameraControlMode;

  /*
   * 重复设置相同模式时不再重新执行进入逻辑。
   *
   * 特别是拆楼、楼层聚焦期间可能重复调用FreeMove，
   * 不能因此停止正在播放的聚焦Timeline。
   */
  if (OldMode == NewMode)
  {
    // 模式未变时也要恢复可能被漫游覆盖的相机限制。
    ApplyCameraPitchLimits(Cast<APlayerController>(GetController()));
    if (
        NewMode ==
            ECameraControlMode::FreeMove &&
        SpringArm)
    {
      // FreeMove的机械臂约束仍然要保证。
      SpringArm->TargetArmLength = 0.f;
      SpringLen = 0.f;
      TargetArmLength = 0.f;
      ZoomVelocity = 0.f;
    }

    return;
  }

  UE_LOG(
      LogTemp,
      Warning,
      TEXT(
          "[CameraMode] %d -> %d, "
          "Pawn=%s, Arm=%.2f"),
      static_cast<int32>(OldMode),
      static_cast<int32>(NewMode),
      *GetActorLocation().ToCompactString(),
      SpringArm
          ? SpringArm->TargetArmLength
          : -1.f);

  /*
   * 先写入新模式。
   * 后面的CameraMoveWithDuration和
   * SetCameraNewLocation才能正确识别FreeMove。
   */
  CameraControlMode = NewMode;

  if (
      NewMode ==
      ECameraControlMode::FreeMove)
  {
    /*
     * 这里只会在真正进入FreeMove时执行一次。
     *
     * 保存原鸟瞰视角，
     * 然后将Pawn放到当前Camera位置并将机械臂设为0。
     */
    CollapseSpringArmForFreeMove();
  }
  else if (
      OldMode ==
          ECameraControlMode::FreeMove &&
      NewMode ==
          ECameraControlMode::BirdsEye &&
      bHasSavedPreFreeMoveView)
  {
    /*
     * 只有真正从FreeMove退出到BirdsEye时，
     * 才允许恢复进入FreeMove之前的位置。
     */
    RestoreSpringArmAfterFreeMove();
  }

  ZoomVelocity = 0.f;

  bKeyboardMoving = false;
  bMouseMoving = false;

  WASDMoveInput = FVector::ZeroVector;
  VerticalMoveInput = 0.f;

  ApplyCameraPitchLimits(Cast<APlayerController>(GetController()));
}

void ATwinSpectatorPawn::ApplyCameraPitchLimits(APlayerController *PC) const
{
  if (!IsValid(PC) || !IsValid(PC->PlayerCameraManager))
  {
    return;
  }

  // Spectator 下仍分鸟瞰、设备聚焦和自由移动，不能统一使用鸟瞰限制。
  // 鸟瞰上限必须 >= 0：楼层/房间聚焦视角是水平的（Pitch=0），
  // 若上限为 -10，视角会被永久夹在 -10，表现为上下旋转锁死。
  PC->PlayerCameraManager->ViewPitchMin = -89.f;
  PC->PlayerCameraManager->ViewPitchMax =
      CameraControlMode == ECameraControlMode::BirdsEye ? 10.f : 89.f;
}
bool ATwinSpectatorPawn::CanUseWASD() const
{
  return CameraControlMode == ECameraControlMode::DeviceFocus ||
         CameraControlMode == ECameraControlMode::FreeMove;
}

bool ATwinSpectatorPawn::CanRightMousePan() const
{
  return CameraControlMode == ECameraControlMode::BirdsEye;
}

bool ATwinSpectatorPawn::CanLeftMouseRotate() const
{
  // 两种模式都支持左键旋转，行为由 Pitch 限制区分
  return true;
}

// ===== 输入绑定 =====
void ATwinSpectatorPawn::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
  Super::SetupPlayerInputComponent(PlayerInputComponent);

  MappingContext = NewObject<UInputMappingContext>(this);

  IA_VerticalMove = NewObject<UInputAction>(this);
  IA_VerticalMove->ValueType = EInputActionValueType::Axis1D;

  IA_LeftMouse = NewObject<UInputAction>(this);
  IA_LeftMouse->ValueType = EInputActionValueType::Boolean;

  IA_RightMouse = NewObject<UInputAction>(this);
  IA_RightMouse->ValueType = EInputActionValueType::Boolean;

  IA_LookAndMouse = NewObject<UInputAction>(this);
  IA_LookAndMouse->ValueType = EInputActionValueType::Axis2D;

  IA_Zoom = NewObject<UInputAction>(this);
  IA_Zoom->ValueType = EInputActionValueType::Axis1D;

  IA_Touch = NewObject<UInputAction>(this);
  IA_Touch->ValueType = EInputActionValueType::Axis2D;
  MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[0]);
  MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[1]);
  MappingContext->MapKey(IA_Touch, EKeys::TouchKeys[2]);

  // WASD
  IA_Move = NewObject<UInputAction>(this);
  IA_Move->ValueType = EInputActionValueType::Axis2D;
  // W 前进 X=+1，布尔键默认值就是 X=1，不需要任何 Modifier
  MappingContext->MapKey(IA_Move, EKeys::W);
  // S 后退 X=-1，只需要 Negate
  {
    FEnhancedActionKeyMapping &M = MappingContext->MapKey(IA_Move, EKeys::S);
    UInputModifierNegate *Negate = NewObject<UInputModifierNegate>();
    M.Modifiers.Add(Negate);
  }
  // D 右移 → 把 X 轴换到 Y 轴：Swizzle YXZ
  {
    FEnhancedActionKeyMapping &M = MappingContext->MapKey(IA_Move, EKeys::D);
    UInputModifierSwizzleAxis *Swizzle = NewObject<UInputModifierSwizzleAxis>();
    Swizzle->Order = EInputAxisSwizzle::YXZ;
    M.Modifiers.Add(Swizzle);
  }
  // A 左移 → 先 Swizzle 换到 Y 轴，再 Negate 取反
  {
    FEnhancedActionKeyMapping &M = MappingContext->MapKey(IA_Move, EKeys::A);
    UInputModifierSwizzleAxis *Swizzle = NewObject<UInputModifierSwizzleAxis>();
    Swizzle->Order = EInputAxisSwizzle::YXZ;
    UInputModifierNegate *Negate = NewObject<UInputModifierNegate>();
    // 注意顺序：先 Swizzle 再 Negate
    M.Modifiers.Add(Swizzle);
    M.Modifiers.Add(Negate);
  }

  // Space：+1，向上。
  MappingContext->MapKey(
      IA_VerticalMove,
      EKeys::SpaceBar);

  // Left Shift：-1，向下。
  {
    FEnhancedActionKeyMapping &Mapping = MappingContext->MapKey(IA_VerticalMove, EKeys::LeftShift);

    UInputModifierNegate *Negate = NewObject<UInputModifierNegate>(this);

    Mapping.Modifiers.Add(Negate);
  }

  // Right Shift：-1，向下。
  {
    FEnhancedActionKeyMapping &Mapping = MappingContext->MapKey(IA_VerticalMove, EKeys::RightShift);

    UInputModifierNegate *Negate = NewObject<UInputModifierNegate>(this);

    Mapping.Modifiers.Add(Negate);
  }

  MappingContext->MapKey(IA_LookAndMouse, EKeys::Mouse2D);
  MappingContext->MapKey(IA_Zoom, EKeys::MouseWheelAxis);
  MappingContext->MapKey(IA_LeftMouse, EKeys::LeftMouseButton);
  MappingContext->MapKey(IA_RightMouse, EKeys::RightMouseButton);

  if (UEnhancedInputComponent *EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
  {
    EIC->BindAction(IA_LookAndMouse, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::MouseMove);
    EIC->BindAction(IA_LookAndMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::MouseMoveStop);

    EIC->BindAction(IA_LookAndMouse, ETriggerEvent::Canceled, this, &ATwinSpectatorPawn::MouseMoveStop);

    EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::Zoom);

    EIC->BindAction(IA_LeftMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnLeftDown);
    EIC->BindAction(IA_LeftMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnLeftUp);
    EIC->BindAction(IA_RightMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnRightDown);
    EIC->BindAction(IA_RightMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnRightUp);

    EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::WASDMove);
    EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::WASDMoveStop);
    EIC->BindAction(IA_Move, ETriggerEvent::Canceled, this, &ATwinSpectatorPawn::WASDMoveStop);

    EIC->BindAction(IA_VerticalMove, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::VerticalMove);

    EIC->BindAction(IA_VerticalMove, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::VerticalMoveStop);

    EIC->BindAction(IA_VerticalMove, ETriggerEvent::Canceled, this, &ATwinSpectatorPawn::VerticalMoveStop);
  }

  PlayerInputComponent->BindTouch(IE_Pressed, this, &ATwinSpectatorPawn::OnTouchPressed);
  PlayerInputComponent->BindTouch(IE_Released, this, &ATwinSpectatorPawn::OnTouchReleased);
  PlayerInputComponent->BindTouch(IE_Repeat, this, &ATwinSpectatorPawn::OnTouchMoved);
}

// ===== 鼠标按键 =====
void ATwinSpectatorPawn::OnLeftDown(const FInputActionValue &)
{
  if (!CanLeftMouseRotate())
    return;
  bLeftMouseDown = true;
}

void ATwinSpectatorPawn::OnLeftUp(const FInputActionValue &)
{
  // 重置状态
  bLeftMouseDown = false;
  bMouseMoving = false;
}

void ATwinSpectatorPawn::OnRightDown(const FInputActionValue &)
{
  if (CameraControlMode == ECameraControlMode::DeviceFocus)
  {
    // 右键与 WBP_MainUI.WBP_DeviceFoucsBlackBtn 共用业务层返回流程。
    // Pawn 只发送请求，场景、拆解、UI 和相机由控制器统一恢复。
    OnDeviceFocusReturnRequested.Broadcast();
    return;
  }

  if (!CanRightMousePan())
    return;
  bRightMouseDown = true;
}

void ATwinSpectatorPawn::OnRightUp(const FInputActionValue &)
{
  bRightMouseDown = false;
  bMouseMoving = false;
}

// ===== 鼠标移动 =====
void ATwinSpectatorPawn::MouseMove(const FInputActionValue &Value)
{
  const FVector2D Axis = Value.Get<FVector2D>();

  const bool bCanMouseControlCamera = (bRightMouseDown && CanRightMousePan()) || (bLeftMouseDown && CanLeftMouseRotate());

  // 只有按住有效鼠标键并且鼠标确实产生位移，才算正在移动
  bMouseMoving = bCanMouseControlCamera && !Axis.IsNearlyZero(InputMoveDeadZone);

  if (!bMouseMoving)
  {
    return;
  }

  // 右键平移：仅鸟瞰模式
  if (bRightMouseDown && CanRightMousePan())
  {
    SetCameraMove(Axis.X, true);
    SetCameraMove(Axis.Y, false);
  }
  // 左键旋转
  else if (bLeftMouseDown && CanLeftMouseRotate())
  {
    SetCameraRotate(Axis.X, true);
    SetCameraRotate(Axis.Y, false);
  }
}

void ATwinSpectatorPawn::MouseMoveStop(const FInputActionValue &Value)
{
  bMouseMoving = false;
}

void ATwinSpectatorPawn::SetCameraMove(float Val, bool bX)
{
  FRotator Rot = GetActorRotation();
  Rot.Pitch = 0.f;
  Rot.Roll = 0.f;

  FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
  FVector Forward = FRotationMatrix(Rot).GetUnitAxis(EAxis::X);
  FVector Dir = bX ? Right : Forward;

  // 平移速度随臂长线性缩放：臂长越短移动越慢，臂长越长移动越快
  float ArmLength = SpringArm ? SpringArm->TargetArmLength : MinZoom;
  float T = FMath::Clamp((ArmLength - MinZoom) / (MaxZoom - MinZoom), 0.f, 1.f);
  float ScaledSpeed = FMath::Lerp(MoveSpeed * MinMoveSpeedScale, MoveSpeed, T);

  SetActorLocation(GetActorLocation() + Dir * Val * ScaledSpeed);
}

void ATwinSpectatorPawn::SetCameraRotate(float Val, bool bX)
{
  if (FMath::IsNearlyZero(Val))
    return;

  if (APlayerController *PC = Cast<APlayerController>(GetController()))
  {
    FRotator Rot = PC->GetControlRotation();

    if (bX)
    {
      Rot.Yaw += Val * RotateSpeed;
    }
    else
    {
      Rot.Pitch += Val * RotateSpeed;

      // 鸟瞰模式手动 Clamp（PlayerCameraManager 也会 Clamp，双保险）
      if (CameraControlMode == ECameraControlMode::BirdsEye)
      {
        Rot.Pitch = FMath::Clamp(Rot.Pitch, -89.f, -10.f);
      }
      else
      {
        Rot.Pitch = FMath::Clamp(Rot.Pitch, -89.f, 89.f);
      }
    }

    PC->SetControlRotation(Rot);
  }
}

// ===== WASD =====
void ATwinSpectatorPawn::WASDMove(const FInputActionValue &Value)
{
  if (!CanUseWASD())
  {
    WASDMoveInput = FVector::ZeroVector;
    UpdateKeyboardMovingState();
    return;
  }

  const FVector2D Axis = Value.Get<FVector2D>();

  // W/S：前后。
  WASDMoveInput.X = Axis.X;

  // A/D：左右。
  WASDMoveInput.Y = Axis.Y;

  UpdateKeyboardMovingState();
}

void ATwinSpectatorPawn::WASDMoveStop(const FInputActionValue &)
{
  WASDMoveInput = FVector::ZeroVector;
  UpdateKeyboardMovingState();
}

void ATwinSpectatorPawn::VerticalMove(
    const FInputActionValue &Value)
{
  if (CameraControlMode != ECameraControlMode::FreeMove)
  {
    VerticalMoveInput = 0.f;
    UpdateKeyboardMovingState();
    return;
  }

  VerticalMoveInput =
      FMath::Clamp(Value.Get<float>(), -1.f, 1.f);

  UpdateKeyboardMovingState();
}

void ATwinSpectatorPawn::VerticalMoveStop(
    const FInputActionValue &)
{
  VerticalMoveInput = 0.f;
  UpdateKeyboardMovingState();
}

void ATwinSpectatorPawn::UpdateKeyboardMovingState()
{
  const bool bHasHorizontalInput = !WASDMoveInput.IsNearlyZero(InputMoveDeadZone);

  const bool bHasVerticalInput =
      CameraControlMode == ECameraControlMode::FreeMove &&
      !FMath::IsNearlyZero(
          VerticalMoveInput,
          InputMoveDeadZone);

  bKeyboardMoving = bHasHorizontalInput || bHasVerticalInput;
}
// ===== 缩放 =====
void ATwinSpectatorPawn::Zoom(
    const FInputActionValue &Value)
{
  if (!SpringArm || CameraTimeline.IsPlaying())
  {
    return;
  }

  const float WheelValue = Value.Get<float>();

  if (FMath::IsNearlyZero(WheelValue))
  {
    return;
  }

  /*
   * FreeMove 下滚轮不控制 SpringArm，
   * 只调整飞行速度。
   */
  if (CameraControlMode ==
      ECameraControlMode::FreeMove)
  {
    ZoomVelocity = 0.f;

    FreeMoveSpeed = FMath::Clamp(
        FreeMoveSpeed +
            WheelValue * FreeMoveSpeedStep,
        FreeMoveMinSpeed,
        FreeMoveMaxSpeed);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[FreeMove] Speed=%.1f"),
        FreeMoveSpeed);

    return;
  }

  const float ActiveZoomSpeed = CameraControlMode == ECameraControlMode::DeviceFocus ? DeviceFocusZoomSpeed : ZoomSpeed;

  ZoomVelocity += WheelValue * ActiveZoomSpeed;
}

float ATwinSpectatorPawn::GetActiveMinZoom() const
{
  if (CameraControlMode == ECameraControlMode::FreeMove)
  {
    return 0.f;
  }

  if (CameraControlMode == ECameraControlMode::DeviceFocus)
  {
    return FMath::Max(DeviceFocusMinZoom, 1.f);
  }

  return FMath::Max(MinZoom, 1.f);
}

float ATwinSpectatorPawn::GetActiveMaxZoom() const
{
  if (CameraControlMode == ECameraControlMode::FreeMove)
  {
    /*
     * FreeMove机械臂始终固定为0。
     * 滚轮只控制飞行速度。
     */
    return 0.f;
  }

  const float ActiveMinZoom = GetActiveMinZoom();

  if (CameraControlMode == ECameraControlMode::DeviceFocus)
  {
    return FMath::Max(DeviceFocusMaxZoom, ActiveMinZoom);
  }

  return FMath::Max(MaxZoom, ActiveMinZoom);
}

// ===== 触控 =====
void ATwinSpectatorPawn::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
  TouchMap.Add(FingerIndex, Location);
}

void ATwinSpectatorPawn::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
  TouchMap.Remove(FingerIndex);
  if (TouchMap.Num() < 2)
    LastPinchDistance = 0.f;
}

void ATwinSpectatorPawn::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
  if (!TouchMap.Contains(FingerIndex))
    return;

  FVector Last = TouchMap[FingerIndex];
  FVector Delta3D = Location - Last;
  TouchMap[FingerIndex] = Location;

  FVector2D Delta(Delta3D.X, Delta3D.Y);
  int32 FingerCount = TouchMap.Num();

  if (FingerCount == 1)
  {
    SetCameraRotate(Delta.X, true);
    SetCameraRotate(Delta.Y, false);
  }
  else if (FingerCount >= 2)
  {
    SetCameraMove(Delta.X, true);
    SetCameraMove(Delta.Y, false);

    auto It = TouchMap.CreateConstIterator();
    FVector P1 = It.Value();
    ++It;
    FVector P2 = It.Value();

    float Dist = FVector::Dist(P1, P2);
    if (LastPinchDistance > 0.f)
    {
      ZoomVelocity += (Dist - LastPinchDistance) * 5.f;
    }
    LastPinchDistance = Dist;
  }
}

// ===== Camera 工具函数 =====
void ATwinSpectatorPawn::GetCameraLocation()
{
  CameraLocation = GetActorLocation();

  if (APlayerController *PC = GetWorld()->GetFirstPlayerController())
  {
    if (PC->GetPawn() == this)
    {
      CameraRotate = PC->GetControlRotation();
    }
    else
    {
      CameraRotate = this->GetViewRotation();
    }
  }

  if (SpringArm)
    SpringLen = SpringArm->TargetArmLength;
}

void ATwinSpectatorPawn::SetCameraNewLocation(
    float Time,
    FVector CameraNewLocation,
    FRotator CameraNewRotate,
    float SpringNewLength)
{
  SetActorLocation(CameraNewLocation);

  // 避免精确 ±90° 导致欧拉角奇异
  FRotator ViewRotation = CameraNewRotate.GetNormalized();
  ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch, -89.f, 89.f);
  ViewRotation.Roll = 0.f;

  // Pawn 只保存水平朝向
  const FRotator PawnRotation(0.f, ViewRotation.Yaw, 0.f);
  SetActorRotation(PawnRotation, ETeleportType::TeleportPhysics);

  if (APlayerController *PC = Cast<APlayerController>(GetController()))
  {
    PC->SetControlRotation(ViewRotation);
  }

  if (SpringArm)
  {
    /*
     * FreeMove模式下机械臂必须始终为0。
     *
     * 即使楼栋聚焦、楼层聚焦或恢复视角的Timeline
     * 传入了非零SpringNewLength，也不能覆盖。
     */
    SpringArm->TargetArmLength = CameraControlMode == ECameraControlMode::FreeMove ? 0.f : SpringNewLength;
  }
}

void ATwinSpectatorPawn::CameraMove(FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength)
{
  const float DefaultDuration = CameraTimeline.GetTimelineLength() > SMALL_NUMBER
                                    ? CameraTimeline.GetTimelineLength()
                                    : 1.f;
  CameraMoveWithDuration(CameraNewLocation, CameraNewRotate, SpringNewLength, DefaultDuration);
}

void ATwinSpectatorPawn::CameraMoveWithDuration(
    FVector CameraNewLocation,
    FRotator CameraNewRotate,
    float SpringNewLength,
    float Duration)
{
  /*
   * FreeMove下Camera与Pawn重合，
   * 所以GetCameraLocation取得的是Pawn当前位置。
   */
  GetCameraLocation();

  TargetLocation = CameraNewLocation;
  TargetRotation = CameraNewRotate;

  TargetArmLength =
      CameraControlMode ==
              ECameraControlMode::FreeMove
          ? 0.f
          : FMath::Clamp(
                SpringNewLength,
                GetActiveMinZoom(),
                GetActiveMaxZoom());

  UE_LOG(
      LogTemp,
      Warning,
      TEXT(
          "[CameraMove] Mode=%d, "
          "From=%s, To=%s, Arm=%.2f"),
      static_cast<int32>(CameraControlMode),
      *CameraLocation.ToCompactString(),
      *TargetLocation.ToCompactString(),
      TargetArmLength);

  if (CameraCurve)
  {
    const float TimelineLength =
        CameraTimeline.GetTimelineLength();

    const float SafeDuration =
        FMath::Max(Duration, 0.01f);

    CameraTimeline.SetPlayRate(
        TimelineLength > SMALL_NUMBER
            ? TimelineLength / SafeDuration
            : 1.f);

    CameraTimeline.PlayFromStart();
  }
  else
  {
    SetCameraNewLocation(
        1.f,
        TargetLocation,
        TargetRotation,
        TargetArmLength);

    OnCameraMoveFinished.Broadcast();
  }
}

void ATwinSpectatorPawn::SetSpringArmLength(
    const float NewArmLength,
    const float InterpTime)
{
  if (!SpringArm)
  {
    return;
  }

  ZoomVelocity = 0.f;
  GetCameraLocation();

  const float ClampedArmLength = FMath::Clamp(
      NewArmLength,
      GetActiveMinZoom(),
      GetActiveMaxZoom());

  if (InterpTime <= KINDA_SMALL_NUMBER)
  {
    CameraTimeline.Stop();
    SpringArm->TargetArmLength = ClampedArmLength;
    SpringLen = ClampedArmLength;
    TargetArmLength = ClampedArmLength;
    return;
  }

  CameraMoveWithDuration(
      CameraLocation,
      CameraRotate,
      ClampedArmLength,
      InterpTime);
}

void ATwinSpectatorPawn::SetDeviceFocusZoomRange(
    const float NewMinZoom,
    const float NewMaxZoom,
    const bool bClampCurrentArm)
{
  DeviceFocusMinZoom = FMath::Max(NewMinZoom, 1.f);
  DeviceFocusMaxZoom = FMath::Max(
      NewMaxZoom,
      DeviceFocusMinZoom);

  if (!bClampCurrentArm ||
      CameraControlMode != ECameraControlMode::DeviceFocus ||
      !SpringArm)
  {
    return;
  }

  ZoomVelocity = 0.f;
  TargetArmLength = FMath::Clamp(
      TargetArmLength,
      DeviceFocusMinZoom,
      DeviceFocusMaxZoom);
  SpringArm->TargetArmLength = FMath::Clamp(
      SpringArm->TargetArmLength,
      DeviceFocusMinZoom,
      DeviceFocusMaxZoom);
  SpringLen = SpringArm->TargetArmLength;
}

void ATwinSpectatorPawn::SetDeviceDismantleViewEnabled(
    const bool bEnabled)
{
  if (bEnabled)
  {
    if (!bHasSavedDeviceDismantleZoom)
    {
      GetCameraLocation();
      SavedPreDismantleArmLength = SpringLen;
      SavedPreDismantleMinZoom = DeviceFocusMinZoom;
      SavedPreDismantleMaxZoom = DeviceFocusMaxZoom;
      bHasSavedDeviceDismantleZoom = true;
    }

    SetDeviceFocusZoomRange(
        DeviceDismantleMinZoom,
        DeviceDismantleMaxZoom,
        false);
    SetSpringArmLength(
        DeviceDismantleArmLength,
        DeviceDismantleZoomInterpTime);
    return;
  }

  if (!bHasSavedDeviceDismantleZoom)
  {
    return;
  }

  const float RestoreArmLength =
      SavedPreDismantleArmLength;
  const float RestoreMinZoom =
      SavedPreDismantleMinZoom;
  const float RestoreMaxZoom =
      SavedPreDismantleMaxZoom;
  bHasSavedDeviceDismantleZoom = false;

  SetDeviceFocusZoomRange(
      RestoreMinZoom,
      RestoreMaxZoom,
      false);
  SetSpringArmLength(
      RestoreArmLength,
      DeviceDismantleZoomInterpTime);
}

void ATwinSpectatorPawn::FocusOnActor(
    AActor *TargetActor,
    float InterpTime,
    float ZoomScale,
    FRotator NewRotation)
{
  SetCameraControlMode(ECameraControlMode::BirdsEye);
  FocusOnActorInternal(TargetActor, InterpTime, ZoomScale, NewRotation);
}

void ATwinSpectatorPawn::FocusOnDevice(AActor *TargetDevice, float InterpTime, float ZoomScale)
{
  if (!IsValid(TargetDevice))
  {
    return;
  }

  if (!bHasSavedDeviceFocusView)
  {
    GetCameraLocation();
    SavedDeviceFocusLocation = CameraLocation;
    SavedDeviceFocusRotation = CameraRotate;
    SavedDeviceFocusArmLength = SpringLen;
    bHasSavedDeviceFocusView = true;
  }

  SetCameraControlMode(ECameraControlMode::DeviceFocus);

  // 当前项目设备聚焦轴使用设备局部空间；具体轴仍由全局配置决定，默认 Y+。
  // 在这里固定为局部空间，避免蓝图中遗留的 World 默认值覆盖新规则。
  DeviceFocusAxisSpace = EDeviceFocusAxisSpace::DeviceLocal;

  const FRotator FocusRotation = GetDeviceFocusRotation(TargetDevice);

  UE_LOG(
      LogTemp,
      Log,
      TEXT("[DeviceFocus] Target=%s Location=%s Rotation=%s"),
      *GetNameSafe(TargetDevice),
      *TargetDevice->GetActorLocation().ToCompactString(),
      *FocusRotation.ToCompactString());

  // 设备蓝图的 Actor 轴心就是业务配置的定位点。设备可能包含很长的杆、
  // 支架等模型，不能再用整套模型的 Bounds 中心，否则镜头会被拉到杆中间。
  FocusOnActorInternal(
      TargetDevice,
      InterpTime,
      ZoomScale,
      FocusRotation,
      true);
}

void ATwinSpectatorPawn::FocusOnDeviceImmediate(
    AActor *TargetDevice,
    float ZoomScale)
{
  // FocusOnActorInternal 会把非正时长识别为立即应用，同时仍复用完整的
  // 设备边界、观察轴、机械臂长度以及返回视角缓存逻辑。
  FocusOnDevice(TargetDevice, 0.f, ZoomScale);
}

void ATwinSpectatorPawn::RestoreDeviceFocusView(float InterpTime)
{
  if (!bHasSavedDeviceFocusView)
  {
    SetCameraControlMode(ECameraControlMode::BirdsEye);
    return;
  }

  const FVector RestoreLocation = SavedDeviceFocusLocation;
  const FRotator RestoreRotation = SavedDeviceFocusRotation;
  const float RestoreArmLength = SavedDeviceFocusArmLength;
  bHasSavedDeviceFocusView = false;

  SetCameraControlMode(ECameraControlMode::BirdsEye);
  CameraMoveWithDuration(
      RestoreLocation,
      RestoreRotation,
      RestoreArmLength,
      FMath::Max(InterpTime, 0.01f));
}

void ATwinSpectatorPawn::DiscardDeviceFocusView()
{
  bHasSavedDeviceFocusView = false;
  CameraTimeline.Stop();
  SetCameraControlMode(ECameraControlMode::BirdsEye);
}

void ATwinSpectatorPawn::FocusOnActorInternal(
    AActor *TargetActor,
    float InterpTime,
    float ZoomScale,
    FRotator NewRotation,
    bool bUseActorPivotAsCenter)
{
  if (!IsValid(TargetActor) || !Camera || !SpringArm)
  {
    return;
  }

  /*
   * 同时兼容：
   * 1. 楼栋 -> 楼层 -> 模型 Actor；
   * 2. 容器 Actor -> StaticMeshActor；
   * 3. 设备 Actor 自身直接持有无碰撞 MeshComponent。
   */
  TArray<AActor *> FocusActors;
  FocusActors.Add(TargetActor);

  TArray<AActor *> AttachedActors;
  TargetActor->GetAttachedActors(AttachedActors, true, true);
  FocusActors.Append(AttachedActors);

  FBox BoundingBox(ForceInit);
  for (AActor *FocusActor : FocusActors)
  {
    if (!IsValid(FocusActor))
    {
      continue;
    }

    TArray<UPrimitiveComponent *> PrimitiveComponents;
    FocusActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

    for (UPrimitiveComponent *Primitive : PrimitiveComponents)
    {
      if (!IsValid(Primitive) ||
          !Primitive->IsRegistered() ||
          Primitive->IsA<UWidgetComponent>())
      {
        continue;
      }

      // 直接读取组件 Bounds，不依赖碰撞是否开启。
      const FVector Origin = Primitive->Bounds.Origin;
      const FVector Extent = Primitive->Bounds.BoxExtent;
      if (Extent.ContainsNaN() ||
          Extent.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
      {
        continue;
      }

      BoundingBox += FBox(Origin - Extent, Origin + Extent);
    }
  }

  if (!BoundingBox.IsValid)
  {
    // 纯空节点或尚未完成渲染注册时，以 Actor 位置建立安全聚焦范围。
    constexpr float SafeFocusExtent = 50.f;
    const FVector Center = TargetActor->GetActorLocation();
    BoundingBox = FBox(
        Center - FVector(SafeFocusExtent),
        Center + FVector(SafeFocusExtent));
  }

  // 普通 Actor 仍按模型 Bounds 居中；设备则以蓝图中明确放置的 Actor
  // 轴心为定位点。Bounds 继续用于计算观察距离，保证 ZoomScale 的语义不变。
  const FVector Center = bUseActorPivotAsCenter
                             ? TargetActor->GetActorLocation()
                             : BoundingBox.GetCenter();

  // Camera 的 FieldOfView 是水平 FOV，同时计算垂直 FOV，避免窄屏裁切。
  const float HalfHorizontalFOV = FMath::DegreesToRadians(
      FMath::Clamp(Camera->FieldOfView, 5.f, 170.f) * 0.5f);
  const float AspectRatio = FMath::Max(Camera->AspectRatio, 0.1f);
  const float HalfVerticalFOV = FMath::Atan(
      FMath::Tan(HalfHorizontalFOV) / AspectRatio);
  const float LimitingHalfFOV = FMath::Min(
      HalfHorizontalFOV,
      HalfVerticalFOV);

  const float Radius = FMath::Max(
      BoundingBox.GetExtent().Size(),
      1.f);
  const float Distance = LimitingHalfFOV > SMALL_NUMBER
                             ? Radius / FMath::Tan(LimitingHalfFOV)
                             : Radius * 2.f;
  const float SafeZoomScale = FMath::Max(ZoomScale, 0.01f);
  const float NewArmLength = FMath::Clamp(
      Distance * SafeZoomScale,
      GetActiveMinZoom(),
      GetActiveMaxZoom());

  if (InterpTime <= KINDA_SMALL_NUMBER)
  {
    // 专用设备过渡由 PlayerController 的 TransitionCamera 负责画面插值；
    // MainPawn 在画面背后直接准备到最终姿态，不能再启动第二条 Timeline。
    CameraTimeline.Stop();
    TargetLocation = Center;
    TargetRotation = NewRotation;
    TargetArmLength = NewArmLength;
    SetCameraNewLocation(
        1.f,
        TargetLocation,
        TargetRotation,
        TargetArmLength);
    OnCameraMoveFinished.Broadcast();
    return;
  }

  CameraMoveWithDuration(Center, NewRotation, NewArmLength, InterpTime);
}

FRotator ATwinSpectatorPawn::GetDeviceFocusRotation(
    AActor *TargetActor) const
{
  FVector FrontAxis = FVector::RightVector;

  switch (DeviceFocusAxis)
  {
  case EDeviceFocusAxis::PositiveX:
    FrontAxis = FVector::ForwardVector;
    break;

  case EDeviceFocusAxis::NegativeX:
    FrontAxis = -FVector::ForwardVector;
    break;

  case EDeviceFocusAxis::PositiveY:
    FrontAxis = FVector::RightVector;
    break;

  case EDeviceFocusAxis::NegativeY:
    FrontAxis = -FVector::RightVector;
    break;

  case EDeviceFocusAxis::PositiveZ:
    FrontAxis = FVector::UpVector;
    break;

  case EDeviceFocusAxis::NegativeZ:
    FrontAxis = -FVector::UpVector;
    break;

  default:
    break;
  }

  if (DeviceFocusAxisSpace == EDeviceFocusAxisSpace::DeviceLocal &&
      IsValid(TargetActor))
  {
    // 把设备局部正面轴转换成世界方向，兼容不同世界旋转的设备。
    FrontAxis = TargetActor->GetActorTransform()
                    .TransformVectorNoScale(FrontAxis);
  }

  FrontAxis = FrontAxis.GetSafeNormal();
  if (FrontAxis.IsNearlyZero())
  {
    FrontAxis = FVector::RightVector;
  }

  /*
   * 设备 Actor 的局部 Y+ 直接作为 Pawn Forward。
   * 例如设备世界 Yaw=45°，局部 Y+ 转到世界空间后为 Yaw=135°。
   */
  FRotator Result = FrontAxis.Rotation();
  Result.Roll = 0.f;

  return Result;
}

bool ATwinSpectatorPawn::IsCameraMoveRunning() const
{
  return CameraTimeline.IsPlaying();
}

void ATwinSpectatorPawn::ResetCameraLocation()
{
  SetCameraControlMode(ECameraControlMode::BirdsEye);
  CameraMove(CameraStartLocation, CameraStartRotate, SpringStartLength);
}

float ATwinSpectatorPawn::GetFOV()
{
  return Camera ? Camera->FieldOfView : 0.f;
}

// 聚焦楼栋的时候
void ATwinSpectatorPawn::FocusBuildingTarget_Implementation(const FBuildingFocusTarget &Target, const FBuildingFocusSettings &Settings)
{
  if (!Target.bIsValid || !Camera || !SpringArm)
  {
    return;
  }
  const bool bFocusWithFreeMove =
      CameraControlMode ==
      ECameraControlMode::FreeMove;

  /*
   * 已经是FreeMove时保持当前模式，
   * 不要再次进入FreeMove，否则可能停止当前聚焦动画。
   */
  if (!bFocusWithFreeMove)
  {
    SetCameraControlMode(
        ECameraControlMode::BirdsEye);
  }

  FVector LongAxis = Target.LongAxis.GetSafeNormal2D();
  FVector ShortAxis = Target.ShortAxis.GetSafeNormal2D();
  if (LongAxis.IsNearlyZero())
  {
    LongAxis = FVector::ForwardVector;
  }
  if (ShortAxis.IsNearlyZero())
  {
    ShortAxis = FVector::RightVector;
  }

  const FVector CurrentCameraLocation = Camera->GetComponentLocation();
  const FVector ToCurrentCamera = (CurrentCameraLocation - Target.Center).GetSafeNormal2D();

  // 保持在离当前镜头较近的一侧，切换楼栋时不绕场飞行。
  if (FVector::DotProduct(ShortAxis, ToCurrentCamera) < 0.f)
  {
    ShortAxis *= -1.f;
  }
  if (FVector::DotProduct(LongAxis, ToCurrentCamera) < 0.f)
  {
    LongAxis *= -1.f;
  }

  // 相机主要沿短轴观察长立面，同时加入少量长轴偏移显示纵深。
  const FVector FromCenter = (ShortAxis +
                              LongAxis * FMath::Clamp(Settings.DiagonalViewRatio, 0.f, 1.f))
                                 .GetSafeNormal2D();
  const FVector HorizontalLookDirection = -FromCenter;
  const float FocusYaw = Settings.bUseAutoYaw
                             ? HorizontalLookDirection.Rotation().Yaw
                             : Settings.Yaw;
  const FRotator FocusRotation(
      FMath::Clamp(Settings.Pitch, -89.f, 0.f),
      FocusYaw,
      FMath::Clamp(Settings.Roll, -180.f, 180.f));

  const FVector ViewForward = FocusRotation.Vector().GetSafeNormal();
  const FVector ViewRight = FRotationMatrix(FocusRotation)
                                .GetUnitAxis(EAxis::Y);
  const FVector ViewUp = FRotationMatrix(FocusRotation)
                             .GetUnitAxis(EAxis::Z);

  float HalfWidth = 0.f;
  float HalfHeight = 0.f;
  float HalfDepth = 0.f;
  for (int32 X = -1; X <= 1; X += 2)
  {
    for (int32 Y = -1; Y <= 1; Y += 2)
    {
      for (int32 Z = -1; Z <= 1; Z += 2)
      {
        const FVector RelativeCorner(
            Target.Extent.X * static_cast<float>(X),
            Target.Extent.Y * static_cast<float>(Y),
            Target.Extent.Z * static_cast<float>(Z));
        HalfWidth = FMath::Max(
            HalfWidth,
            FMath::Abs(FVector::DotProduct(RelativeCorner, ViewRight)));
        HalfHeight = FMath::Max(
            HalfHeight,
            FMath::Abs(FVector::DotProduct(RelativeCorner, ViewUp)));
        HalfDepth = FMath::Max(
            HalfDepth,
            FMath::Abs(FVector::DotProduct(RelativeCorner, ViewForward)));
      }
    }
  }

  const float HalfHorizontalFov = FMath::DegreesToRadians(
      FMath::Clamp(Camera->FieldOfView, 5.f, 170.f) * 0.5f);
  const float AspectRatio = FMath::Max(Camera->AspectRatio, 0.1f);
  const float HalfVerticalFov = FMath::Atan(
      FMath::Tan(HalfHorizontalFov) / AspectRatio);
  const float Padding = FMath::Max(Settings.Padding, 1.f);

  const float HorizontalDistance = HalfHorizontalFov > SMALL_NUMBER
                                       ? (HalfWidth * Padding) / FMath::Tan(HalfHorizontalFov)
                                       : HalfWidth * Padding * 2.f;
  const float VerticalDistance = HalfVerticalFov > SMALL_NUMBER
                                     ? (HalfHeight * Padding) / FMath::Tan(HalfVerticalFov)
                                     : HalfHeight * Padding * 2.f;

  const float ConfigMinDistance = FMath::Max(Settings.MinDistance, MinZoom);
  const float ConfigMaxDistance = Settings.MaxDistance > 0.f
                                      ? FMath::Min(Settings.MaxDistance, MaxZoom)
                                      : MaxZoom;
  const float FocusDistance = FMath::Clamp(
      FMath::Max(HorizontalDistance, VerticalDistance) + HalfDepth,
      ConfigMinDistance,
      FMath::Max(ConfigMaxDistance, ConfigMinDistance));

  // 配置非零坐标时使用配置点；FreeMove 以此为锚点计算最终位置。
  // 未配置坐标时，仍根据楼栋 Bounds 自动计算机位。
  const bool bHasConfiguredCameraLocation =
      !Settings.CameraLocation.IsNearlyZero();

  FVector FinalPawnLocation;

  if (bFocusWithFreeMove && bHasConfiguredCameraLocation)
  {
    /*
     * FreeMove 下机械臂为 0，Camera 与 Pawn 重合。
     * 以配置的 CameraLocation 为锚点，沿配置旋转的单位方向
     * 回退 MinDistance，得到 Pawn（也是相机）的最终世界位置。
     */
    const FRotator FreeConfigRotation(
        FMath::Clamp(Settings.Pitch, -89.f, 0.f),
        FocusYaw, // 包含自动 Yaw 的计算结果。
        0.f);     // 计算回退方向时 Roll 固定为 0。
    const FVector RotationUnitVector =
        FreeConfigRotation.Vector().GetSafeNormal();
    FinalPawnLocation =
        Settings.CameraLocation -
        RotationUnitVector * Settings.MinDistance;
  }
  else if (bHasConfiguredCameraLocation)
  {
    // 非 FreeMove 模式仍直接使用配置的 Pawn 绝对世界坐标。
    FinalPawnLocation =
        Settings.CameraLocation;
  }
  else if (bFocusWithFreeMove)
  {
    /*
     * 没有配置CameraLocation时，
     * 根据楼栋Bounds自动计算真正的相机位置。
     */
    FinalPawnLocation =
        Target.Center -
        ViewForward * FocusDistance;
  }
  else
  {
    /*
     * 普通鸟瞰模式：
     * Pawn在楼栋中心，Camera由机械臂向后拉开。
     */
    FinalPawnLocation =
        Target.Center;
  }

  const float FinalArmLength =
      bFocusWithFreeMove
          ? 0.f
          : FocusDistance;

  SavedBuildingFocusInterpTime =
      FMath::Max(
          Settings.InterpTime,
          0.01f);

  CameraMoveWithDuration(
      FinalPawnLocation,
      FocusRotation,
      FinalArmLength,
      SavedBuildingFocusInterpTime);
}

void ATwinSpectatorPawn::
    RestoreBuildingFocusView_Implementation()
{
  if (!bHasSavedBuildingFocusView)
  {
    return;
  }

  /*
   * FreeMove下退出楼栋或拆楼时，
   * 不恢复楼栋聚焦前的位置。
   *
   * 否则即使机械臂为0，
   * Pawn仍会被CameraTimeline移动回旧位置，
   * 表现为视角被拉回。
   */
  if (
      CameraControlMode ==
      ECameraControlMode::FreeMove)
  {
    CameraTimeline.Stop();

    if (SpringArm)
    {
      SpringArm->TargetArmLength = 0.f;
    }

    const FVector CurrentLocation =
        GetActorLocation();

    FRotator CurrentRotation =
        GetActorRotation();

    if (const AController *PawnController =
            GetController())
    {
      CurrentRotation =
          PawnController->GetControlRotation();
    }

    /*
     * 将Timeline目标重置为当前位置，
     * 防止旧目标以后再次生效。
     */
    CameraLocation = CurrentLocation;
    CameraRotate = CurrentRotation;

    TargetLocation = CurrentLocation;
    TargetRotation = CurrentRotation;

    SpringLen = 0.f;
    TargetArmLength = 0.f;
    ZoomVelocity = 0.f;

    /*
     * 只清除楼栋聚焦记录。
     *
     * 不清除bHasSavedPreFreeMoveView，
     * 退出预案时仍需要恢复最初鸟瞰视角。
     */
    bHasSavedBuildingFocusView = false;

    return;
  }

  const ECameraControlMode RestoreMode =
      SavedBuildingFocusControlMode;

  SetCameraControlMode(RestoreMode);

  CameraMoveWithDuration(
      SavedBuildingFocusLocation,
      SavedBuildingFocusRotation,
      SavedBuildingFocusArmLength,
      SavedBuildingFocusInterpTime);

  bHasSavedBuildingFocusView = false;
}
void ATwinSpectatorPawn::ClearBuildingFocusView_Implementation()
{
  bHasSavedBuildingFocusView = false;
}

void ATwinSpectatorPawn::OnCameraTimelineUpdate(float Value)
{
  FVector NewLocation = UKismetMathLibrary::VLerp(CameraLocation, TargetLocation, Value);
  FRotator NewRotation = UKismetMathLibrary::RLerp(CameraRotate, TargetRotation, Value, true);
  float NewArmLength = UKismetMathLibrary::Lerp(SpringLen, TargetArmLength, Value);
  SetCameraNewLocation(Value, NewLocation, NewRotation, NewArmLength);
}

void ATwinSpectatorPawn::OnCameraTimelineFinished()
{
  // 无论曲线末端值是否精确为 1，都把最终位置、旋转和臂长落到目标值。
  SetCameraNewLocation(
      1.f,
      TargetLocation,
      TargetRotation,
      TargetArmLength);

  if (CameraControlMode == ECameraControlMode::DeviceFocus)
  {
    const AController *PawnController = GetController();
    const FRotator ControlRotation = PawnController
                                         ? PawnController->GetControlRotation()
                                         : FRotator::ZeroRotator;

    // UE_LOG(
    //     LogTemp,
    //     Log,
    //     TEXT("[DeviceFocus] Finished PawnLocation=%s PawnRotation=%s ControlRotation=%s"),
    //     *GetActorLocation().ToCompactString(),
    //     *GetActorRotation().ToCompactString(),
    //     *ControlRotation.ToCompactString());
  }

  OnCameraMoveFinished.Broadcast();
}

void ATwinSpectatorPawn::SaveBuildingFocusView_Implementation()
{
  if (bHasSavedBuildingFocusView)
  {
    return;
  }

  GetCameraLocation();

  SavedBuildingFocusLocation = CameraLocation;

  SavedBuildingFocusRotation = CameraRotate;

  SavedBuildingFocusArmLength = SpringLen;

  SavedBuildingFocusControlMode = CameraControlMode;

  bHasSavedBuildingFocusView = true;
}

void ATwinSpectatorPawn::CollapseSpringArmForFreeMove()
{
  if (!SpringArm || !Camera)
  {
    return;
  }

  CameraTimeline.Stop();

  /*
   * 只在第一次进入FreeMove时，
   * 保存进入前的Pawn、Controller和机械臂状态。
   */
  if (!bHasSavedPreFreeMoveView)
  {
    SavedPreFreeMoveTransform =
        GetActorTransform();

    SavedPreFreeMoveArmLength =
        SpringArm->TargetArmLength;

    if (const AController *PawnController =
            GetController())
    {
      SavedPreFreeMoveControlRotation =
          PawnController->GetControlRotation();
    }
    else
    {
      SavedPreFreeMoveControlRotation =
          Camera->GetComponentRotation();
    }

    bSavedPreFreeMoveCameraLag =
        SpringArm->bEnableCameraLag;

    bSavedPreFreeMoveCameraRotationLag =
        SpringArm->bEnableCameraRotationLag;

    bHasSavedPreFreeMoveView = true;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT(
            "[FreeMove] Save View: "
            "Pawn=%s Camera=%s Arm=%.2f"),
        *GetActorLocation().ToCompactString(),
        *Camera->GetComponentLocation().ToCompactString(),
        SavedPreFreeMoveArmLength);
  }

  /*
   * FreeMove关闭机械臂Lag。
   *
   * 否则TargetArmLength虽然已经是0，
   * Camera仍可能继续使用之前缓存的滞后位置。
   */
  SpringArm->bEnableCameraLag = false;
  // 旋转平滑保留，使得视角旋转平滑
  SpringArm->bEnableCameraRotationLag = true;

  /*
   * 关键：
   *
   * 不要调用：
   * SetActorLocation(Camera->GetComponentLocation());
   *
   * Pawn保持原位置，
   * Camera才会随着机械臂归零移动到Pawn根节点。
   */
  SpringArm->TargetArmLength = 0.f;

  const FVector CurrentPawnLocation =
      GetActorLocation();

  FRotator CurrentViewRotation =
      Camera->GetComponentRotation();

  if (const AController *PawnController =
          GetController())
  {
    CurrentViewRotation =
        PawnController->GetControlRotation();
  }

  /*
   * 机械臂归零后Camera与Pawn重合，
   * 所以相机移动缓存必须使用Pawn位置，
   * 不能继续使用归零前的Camera位置。
   */
  CameraLocation = CurrentPawnLocation;
  CameraRotate = CurrentViewRotation;

  TargetLocation = CurrentPawnLocation;
  TargetRotation = CurrentViewRotation;

  SpringLen = 0.f;
  TargetArmLength = 0.f;
  ZoomVelocity = 0.f;

  UE_LOG(
      LogTemp,
      Warning,
      TEXT(
          "[FreeMove] Arm collapsed: "
          "Pawn=%s Arm=%.2f"),
      *CurrentPawnLocation.ToCompactString(),
      SpringArm->TargetArmLength);
}

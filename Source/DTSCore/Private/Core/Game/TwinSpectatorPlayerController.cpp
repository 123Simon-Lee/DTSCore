#include "Core/Game/TwinSpectatorPlayerController.h"
#include "Kismet/KismetMathLibrary.h"
#include "Core/Game/TwinSpectatorPawn.h"
#include "EngineUtils.h"
#include "Core/Game/UniversalCharacter.h"
#include "EnhancedInputSubsystems.h"

ATwinSpectatorPlayerController::ATwinSpectatorPlayerController()
{
    bShowMouseCursor = true;

    bEnableClickEvents = true;      // 点击事件
    bEnableMouseOverEvents = true;  // 悬停事件
    bEnableTouchEvents = true;      // 触摸
    bEnableTouchOverEvents = true;  // 触摸悬停

    DefaultClickTraceChannel = ECC_GameTraceChannel1;

    CurrentClickTraceChannel = ECC_GameTraceChannel1;
}

void ATwinSpectatorPlayerController::BeginPlay()
{
    Super::BeginPlay();

    TwinSpectatorPawn = GetPawn();

    for (TActorIterator<AUniversalCharacter> It(GetWorld()); It; ++It)
    {
        CharacterPawn = *It;
        break;
    }

    // ⭐ 创建过渡相机
    TransitionCamera = GetWorld()->SpawnActor<ACameraActor>();
    OnControllerReady.Broadcast();
}

void ATwinSpectatorPlayerController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UpdateBlend(DeltaTime);
}

void ATwinSpectatorPlayerController::SwitchToSpectator()
{
    if (!TwinSpectatorPawn) return;
    
    CurrentViewMode = EViewMode::Spectator;
    StartBlend(TwinSpectatorPawn);
}

void ATwinSpectatorPlayerController::SwitchToCharacter()
{
    if (!CharacterPawn) return;
    
    CurrentViewMode = EViewMode::Roaming;
    StartBlend(CharacterPawn);
}

//////////////////////////////////////////////////////////
//入口（支持打断 + 丝滑连续）
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::SwitchViewMode(EViewMode NewMode)
{
    // 必须当前模式不为同一个才允许切换
    if (!GetWorld()) return;
    if (CurrentViewMode == NewMode) return;

  

    if (NewMode == EViewMode::Spectator)
    {
        SwitchToSpectator();
        // 鸟瞰：Pitch 限制 -89 ~ 0
        this->PlayerCameraManager->ViewPitchMin = -89.f;
        this->PlayerCameraManager->ViewPitchMax = 0.f;
    }
    else
    {
        SwitchToCharacter();
        // 第一人称：Pitch 放开 -89 ~ 89
        this->PlayerCameraManager->ViewPitchMin = -89.f;
        this->PlayerCameraManager->ViewPitchMax = 89.f;
    }
}

bool ATwinSpectatorPlayerController::BeginDeviceFocusTransition(
    AActor* TargetDevice,
    float TransitionDuration,
    float ZoomScale)
{
    ATwinSpectatorPawn* TargetSpectatorPawn =
        Cast<ATwinSpectatorPawn>(TwinSpectatorPawn);
    if (!IsValid(TargetDevice) ||
        !IsValid(TargetSpectatorPawn) ||
        !TransitionCamera)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[DeviceFocus] BeginDeviceFocusTransition failed: Target=%s Pawn=%s TransitionCamera=%s"),
            *GetNameSafe(TargetDevice),
            *GetNameSafe(TargetSpectatorPawn),
            *GetNameSafe(TransitionCamera));
        return false;
    }

    /*
     * StartBlend 必须在 Possess 前执行，它会从当前 Character 画面读取
     * GetPlayerViewPoint 并交给 TransitionCamera。之后即使 Possess 改变了默认
     * ViewTarget，画面也会被重新锁回 TransitionCamera。
     */
    CurrentViewMode = EViewMode::Spectator;
    StartBlend(TargetSpectatorPawn, TransitionDuration);

    Possess(TargetSpectatorPawn);
    SetViewTarget(TransitionCamera);

    /*
     * 此时 SpectatorPawn 已经拥有 Controller，可以正确写入 ControlRotation。
     * Pawn 自己立即到达最终设备姿态；屏幕上唯一可见的移动由 UpdateBlend 中的
     * TransitionCamera 完成，不会显示 Pawn 原来所在的远处位置。
     */
    TargetSpectatorPawn->FocusOnDeviceImmediate(TargetDevice, ZoomScale);

    // StartBlend 运行时 MainPawn 仍在旧姿态；现在目标已经被替换为设备最终姿态，
    // 因此不能沿用基于旧目标算出的长弧选择，设备聚焦统一走最终目标短弧。
    bUseLongRotationPath = false;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[DeviceFocus] Single transition started: Target=%s Duration=%.2f"),
        *GetNameSafe(TargetDevice),
        ActiveBlendDuration);
    return true;
}

//////////////////////////////////////////////////////////
//开始插值
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::StartBlend(
    APawn* NewPawn,
    float DurationOverride)
{
    if (!NewPawn || !TransitionCamera) return;

    // 只有“正在 Blend 时又收到新请求”才需要判断方向连续性。
    // LastBlendAngularDirection 是上一帧相机实际转动的世界空间方向：
    // 新路径优先延续该方向，避免无条件选择短弧后在屏幕上突然掉头。
    // 非打断场景没有可靠的历史方向，后面会保持默认短弧。
    const bool bWasBlending = bBlending;
    const FVector PreviousAngularDirection = LastBlendAngularDirection;

    FVector CamLoc;
    FRotator CamRot;
    GetPlayerViewPoint(CamLoc, CamRot);

    //完全同步当前画面
    TransitionCamera->SetActorLocation(CamLoc);
    TransitionCamera->SetActorRotation(CamRot);

    if (PlayerCameraManager)
    {

        UCameraComponent* CamComp = TransitionCamera->GetCameraComponent();

        CamComp->SetFieldOfView(PlayerCameraManager->GetFOVAngle());
        CamComp->PostProcessSettings =
            PlayerCameraManager->ViewTarget.POV.PostProcessSettings;
        CamComp->PostProcessBlendWeight = 1.f;
        CamComp->bConstrainAspectRatio = false;
    }

    BlendStartLoc = CamLoc;
    BlendStartRot = CamRot;

    ActiveBlendDuration = DurationOverride >= 0.f
        ? FMath::Max(DurationOverride, 0.01f)
        : FMath::Max(BlendDuration, 0.01f);

    BlendTargetPawn = NewPawn;

    // 每段 Blend 只在开始时选择一次长/短弧，之后锁定，避免目标姿态
    // 经过四元数半球边界时逐帧改选路径而产生抖动。
    bUseLongRotationPath = false;
    if (bWasBlending && !PreviousAngularDirection.IsNearlyZero())
    {
        FRotator InitialTargetRot;
        if (UCameraComponent* TargetCamera = NewPawn->FindComponentByClass<UCameraComponent>())
        {
            InitialTargetRot = TargetCamera->GetComponentRotation();
        }
        else
        {
            InitialTargetRot = NewPawn->GetActorRotation();
        }

        const FQuat StartQuat = CamRot.Quaternion();
        FQuat ShortTargetQuat = InitialTargetRot.Quaternion();

        // 四元数 Q 和 -Q 表示同一姿态。与起点点积为负说明二者位于
        // 四元数球的不同半球；将目标取反后，Start -> Target 表示短弧。
        if ((StartQuat | ShortTargetQuat) < 0.f)
        {
            ShortTargetQuat = -ShortTargetQuat;
        }

        // ShortDelta = Target * Start^-1，表示把当前世界姿态旋转到目标
        // 世界姿态所需的增量。W >= 0 将它规范为不超过 180° 的主旋转，
        // 因而下面提取到的是“候选短弧”的旋转轴和角度。
        FQuat ShortDelta = ShortTargetQuat * StartQuat.Inverse();
        if (ShortDelta.W < 0.f)
        {
            ShortDelta = -ShortDelta;
        }
        ShortDelta.Normalize();

        // RotationVector = Axis * Angle。这里只比较方向，所以归一化后
        // 与上一帧实际旋转方向做点积：
        //   接近 +1：方向一致，短弧不会掉头；
        //   接近  0：方向近似垂直，没有明确反向，仍优先短弧；
        //   接近 -1：方向相反，短弧会造成明显掉头。
        // 同一终点的长弧会沿相反旋转轴走 360° - ShortAngle，因此当
        // 短弧方向反向时，选择长弧可以延续上一帧的视觉旋转方向。
        const FVector ShortRotationVector = ShortDelta.ToRotationVector();
        if (!ShortRotationVector.IsNearlyZero())
        {
            const float DirectionDot = FVector::DotProduct(
                ShortRotationVector.GetSafeNormal(),
                PreviousAngularDirection.GetSafeNormal());
            // 使用 -0.1 而不是 0，过滤浮点误差以及接近垂直时的轻微误差
            // 负值；只有反向趋势足够明确才选择长弧
            bUseLongRotationPath = DirectionDot < -0.1f;
        }
        // 短弧角度接近 0 时没有可靠旋转轴，不改成长弧，避免为几乎相同的起止姿态额外旋转一整圈

    }

    if (!bWasBlending)
    {
        LastBlendAngularDirection = FVector::ZeroVector;
    }

    BlendTime = 0.f;
    bBlending = true;
    // 先设置值，再开始绑定，不然你的相机飞到一半会被赋值
    // 画面给到过渡相机  
    SetViewTarget(TransitionCamera);
}

//////////////////////////////////////////////////////////
//Tick插值
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::UpdateBlend(float DeltaTime)
{
    if (!bBlending || !BlendTargetPawn) return;

    // ── 时间推进 ────────────────────────────────────────────
    // BlendTime 从 0 累加到 BlendDuration
    // T 是归一化进度 [0,1]
    // SmoothT 是缓入缓出曲线后的进度，让动画开始和结束更柔和
    BlendTime += DeltaTime;
    float T = FMath::Clamp(BlendTime / ActiveBlendDuration, 0.f, 1.f);
    float SmoothT = FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);

    // ── 获取目标位置和旋转 ──────────────────────────────────
    // 优先取 CameraComponent 的变换，因为 Pawn 的 Actor 位置
    // 不一定等于相机位置（尤其是第一人称 Character）
    FVector TargetLoc;
    FRotator TargetRot;
    if (UCameraComponent* Cam = BlendTargetPawn->FindComponentByClass<UCameraComponent>())
    {
        TargetLoc = Cam->GetComponentLocation();
        TargetRot = Cam->GetComponentRotation();
    }
    else
    {
        TargetLoc = BlendTargetPawn->GetActorLocation();
        TargetRot = BlendTargetPawn->GetActorRotation();
    }

    // ── 位置插值（直接线性，不需要额外处理）────────────────
    FVector NewLoc = FMath::Lerp(BlendStartLoc, TargetLoc, SmoothT);

    // ── 旋转插值准备 ────────────────────────────────────────
    FQuat StartRotQuat = BlendStartRot.Quaternion();
    FQuat TargetRotQuat = TargetRot.Quaternion();

    // Q 和 -Q 表示同一个最终姿态，但在四元数球上对应两条不同路径使用点积来判断
    //   短弧：让 Start·Target >= 0
    //   长弧：让 Start·Target <= 0
    // 这里按照 StartBlend 已锁定的选择调整目标符号
    // SlerpFullPath；普通 Slerp 会自行纠正为短弧，导致长弧选择失效
    const float QuatDot = StartRotQuat | TargetRotQuat;
    if ((!bUseLongRotationPath && QuatDot < 0.f) ||
        (bUseLongRotationPath && QuatDot > 0.f))
    {
        TargetRotQuat = -TargetRotQuat;
    }

    // ── 执行球面插值 ────────────────────────────────────────
    const FQuat PreviousRotQuat = TransitionCamera->GetActorRotation().Quaternion();
    FQuat NewRotQuat = FQuat::SlerpFullPath(StartRotQuat, TargetRotQuat, SmoothT);
    NewRotQuat.Normalize();

    // FrameDelta = New * Previous^-1，得到上一帧画面到当前帧画面的
    // 世界空间实际旋转增量。将 W 规范为非负可消除 Q/-Q 符号跳变
    // 保证记录的是这一帧的小角度真实运动，而不是等价的绕远表示
    FQuat FrameDelta = NewRotQuat * PreviousRotQuat.Inverse();
    if (FrameDelta.W < 0.f)
    {
        FrameDelta = -FrameDelta;
    }
    FrameDelta.Normalize();
    // 只保存旋转轴方向，不保存角速度大小。缓入阶段增量接近 0 时不
    // 覆盖旧值，避免尚未形成可靠方向就丢失上一段 Blend 的方向信息
    const FVector FrameRotationVector = FrameDelta.ToRotationVector();
    if (!FrameRotationVector.IsNearlyZero())
    {
        LastBlendAngularDirection = FrameRotationVector.GetSafeNormal();
    }

    // ── 应用到过渡相机 ──────────────────────────────────────
    FRotator NewRot = NewRotQuat.Rotator();
    TransitionCamera->SetActorLocation(NewLoc);
    TransitionCamera->SetActorRotation(NewRot);

    // ── 插值完成 ────────────────────────────────────────────
    if (T >= 1.f)
    {
        FinishBlend();
    }
}

//////////////////////////////////////////////////////////
//完成切换
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::FinishBlend()
{
    if (!BlendTargetPawn) return;

    APawn* NewPawn = BlendTargetPawn;

    bBlending = false;
    BlendTargetPawn = nullptr;

    // 专用设备聚焦在 Blend 开始时已经 Possess；普通模式切换仍在这里 Possess。
    if (GetPawn() != NewPawn)
    {
        Possess(NewPawn);
    }

    // ⭐ 再切回Pawn（顺序不能反）
    SetViewTarget(NewPawn);

    NewPawn->InputComponent = nullptr;
    NewPawn->EnableInput(this);

    if (ATwinSpectatorPawn* SP = Cast<ATwinSpectatorPawn>(NewPawn))
    {
        SwitchInputContext(SP->MappingContext);
    }
    else if (AUniversalCharacter* CH = Cast<AUniversalCharacter>(NewPawn))
    {
        SwitchInputContext(CH->MappingContext);
    }

    // 必须在 Possess、SetViewTarget 和输入映射全部完成后广播。
    // 设备聚焦依赖 Spectator Pawn 的 Controller，不能在 StartBlend 时提前执行。
    OnViewModeBlendFinished.Broadcast(CurrentViewMode);
}

//////////////////////////////////////////////////////////
//输入切换
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::SwitchInputContext(UInputMappingContext* NewContext)
{
    if (!GetLocalPlayer()) return;

    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        Subsystem->ClearAllMappings();

        if (NewContext)
        {
            Subsystem->AddMappingContext(NewContext, 0);
        }
    }
}

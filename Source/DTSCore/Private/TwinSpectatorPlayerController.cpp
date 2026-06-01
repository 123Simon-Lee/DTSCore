#include "TwinSpectatorPlayerController.h"
#include "Kismet/KismetMathLibrary.h"
#include "TwinSpectatorPawn.h"
#include "EngineUtils.h"
#include "UniversalCharacter.h"
#include "EnhancedInputSubsystems.h"

ATwinSpectatorPlayerController::ATwinSpectatorPlayerController()
{
    bShowMouseCursor = true;

    bEnableClickEvents = true;      // 点击事件
    bEnableMouseOverEvents = true;  // 悬停事件
    bEnableTouchEvents = true;      // 触摸
    bEnableTouchOverEvents = true;  // 触摸悬停

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

    float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastSwitchTime < SwitchCooldown) return;
    LastSwitchTime = Now;

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

//////////////////////////////////////////////////////////
//开始插值
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::StartBlend(APawn* NewPawn)
{
    if (!NewPawn || !TransitionCamera) return;
    
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

    BlendTargetPawn = NewPawn;

    BlendTime = 0.f;
    bBlending = true;

    // 初始化为没有四元数缓存
    bHasRotQuat = false;
    // 重置时间
    this->QuatPathReverseTime = 0.f;
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
    float T = FMath::Clamp(BlendTime / BlendDuration, 0.f, 1.f);
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

    // 四元数有双覆盖性：Q 和 -Q 表示同一个旋转
    // 但 Slerp 会选择点积为正（夹角 < 90°）的那条弧（短弧）
    // 如果点积为负，说明两者在"四元数空间"里在相反半球
    // 此时 Slerp 会走长弧，产生视角绕远路的问题
    // 解决：把 Target 取反，让它与 Start 在同一半球，强制走短弧
    if ((StartRotQuat | TargetRotQuat) < 0.f)
    {
        TargetRotQuat = -TargetRotQuat;
    }

    // 计算从 Start 到 Target 的相对旋转四元数
    // 用于检测目标是否在插值过程中发生了反向
    // NowRotQuat = Start⁻¹ * Target，表示"还需要转多少"
    FQuat NowRotQuat = StartRotQuat.Inverse() * TargetRotQuat;
    // 同样保证短弧
    if (NowRotQuat.W < 0.f) NowRotQuat = -NowRotQuat;
    NowRotQuat.Normalize();

    // ── 反向检测 ────────────────────────────────────────────
    if (!bHasRotQuat)
    {
        // 第一帧：初始化缓存，没有上一帧可以比较
        RotQuatCache = NowRotQuat;
        DeltaQuat = NowRotQuat;
        bHasRotQuat = true;
    }
    else
    {
        // 与上一帧的相对旋转做点积
        // 点积 > 0：方向一致，插值正常推进
        // 点积 < 0：方向反了，说明目标旋转发生了大幅变化
        //           导致原来的短弧变成了长弧，需要重置起点
        float DotProduct = NowRotQuat | RotQuatCache;

        if (DotProduct < 0.f)
        {
            UE_LOG(LogTemp, Display, TEXT("[Blend] 检测到反向，重置起点"));

            // ── 重置起点为当前过渡相机的旋转 ──────────────
            // 不能从头开始（会闪），只能从"现在的位置"继续插值
            BlendStartRot = TransitionCamera->GetActorRotation();
            StartRotQuat = BlendStartRot.Quaternion();

            // 新起点与目标也要保证同半球
            if ((StartRotQuat | TargetRotQuat) < 0.f)
            {
                TargetRotQuat = -TargetRotQuat;
            }

            // 重新计算相对四元数
            NowRotQuat = StartRotQuat.Inverse() * TargetRotQuat;
            if (NowRotQuat.W < 0.f) NowRotQuat = -NowRotQuat;
            NowRotQuat.Normalize();

            // ── 记录反向发生时的时间进度 ───────────────────
            // QuatPathReverseTime = 反向时的 SmoothT
            // 用于构造新的线性映射函数（见下方 QuatSmoothT 计算）
            QuatPathReverseTime = SmoothT;

            // ── 计算 QuatLerp（新函数的起点 Y 值）──────────
            // 问题：重置起点后，需要知道"现在旋转进度是多少"
            // 才能让新的插值函数从这里无缝衔接到终点（1.0）
            //
            // 方法：用角度比值估算
            //   AngleCurrent = 当前相机 → 目标 的剩余角度
            //   AngleTotal   = 新起点 → 目标 的总角度
            //   已走比例 = 1 - AngleCurrent / AngleTotal
            //   这个比例就是新函数在 QuatPathReverseTime 时对应的 Y 值
            FQuat CurrentQuat = TransitionCamera->GetActorRotation().Quaternion();
            // 同半球保证
            if ((CurrentQuat | TargetRotQuat) < 0.f) CurrentQuat = -CurrentQuat;

            float AngleTotal = StartRotQuat.AngularDistance(TargetRotQuat);
            if (AngleTotal > KINDA_SMALL_NUMBER)
            {
                float AngleCurrent = CurrentQuat.AngularDistance(TargetRotQuat);
                // Clamp 避免边界浮点误差导致函数斜率异常
                QuatLerp = FMath::Clamp(1.f - AngleCurrent / AngleTotal, 0.01f, 0.99f);
            }
            else
            {
                // 夹角极小（目标几乎没动），退化为固定起点
                QuatLerp = 0.07f;
            }

           /* UE_LOG(LogTemp, Display, TEXT("[Blend] QuatPathReverseTime=%.3f QuatLerp=%.3f"),
                QuatPathReverseTime, QuatLerp);*/
        }

        // 每帧更新缓存，供下一帧比较
        RotQuatCache = NowRotQuat;
    }

    // ── 计算最终旋转插值进度 QuatSmoothT ───────────────────
    // 正常情况：QuatSmoothT = SmoothT，直接用缓入缓出进度
    //
    // 发生反向后：需要一个线性函数把 SmoothT 重新映射
    // 使得：
    //   当 SmoothT = QuatPathReverseTime 时，QuatSmoothT = QuatLerp（当前进度）
    //   当 SmoothT = 1.0               时，QuatSmoothT = 1.0（到达终点）
    //
    // 这条直线方程：
    //   y = k * x + b
    //   k = (QuatLerp - 1) / (QuatPathReverseTime - 1)
    //   b = (QuatPathReverseTime - QuatLerp) / (QuatPathReverseTime - 1)
    float QuatSmoothT;
    if (QuatPathReverseTime == 0.f)
    {
        // 没有发生反向，正常插值
        QuatSmoothT = SmoothT;
    }
    else
    {
        float k = (QuatLerp - 1.f) / (QuatPathReverseTime - 1.f);
        float b = (QuatPathReverseTime - QuatLerp) / (QuatPathReverseTime - 1.f);
        QuatSmoothT = FMath::Clamp(k * SmoothT + b, 0.f, 1.f);
    }

    // ── 执行球面插值 ────────────────────────────────────────
    // Slerp 在单位四元数球面上沿最短弧匀速插值
    // 相比 Lerp 保证了旋转速度均匀，不会在某个方向上加速
    FQuat PrevQuat = TransitionCamera->GetActorRotation().Quaternion();
    FQuat NewRotQuat = FQuat::Slerp(StartRotQuat, TargetRotQuat, QuatSmoothT);
    NewRotQuat.Normalize();

    // ── 记录本帧旋转增量，供下帧反向检测用 ─────────────────
    // DeltaQuat = 上一帧相机旋转⁻¹ * 本帧相机旋转
    // 表示"本帧转了多少"，下帧用于估算 QuatLerp 的备用
    DeltaQuat = PrevQuat.Inverse() * NewRotQuat;
    if (DeltaQuat.W < 0.f) DeltaQuat = -DeltaQuat;

    // ── 应用到过渡相机 ──────────────────────────────────────
    FRotator NewRot = NewRotQuat.Rotator();
    TransitionCamera->SetActorLocation(NewLoc);
    TransitionCamera->SetActorRotation(NewRot);

  /*  UE_LOG(LogTemp, Display, TEXT("[Blend] T=%.3f SmoothT=%.3f QuatSmoothT=%.3f"),
        T, SmoothT, QuatSmoothT);*/

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

    // ⭐ 先Possess
    Possess(NewPawn);

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
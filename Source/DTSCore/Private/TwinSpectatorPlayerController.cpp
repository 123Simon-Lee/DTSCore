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

    // 画面给到过渡相机  
    SetViewTarget(TransitionCamera);

    BlendStartLoc = CamLoc;
    BlendStartRot = CamRot;

    BlendTargetPawn = NewPawn;

    BlendTime = 0.f;
    bBlending = true;
}

//////////////////////////////////////////////////////////
//Tick插值
//////////////////////////////////////////////////////////

void ATwinSpectatorPlayerController::UpdateBlend(float DeltaTime)
{
    if (!bBlending || !BlendTargetPawn) return;

    
    BlendTime += DeltaTime;

    float T = FMath::Clamp(BlendTime / BlendDuration, 0.f, 1.f);
    float SmoothT = FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);

    FVector TargetLoc;
    FRotator TargetRot;

    // ⭐ 优先用CameraComponent（非常重要）
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

    FVector NewLoc = FMath::Lerp(BlendStartLoc, TargetLoc, SmoothT);
    FRotator NewRot = FMath::Lerp(BlendStartRot, TargetRot, SmoothT);

    // ⭐ 只控制过渡相机
    TransitionCamera->SetActorLocation(NewLoc);
    TransitionCamera->SetActorRotation(NewRot);

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
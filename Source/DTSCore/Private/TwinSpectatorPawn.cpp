#include "TwinSpectatorPawn.h"
#include "Camera/CameraComponent.h"
#include "InputModifiers.h"
#include "GameFramework/SpringArmComponent.h"
#include <Kismet/KismetMathLibrary.h>
#include <UDSTCoreFunctionLibrary.h>

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

void ATwinSpectatorPawn::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        // 鸟瞰默认限制 Pitch
        PC->PlayerCameraManager->ViewPitchMin = -89.f;
        PC->PlayerCameraManager->ViewPitchMax = 0.f;

        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(MappingContext, 0);
        }
    }

    SetActorLocation(CameraStartLocation);

    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
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

    CameraTimeline.TickTimeline(DeltaTime);

    ZoomVelocity = FMath::FInterpTo(ZoomVelocity, 0.f, DeltaTime, Damping);
    float NewLength = SpringArm->TargetArmLength - ZoomVelocity * DeltaTime;
    SpringArm->TargetArmLength = FMath::Clamp(NewLength, MinZoom, MaxZoom);

    // DeviceFocus 模式下 WASD 持续移动
    if (CameraControlMode == ECameraControlMode::DeviceFocus)
    {
        FVector MoveInput = WASDMoveInput;
        if (!MoveInput.IsNearlyZero())
        {
            FRotator Rot = GetActorRotation();
            Rot.Pitch = 0.f;
            Rot.Roll = 0.f;

            FVector Forward = FRotationMatrix(Rot).GetUnitAxis(EAxis::X);
            FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);

            FVector Delta = (Forward * WASDMoveInput.X + Right * WASDMoveInput.Y)
                * WASDMoveSpeed * DeltaTime;

            SetActorLocation(GetActorLocation() + Delta);
        }
    }
}

// ===== 模式切换 =====
void ATwinSpectatorPawn::SetCameraControlMode(ECameraControlMode NewMode)
{
    CameraControlMode = NewMode;

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        switch (NewMode)
        {
        case ECameraControlMode::BirdsEye:
            // 鸟瞰：Pitch 限制 -89 ~ 0
            PC->PlayerCameraManager->ViewPitchMin = -89.f;
            PC->PlayerCameraManager->ViewPitchMax = 0.f;
            break;

        case ECameraControlMode::DeviceFocus:
            // 设备定位：Pitch 放开 -89 ~ 89
            PC->PlayerCameraManager->ViewPitchMin = -89.f;
            PC->PlayerCameraManager->ViewPitchMax = 89.f;
            break;
        }
    }

    // 切换时清空 WASD 输入，防止残留
    WASDMoveInput = FVector::ZeroVector;
}

bool ATwinSpectatorPawn::CanUseWASD() const
{
    return CameraControlMode == ECameraControlMode::DeviceFocus;
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

bool ATwinSpectatorPawn::CanZoom() const
{
    return CameraControlMode == ECameraControlMode::BirdsEye;
}

// ===== 输入绑定 =====
void ATwinSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    MappingContext = NewObject<UInputMappingContext>(this);

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
        FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::S);
        UInputModifierNegate* Negate = NewObject<UInputModifierNegate>();
        M.Modifiers.Add(Negate);
    }
    // D 右移 → 把 X 轴换到 Y 轴：Swizzle YXZ
    {
        FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::D);
        UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>();
        Swizzle->Order = EInputAxisSwizzle::YXZ;
        M.Modifiers.Add(Swizzle);
    }
    // A 左移 → 先 Swizzle 换到 Y 轴，再 Negate 取反
    {
        FEnhancedActionKeyMapping& M = MappingContext->MapKey(IA_Move, EKeys::A);
        UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>();
        Swizzle->Order = EInputAxisSwizzle::YXZ;
        UInputModifierNegate* Negate = NewObject<UInputModifierNegate>();
        // 注意顺序：先 Swizzle 再 Negate
        M.Modifiers.Add(Swizzle);
        M.Modifiers.Add(Negate);
    }


    MappingContext->MapKey(IA_LookAndMouse, EKeys::Mouse2D);
    MappingContext->MapKey(IA_Zoom, EKeys::MouseWheelAxis);
    MappingContext->MapKey(IA_LeftMouse, EKeys::LeftMouseButton);
    MappingContext->MapKey(IA_RightMouse, EKeys::RightMouseButton);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(IA_LookAndMouse, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::MouseMove);
        EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::Zoom);

        EIC->BindAction(IA_LeftMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnLeftDown);
        EIC->BindAction(IA_LeftMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnLeftUp);
        EIC->BindAction(IA_RightMouse, ETriggerEvent::Started, this, &ATwinSpectatorPawn::OnRightDown);
        EIC->BindAction(IA_RightMouse, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::OnRightUp);

        EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &ATwinSpectatorPawn::WASDMove);
        EIC->BindAction(IA_Move, ETriggerEvent::Completed, this, &ATwinSpectatorPawn::WASDMoveStop);
    }

    PlayerInputComponent->BindTouch(IE_Pressed, this, &ATwinSpectatorPawn::OnTouchPressed);
    PlayerInputComponent->BindTouch(IE_Released, this, &ATwinSpectatorPawn::OnTouchReleased);
    PlayerInputComponent->BindTouch(IE_Repeat, this, &ATwinSpectatorPawn::OnTouchMoved);
}

// ===== 鼠标按键 =====
void ATwinSpectatorPawn::OnLeftDown(const FInputActionValue&)
{
    if (!CanLeftMouseRotate()) return;
    bLeftMouseDown = true;
}

void ATwinSpectatorPawn::OnLeftUp(const FInputActionValue&)
{
    bLeftMouseDown = false;
}

void ATwinSpectatorPawn::OnRightDown(const FInputActionValue&)
{
    if (!CanRightMousePan()) return;
    bRightMouseDown = true;
}

void ATwinSpectatorPawn::OnRightUp(const FInputActionValue&)
{
    bRightMouseDown = false;
}

// ===== 鼠标移动 =====
void ATwinSpectatorPawn::MouseMove(const FInputActionValue& Value)
{
    FVector2D Axis = Value.Get<FVector2D>();

    // 右键平移：仅鸟瞰
    if (bRightMouseDown && CanRightMousePan())
    {
        SetCameraMove(Axis.X, true);
        SetCameraMove(Axis.Y, false);
    }
    // 左键旋转：两种模式都支持，Pitch 限制由 PlayerCameraManager 控制
    else if (bLeftMouseDown && CanLeftMouseRotate())
    {
        SetCameraRotate(Axis.X, true);
        SetCameraRotate(Axis.Y, false);
    }
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
    if (FMath::IsNearlyZero(Val)) return;

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
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
                Rot.Pitch = FMath::Clamp(Rot.Pitch, -89.f, 0.f);
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
void ATwinSpectatorPawn::WASDMove(const FInputActionValue& Value)
{
    if (!CanUseWASD()) return;
    FVector2D Axis = Value.Get<FVector2D>();
    WASDMoveInput.X = Axis.X; // 前后
    WASDMoveInput.Y = Axis.Y; // 左右
}

void ATwinSpectatorPawn::WASDMoveStop(const FInputActionValue&)
{
    WASDMoveInput = FVector::ZeroVector;
}

// ===== 缩放 =====
void ATwinSpectatorPawn::Zoom(const FInputActionValue& Value)
{
    if (!CanZoom()) return;  //点位定位的时候禁用滚轮  
    ZoomVelocity += Value.Get<float>() * ZoomSpeed;
}

// ===== 触控 =====
void ATwinSpectatorPawn::OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location)
{
    TouchMap.Add(FingerIndex, Location);
}

void ATwinSpectatorPawn::OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location)
{
    TouchMap.Remove(FingerIndex);
    if (TouchMap.Num() < 2) LastPinchDistance = 0.f;
}

void ATwinSpectatorPawn::OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location)
{
    if (!TouchMap.Contains(FingerIndex)) return;

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
        FVector P1 = It.Value(); ++It;
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

    if (APlayerController* PC = GetWorld()->GetFirstPlayerController() )
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

    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        if (PC->GetPawn() == this)
        {
            PC->SetControlRotation(CameraNewRotate);   
        }
        else
        {
            this->SetActorRotation(CameraNewRotate);
        }
    }

    if (SpringArm)
        SpringArm->TargetArmLength = SpringNewLength;
}

void ATwinSpectatorPawn::CameraMove(FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength)
{
    GetCameraLocation();
    
    TargetLocation = CameraNewLocation;
    TargetRotation = CameraNewRotate;
    TargetArmLength = SpringNewLength;

    if (CameraCurve)
        CameraTimeline.PlayFromStart();
}

void ATwinSpectatorPawn::FocusOnActor(AActor* TargetActor, float InterpTime, float ZoomScale, FRotator NewRotation)
{
    if (!TargetActor) return;

    //递归获取所有层级的子节点
    TArray<AActor*> AllChildren;
    TargetActor->GetAttachedActors(AllChildren, true); // true = 递归

    // 用包围盒合并，兼容三种情况：
    //    1. A01 → 1F/2F → Mesh（多层）
    //    2. A01 → Mesh（单层，无楼层节点）
    //    3. 直接传 Mesh Actor 本身
    FBox BoundingBox(ForceInit);

    // 先把所有子节点的 Bounds 合并
    for (AActor* Child : AllChildren)
    {
        if (!Child) continue;

        FVector Origin, Extent;
        Child->GetActorBounds(true, Origin, Extent);
        
        // Extent 为 0 说明是空节点（纯 SceneComponent），跳过
        if (Extent.IsNearlyZero(UE_KINDA_SMALL_NUMBER)) continue;
        
        BoundingBox += FBox(Origin - Extent, Origin + Extent);
      
    }
    
    // ⭐ 如果子节点都是空节点，兜底用 TargetActor 自身
    if (!BoundingBox.IsValid)
    {
        FVector Origin, Extent;
        TargetActor->GetActorBounds(true, Origin, Extent);
        BoundingBox = FBox(Origin - Extent, Origin + Extent);
      
    }
    
    FVector Center = BoundingBox.GetCenter();
    GetCameraLocation();

    float FOV = Camera->FieldOfView;
    float HalfFOV = FMath::DegreesToRadians(FOV * 0.5f);
    float Radius = BoundingBox.GetExtent().Size();
    float Distance = (HalfFOV > SMALL_NUMBER)
        ? Radius / FMath::Tan(HalfFOV)
        : Radius * 2.f;
    float NewArmLength = Distance * ZoomScale;

    CameraMove(Center, NewRotation, NewArmLength);
}

void ATwinSpectatorPawn::ResetCameraLocation()
{
    CameraMove(CameraStartLocation, CameraStartRotate, SpringStartLength);
}

float ATwinSpectatorPawn::GetFOV()
{
    return Camera ? Camera->FieldOfView : 0.f;
}

void ATwinSpectatorPawn::OnCameraTimelineUpdate(float Value)
{
    FVector   NewLocation = UKismetMathLibrary::VLerp(CameraLocation, TargetLocation, Value);
    FRotator  NewRotation = UKismetMathLibrary::RLerp(CameraRotate, TargetRotation, Value, true);
    float     NewArmLength = UKismetMathLibrary::Lerp(SpringLen, TargetArmLength, Value);
    SetCameraNewLocation(Value, NewLocation, NewRotation, NewArmLength);
}

void ATwinSpectatorPawn::OnCameraTimelineFinished()
{
    OnCameraMoveFinished.Broadcast();
}
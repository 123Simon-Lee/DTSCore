#include "UniversalCharacter.h"

// =======================
// 构造
// =======================
AUniversalCharacter::AUniversalCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArm");
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 75.f));

    Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
    Camera->SetupAttachment(SpringArm);

    // 默认第三人称
    SpringArm->TargetArmLength = 300.f;
    SpringArm->bUsePawnControlRotation = true;

    // 摄像机旋转延迟，使得转向平滑
    SpringArm->bEnableCameraRotationLag = true;

    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;

    GetCharacterMovement()->JumpZVelocity = 600.f;
    // 角色实现默认为1.5
    GetCharacterMovement()->GravityScale = 1.5f;

    // Mesh
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshObj(
        TEXT("/DTSCore/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn")
    );

    if (MeshObj.Succeeded())
    {
        GetMesh()->SetSkeletalMesh(MeshObj.Object);
    }

    static ConstructorHelpers::FClassFinder<UAnimInstance> AnimBPClass(
        TEXT("/DTSCore/Characters/Mannequins/Animations/ABP_Manny")
    );

    if (AnimBPClass.Succeeded())
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(AnimBPClass.Class);
    }

    GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
    GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
}

// =======================
// BeginPlay
// =======================
void AUniversalCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(MappingContext, 0);
        }
    }

    // ⭐ 只做“状态初始化”
    UpdateCameraMode();
}

// =======================
// Tick（核心：插值）
// =======================
void AUniversalCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bCameraBlending) return;

    CameraBlendTime += DeltaTime;

    float T = FMath::Clamp(CameraBlendTime / CameraBlendDuration, 0.f, 1.f);

    float SmoothT = FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);

    float NewArm = FMath::Lerp(StartArmLength, TargetArmLength, SmoothT);

    SpringArm->TargetArmLength = NewArm;

    if (T >= 1.f)
    {
        bCameraBlending = false;
    }
}

// =======================
// 输入绑定（保持不动）
// =======================
void AUniversalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    MappingContext = NewObject<UInputMappingContext>(this);

    IA_Move = NewObject<UInputAction>(this);
    IA_Move->ValueType = EInputActionValueType::Axis2D;

    // WASD
    {
        FEnhancedActionKeyMapping& Map = MappingContext->MapKey(IA_Move, EKeys::W);
        Map.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
    }
    {
        FEnhancedActionKeyMapping& Map = MappingContext->MapKey(IA_Move, EKeys::S);
        auto* Neg = NewObject<UInputModifierNegate>(this);
        auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(this);
        Map.Modifiers.Add(Swizzle);
        Map.Modifiers.Add(Neg);
    }
    MappingContext->MapKey(IA_Move, EKeys::D);
    {
        FEnhancedActionKeyMapping& Map = MappingContext->MapKey(IA_Move, EKeys::A);
        Map.Modifiers.Add(NewObject<UInputModifierNegate>(this));
    }

    IA_Look = NewObject<UInputAction>(this);
    IA_Look->ValueType = EInputActionValueType::Axis2D;
    // 添加修改器
    UInputModifierNegate* LookModifiers = NewObject<UInputModifierNegate>(this);
    LookModifiers->bX = false;
    LookModifiers->bY = true;
    LookModifiers->bZ = false;
    IA_Look->Modifiers.Add(LookModifiers);
    MappingContext->MapKey(IA_Look, EKeys::Mouse2D);

    IA_Jump = NewObject<UInputAction>(this);
    IA_Jump->ValueType = EInputActionValueType::Boolean;
    MappingContext->MapKey(IA_Jump, EKeys::SpaceBar);

    IA_SwitchView = NewObject<UInputAction>(this);
    IA_SwitchView->ValueType = EInputActionValueType::Boolean;
    MappingContext->MapKey(IA_SwitchView, EKeys::V);

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AUniversalCharacter::Move);
        EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AUniversalCharacter::Look);
        EIC->BindAction(IA_SwitchView, ETriggerEvent::Started, this, &AUniversalCharacter::SwitchCameraMode);
        EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &AUniversalCharacter::StartJump);
        EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &AUniversalCharacter::StopJump);
    }
}

// =======================
// 移动/视角（不变）
// =======================
void AUniversalCharacter::Move(const FInputActionValue& Value)
{
    FVector2D Axis = Value.Get<FVector2D>();

    if (Controller)
    {
        FRotator Rot = Controller->GetControlRotation();
        FRotator YawRot(0, Rot.Yaw, 0);

        FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
        FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

        AddMovementInput(Forward, Axis.Y);
        AddMovementInput(Right, Axis.X);
    }
}

void AUniversalCharacter::Look(const FInputActionValue& Value)
{
    FVector2D Axis = Value.Get<FVector2D>();

    AddControllerYawInput(Axis.X);
    AddControllerPitchInput(Axis.Y);
}

// =======================
// ⭐ 切换入口（修复关键）
// =======================
void AUniversalCharacter::SwitchCameraMode()
{
    if (CameraMode == ECameraMode::ThirdPerson)
    {
        SwitchMode(ECameraMode::FirstPerson);
    }
    else
    {
        SwitchMode(ECameraMode::ThirdPerson);
    }
}

// =======================
//核心切换（插值入口）
// =======================
void AUniversalCharacter::SwitchMode(ECameraMode mode)
{
    CameraMode = mode;

    //先更新状态（旋转/显示）
    UpdateCameraMode();

    //再开始插值
    StartArmLength = SpringArm->TargetArmLength;

    if (CameraMode == ECameraMode::FirstPerson)
    {
        TargetArmLength = FirstPersonArmLength;
    }
    else
    {
        TargetArmLength = ThirdPersonArmLength;
    }

    CameraBlendTime = 0.f;
    bCameraBlending = true;
}

// =======================
//状态设置（已修复：不再改位置）
// =======================
void AUniversalCharacter::UpdateCameraMode()
{
    if (CameraMode == ECameraMode::ThirdPerson)
    {
        // ⭐ 恢复Mesh显示（完整恢复）
        GetMesh()->SetHiddenInGame(false);
        GetMesh()->SetVisibility(true, true);

        // ⭐ 关键！！！关闭 OwnerNoSee
        GetMesh()->bOwnerNoSee = false;

        //SpringArm->SetRelativeLocation(FirstPersonOffset);

        SpringArm->bUsePawnControlRotation = true;
        bUseControllerRotationYaw = false;

        GetCharacterMovement()->bOrientRotationToMovement = true;
    }
    else
    {
        // ⭐ 第一人称隐藏
        GetMesh()->SetHiddenInGame(true);

        GetMesh()->bOwnerNoSee = true;

        bUseControllerRotationYaw = true;
        SpringArm->bUsePawnControlRotation = true;

        GetCharacterMovement()->bOrientRotationToMovement = false;
    }
}

// =======================
// Jump
// =======================
void AUniversalCharacter::StartJump()
{
    Jump();
}

void AUniversalCharacter::StopJump()
{
    StopJumping();
}
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include <Components/TimelineComponent.h>
#include "TwinSpectatorPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;



// 相机控制模式
UENUM(BlueprintType)
enum class ECameraControlMode : uint8
{
    // 鸟瞰模式：禁用WASD，启用鼠标右键拖动位移，左键旋转
    BirdsEye     UMETA(DisplayName = "鸟瞰视角"),
    // 设备定位模式：启用WASD，禁用鼠标右键拖动位移，左键旋转锁定
    DeviceFocus  UMETA(DisplayName = "设备定位"),
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCameraMoveFinished);

UCLASS()
class DTSCORE_API ATwinSpectatorPawn : public APawn
{
    GENERATED_BODY()

public:
    ATwinSpectatorPawn();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    void OnLeftDown(const FInputActionValue&);
    void OnLeftUp(const FInputActionValue&);
    void OnRightDown(const FInputActionValue&);
    void OnRightUp(const FInputActionValue&);

public:
    UPROPERTY(BlueprintAssignable, Category = "Camera")
    FOnCameraMoveFinished OnCameraMoveFinished;

    // ===== 控制模式 =====
    UPROPERTY(BlueprintReadOnly, Category = "Camera Control")
    ECameraControlMode CameraControlMode = ECameraControlMode::BirdsEye;

    // 切换控制模式（蓝图和C++都可调用）
    UFUNCTION(BlueprintCallable, Category = "Camera Control")
    void SetCameraControlMode(ECameraControlMode NewMode);

    // 各模式下的行为判断（内部使用）
    bool CanUseWASD() const;
    bool CanRightMousePan() const;
    bool CanLeftMouseRotate() const;

    // ===== Input =====
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputMappingContext* MappingContext;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_Zoom;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_LookAndMouse;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_Move; // WASD

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_RightMouse;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_LeftMouse;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Input action")
    UInputAction* IA_Touch;

    UPROPERTY(EditAnywhere, Category = "Input Config")
    FVector WASDMoveInput = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Input Config")
    float WASDMoveSpeed = 500.f;

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

    UPROPERTY(EditAnywhere)
    float Damping = 5.f;

    UPROPERTY(EditAnywhere)
    float MinZoom = 100.f;

    UPROPERTY(EditAnywhere)
    float MaxZoom = 60000.0f;

    // ===== Touch =====
    TMap<ETouchIndex::Type, FVector> TouchMap;
    UPROPERTY(BlueprintReadOnly)
    float LastPinchDistance = 0.f;

public:
    // ===== Components =====
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    USceneComponent* Root;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    USpringArmComponent* SpringArm;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    UCameraComponent* Camera;

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
    UCurveFloat* CameraCurve;

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

    void MouseMove(const FInputActionValue& Value);
    void SetCameraMove(float Val, bool bX);
    void SetCameraRotate(float Val, bool bX);
    void Zoom(const FInputActionValue& Value);
    void WASDMove(const FInputActionValue& Value); // 新增WASD处理
    void WASDMoveStop(const FInputActionValue& Value);
    void OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
    void OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);
    void OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);

    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    void GetCameraLocation();

    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    void SetCameraNewLocation(float Time, FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength);

    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    void CameraMove(FVector CameraNewLocation, FRotator CameraNewRotate, float SpringNewLength);

    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    void FocusOnActor(AActor* TargetActor, float InterpTime, float ZoomScale, FRotator NewRotation);
    
    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    void ResetCameraLocation();

    UFUNCTION(BlueprintCallable, Category = "Camera Move Event")
    float GetFOV();
};
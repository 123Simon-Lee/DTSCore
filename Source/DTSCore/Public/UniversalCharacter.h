// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "InputActionValue.h"
#include "EnhancedInputComponent.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UniversalCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;

UENUM(BlueprintType)
enum class ECameraMode :uint8
{
    FirstPerson,
    ThirdPerson
};

UCLASS()
class DTSCORE_API AUniversalCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AUniversalCharacter();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    void Move(const FInputActionValue& Value);

    void Look(const FInputActionValue& Value);

public:


	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	USpringArmComponent* SpringArm;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    UCameraComponent* Camera;
    //绑定输入
    // Mapping
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputMappingContext* MappingContext;

    // Actions
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Move;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Look;
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_Jump;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    UInputAction* IA_SwitchView;

    // ===== 模式 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECameraMode CameraMode = ECameraMode::ThirdPerson;

    // ===== 参数 =====
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ThirdPersonArmLength = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FirstPersonArmLength = 0.f;
	
    // ===== 控制 =====
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);

    UFUNCTION(BlueprintCallable, Category = "SwitchMode")
    void SwitchCameraMode();
    UFUNCTION(BlueprintCallable, Category = "SwitchMode")
    void SwitchMode(ECameraMode mode);
    void UpdateCameraMode();

    void StartJump();
    void StopJump();
private:
    // ===== 插值状态 =====
    bool bCameraBlending = false;

    float CameraBlendTime = 0.f;
    float CameraBlendDuration = 0.25f;

    // 起点
    float StartArmLength;

    // 目标
    float TargetArmLength;
};

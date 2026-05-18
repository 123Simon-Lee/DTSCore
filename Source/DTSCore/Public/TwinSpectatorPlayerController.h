#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "TwinSpectatorPlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnControllerReady);

class UInputMappingContext;

UENUM(BlueprintType)
enum class EViewMode : uint8
{
    Spectator   UMETA(DisplayName = "Spectator（俯视漫游）"),
    Roaming     UMETA(DisplayName = "Roaming（角色漫游）")
};

UCLASS()
class DTSCORE_API ATwinSpectatorPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable, Category = "Init")
    FOnControllerReady OnControllerReady;  

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    APawn* TwinSpectatorPawn;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    APawn* CharacterPawn;

    UPROPERTY(BlueprintReadOnly, Category = "ViewMode")
    EViewMode CurrentViewMode = EViewMode::Spectator;

    ATwinSpectatorPlayerController();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // 按模式切换，外部统一调这一个
    UFUNCTION(BlueprintCallable, Category = "ViewMode")
    void SwitchViewMode(EViewMode NewMode);

    //获取当前模式
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewMode")
    EViewMode GetCurrentViewMode() const { return CurrentViewMode; }

    UFUNCTION(BlueprintCallable, Category = "Switch")
    void SwitchToSpectator();
    UFUNCTION(BlueprintCallable, Category = "Switch")
    void SwitchToCharacter();
private:

    bool bBlending = false;

    FVector BlendStartLoc;
    FRotator BlendStartRot;

    float BlendTime = 0.f;
    float BlendDuration = 1.f;

    UPROPERTY()
    APawn* BlendTargetPawn = nullptr;

    float LastSwitchTime = -999.f;
    float SwitchCooldown = 0.15f;


    void StartBlend(APawn* NewPawn);
    void UpdateBlend(float DeltaTime);
    void FinishBlend();

    void SwitchInputContext(UInputMappingContext* NewContext);

    UPROPERTY()
    class ACameraActor* TransitionCamera;


};
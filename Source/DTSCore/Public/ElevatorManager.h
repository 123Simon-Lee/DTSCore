#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElevatorManager.generated.h"

USTRUCT(BlueprintType)
struct FDeviceData
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString DeviceCode;
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 FloorValue = 0;
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DirectionValue = 0;
};

// === 电梯楼层范围 ===
USTRUCT(BlueprintType)
struct FElevatorFloorRange
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 MinFloor = 1;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 MaxFloor = 10;
};

USTRUCT(BlueprintType)
struct FElevatorState
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    AActor* Actor = nullptr;
    UPROPERTY(BlueprintReadOnly)
    float StartZ = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float TargetZ = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float Alpha = 0.f;
    UPROPERTY(BlueprintReadOnly)
    float Speed = 1.f;
    UPROPERTY(BlueprintReadOnly)
    bool bMoving = false;
    UPROPERTY(BlueprintReadOnly)
    bool bRandomPending = false;
    UPROPERTY(BlueprintReadOnly)
    float RandomWaitTimer = 0.f;
    // === 初始位置 ===
    UPROPERTY(BlueprintReadOnly)
    float InitialZ = 0.f;
};

UCLASS()
class DTSCORE_API AElevatorManager : public AActor
{
    GENERATED_BODY()
public:
    AElevatorManager();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elevator")
    TArray<AActor*> Elevators;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Elevator")
    TMap<int32, float> FloorZMap;

    UPROPERTY(EditAnywhere, Category = "Elevator")
    float EasePower = 2.f;

    UPROPERTY(EditAnywhere, Category = "Elevator")
    float TravelTime = 5.f;

    UPROPERTY(BlueprintReadWrite, Category = "Elevator")
    TArray<FDeviceData> PendingDeviceData;

    // === 每部电梯的楼层范围，Key = DeviceCode ===
    // 随机模式会限定在此范围内随机；不在表中的电梯使用全部楼层
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elevator|Random")
    TMap<FString, FElevatorFloorRange> ElevatorFloorRanges;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elevator|Random")
    float RandomMoveInterval = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elevator|Random")
    bool bRandomModeActive = false;

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void CollectElevatorsByTag();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void SetFloorZMap(const TMap<int32, float>& InFloorZMap);

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void InitStates();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void MoveElevatorsToFloor(int32 TargetFloor);

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void MoveElevatorsToAPI();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void MoveElevatorByCode(const FString& DeviceCode, int32 TargetFloor);

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void StopAll();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void StartRandomMode();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void StopRandomMode();

    UFUNCTION(BlueprintCallable, Category = "Elevator")
    void ResetToInitialPositions();
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

private:
    UPROPERTY()
    TArray<FElevatorState> States;

    void StartMove(int32 Index, float TargetZ);
    int32 FindIndexByCode(const FString& DeviceCode) const;
    void ScheduleRandomMove(int32 Index);
    float EaseInOut(float T) const;
};
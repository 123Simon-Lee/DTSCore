#include "ElevatorManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

AElevatorManager::AElevatorManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AElevatorManager::BeginPlay()
{
    Super::BeginPlay();
    CollectElevatorsByTag();
    InitStates();
}

void AElevatorManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    for (int32 i = 0; i < States.Num(); i++)
    {
        FElevatorState& State = States[i];
        if (!State.Actor) continue;

        // --- 移动逻辑 ---
        if (State.bMoving)
        {
            State.Alpha += State.Speed * DeltaTime;
            if (State.Alpha >= 1.f)
            {
                State.Alpha = 1.f;
                State.bMoving = false;
            }

            FVector Loc = State.Actor->GetActorLocation();
            Loc.Z = FMath::Lerp(State.StartZ, State.TargetZ, EaseInOut(State.Alpha));
            State.Actor->SetActorLocation(Loc);
        }

        // --- 随机模式：到达后等待再随机 ---
        if (bRandomModeActive && !State.bMoving)
        {
            if (!State.bRandomPending)
            {
                State.bRandomPending = true;
                State.RandomWaitTimer = RandomMoveInterval;
            }
            else
            {
                State.RandomWaitTimer -= DeltaTime;
                if (State.RandomWaitTimer <= 0.f)
                    ScheduleRandomMove(i);
            }
        }
    }
}

// =========================================================
//  EaseInOut
// =========================================================
float AElevatorManager::EaseInOut(float T) const
{
    T = FMath::Clamp(T, 0.f, 1.f);
    return FMath::InterpEaseInOut(0.f, 1.f, T, EasePower);
}

// =========================================================
//  收集电梯
// =========================================================
void AElevatorManager::CollectElevatorsByTag()
{
    Elevators.Empty();
    UGameplayStatics::GetAllActorsWithTag(GetWorld(), "DTCT", Elevators);
}

// =========================================================
//  设置楼层 Z 表
// =========================================================
void AElevatorManager::SetFloorZMap(const TMap<int32, float>& InFloorZMap)
{
    FloorZMap = InFloorZMap;
}

// =========================================================
//  初始化状态数组
// =========================================================
void AElevatorManager::InitStates()
{
    States.Empty();
    for (AActor* Actor : Elevators)
    {
        FElevatorState State;
        State.Actor = Actor;
        State.Speed = 1.f / FMath::Max(TravelTime, 0.1f);
        State.InitialZ = Actor->GetActorLocation().Z;  // 记录初始位置
        States.Add(State);
    }
}

// =========================================================
//  启动单部电梯
// =========================================================
void AElevatorManager::StartMove(int32 Index, float TargetZ)
{
    if (!States.IsValidIndex(Index) || !States[Index].Actor) return;

    FElevatorState& State = States[Index];
    State.StartZ = State.Actor->GetActorLocation().Z;
    State.TargetZ = TargetZ;
    State.Alpha = 0.f;
    State.bMoving = true;
    State.bRandomPending = false;
}

// =========================================================
//  按 DeviceCode 查找索引
// =========================================================
int32 AElevatorManager::FindIndexByCode(const FString& DeviceCode) const
{
    for (int32 i = 0; i < States.Num(); i++)
    {
        if (!States[i].Actor) continue;
        if (UKismetSystemLibrary::GetDisplayName(States[i].Actor) == DeviceCode)
            return i;
    }
    return INDEX_NONE;
}

// =========================================================
//  所有电梯移动到同一楼层
// =========================================================
void AElevatorManager::MoveElevatorsToFloor(int32 TargetFloor)
{
    if (!FloorZMap.Contains(TargetFloor))
    {
        UE_LOG(LogTemp, Warning, TEXT("ElevatorManager: Floor %d not in FloorZMap"), TargetFloor);
        return;
    }

    float TargetZ = FloorZMap[TargetFloor];
    for (int32 i = 0; i < States.Num(); i++)
        StartMove(i, TargetZ);
}

// =========================================================
//  按 API 数据各自移动
// =========================================================
void AElevatorManager::MoveElevatorsToAPI()
{
    for (const FDeviceData& Data : PendingDeviceData)
    {
        if (!FloorZMap.Contains(Data.FloorValue)) continue;

        float TargetZ = FloorZMap[Data.FloorValue];
        int32 Index = FindIndexByCode(Data.DeviceCode);

        if (Index != INDEX_NONE)
            StartMove(Index, TargetZ);
    }
}

// =========================================================
//  按 DeviceCode 单独移动
// =========================================================
void AElevatorManager::MoveElevatorByCode(const FString& DeviceCode, int32 TargetFloor)
{
    if (!FloorZMap.Contains(TargetFloor)) return;

    float TargetZ = FloorZMap[TargetFloor];
    int32 Index = FindIndexByCode(DeviceCode);

    if (Index != INDEX_NONE)
        StartMove(Index, TargetZ);
}

// =========================================================
//  停止所有
// =========================================================
void AElevatorManager::StopAll()
{
    for (FElevatorState& State : States)
        State.bMoving = false;
}

// =========================================================
//  开启随机模式
// =========================================================
void AElevatorManager::StartRandomMode()
{
    if (FloorZMap.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ElevatorManager: FloorZMap is empty, cannot start random mode"));
        return;
    }

    bRandomModeActive = true;

    for (int32 i = 0; i < States.Num(); i++)
        ScheduleRandomMove(i);
}

// =========================================================
//  关闭随机模式
// =========================================================
void AElevatorManager::StopRandomMode()
{
    bRandomModeActive = false;
    for (FElevatorState& State : States)
        State.bRandomPending = false;
}

// =========================================================
//  为单部电梯安排下一次随机跳转（按范围过滤楼层）
// =========================================================
void AElevatorManager::ScheduleRandomMove(int32 Index)
{
    if (!States.IsValidIndex(Index) || !bRandomModeActive) return;

    // 取该电梯的 DeviceCode
    FString Code = UKismetSystemLibrary::GetDisplayName(States[Index].Actor);

    // 确定楼层范围（没配置则不限制）
    bool bHasRange = ElevatorFloorRanges.Contains(Code);
    int32 MinFloor = bHasRange ? ElevatorFloorRanges[Code].MinFloor : INT32_MIN;
    int32 MaxFloor = bHasRange ? ElevatorFloorRanges[Code].MaxFloor : INT32_MAX;

    // 收集范围内的可用楼层
    TArray<int32> ValidFloors;
    for (const TPair<int32, float>& Pair : FloorZMap)
    {
        if (Pair.Key >= MinFloor && Pair.Key <= MaxFloor)
            ValidFloors.Add(Pair.Key);
    }

    if (ValidFloors.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("ElevatorManager: No valid floors for [%s] in range [%d, %d]"),
            *Code, MinFloor, MaxFloor);
        return;
    }

    int32 RandomFloor = ValidFloors[FMath::RandRange(0, ValidFloors.Num() - 1)];
    StartMove(Index, FloorZMap[RandomFloor]);
}
void AElevatorManager::ResetToInitialPositions()
{
    for (FElevatorState& State : States)
    {
        if (!State.Actor) continue;

        // 直接设位置，不走插值
        FVector Loc = State.Actor->GetActorLocation();
        Loc.Z = State.InitialZ;
        State.Actor->SetActorLocation(Loc);

        // 清掉运动状态，防止 Tick 继续插值
        State.bMoving = false;
        State.bRandomPending = false;
        State.Alpha = 0.f;
    }
}
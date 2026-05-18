#include "CustomTraceManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void UCustomTraceManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UCustomTraceManager::Deinitialize()
{
    Entries.Empty();
    Super::Deinitialize();
}

UCustomTraceManager* UCustomTraceManager::GetInstance(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UCustomTraceManager>() : nullptr;
}

void UCustomTraceManager::RegisterChannel(ECollisionChannel Channel, FTraceDetectFunc DetectFunc)
{
    FTraceChannelEntry& Entry = Entries.FindOrAdd(Channel);
    if (DetectFunc.IsBound())
    {
        // 使用调用方传入的自定义检测函数
        Entry.DetectFunc = MoveTemp(DetectFunc);
    }
    else
    {
        // 未传入则绑定内置默认检测，捕获 Channel 值
        Entry.DetectFunc = FTraceDetectFunc::CreateLambda(
            [Channel](APlayerController* PC) -> AActor*
            {
                return UCustomTraceManager::DefaultDetect(PC, Channel);
            }
        );
    }
}

void UCustomTraceManager::UnregisterChannel(ECollisionChannel Channel)
{
    if (FTraceChannelEntry* Entry = Entries.Find(Channel))
    {
        // 注销前若有命中Actor，广播一次离开事件，让订阅者有机会清理状态
        if (Entry->CurrentHitActor)
            Entry->OnHitChanged.Broadcast(/*NewHit=*/nullptr, /*OldHit=*/Entry->CurrentHitActor);
        Entries.Remove(Channel);
    }
}

FDelegateHandle UCustomTraceManager::Subscribe(
    ECollisionChannel Channel, FOnTraceHitChanged::FDelegate&& Delegate)
{
    FTraceChannelEntry& Entry = Entries.FindOrAdd(Channel);
    return Entry.OnHitChanged.Add(MoveTemp(Delegate));
}

void UCustomTraceManager::Unsubscribe(ECollisionChannel Channel, FDelegateHandle Handle)
{
    if (FTraceChannelEntry* Entry = Entries.Find(Channel))
        Entry->OnHitChanged.Remove(Handle);
}

AActor* UCustomTraceManager::GetCurrentHit(ECollisionChannel Channel) const
{
    if (const FTraceChannelEntry* Entry = Entries.Find(Channel))
        return Entry->CurrentHitActor;
    return nullptr;
}

void UCustomTraceManager::Tick(float DeltaTime)
{
    UGameInstance* GI = GetGameInstance();
    if (!GI) return;
    UWorld* World = GI->GetWorld();
    if (!World) return;
    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return;

    // 遍历所有已注册通道，逐一执行检测
    for (auto& Pair : Entries)
    {
        FTraceChannelEntry& Entry = Pair.Value;
        if (!Entry.DetectFunc.IsBound()) continue;

        AActor* NewHit = Entry.DetectFunc.Execute(PC);

        // 命中Actor发生变化时才广播，避免每帧无意义广播
        if (NewHit != Entry.CurrentHitActor)
        {
            AActor* OldHit = Entry.CurrentHitActor;
            Entry.CurrentHitActor = NewHit;
            Entry.OnHitChanged.Broadcast(NewHit, OldHit);
        }
    }
}

AActor* UCustomTraceManager::DefaultDetect(APlayerController* PC, ECollisionChannel Channel)
{
    // 将鼠标屏幕坐标转换为世界空间射线
    FVector WorldLocation, WorldDirection;
    if (!PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
        return nullptr;

    FVector TraceEnd = WorldLocation + WorldDirection * 100000.f;

    FHitResult HitResult;
    FCollisionQueryParams Params;
    // 忽略玩家自身Pawn，避免射线被自身遮挡
    Params.AddIgnoredActor(PC->GetPawn());
    // 启用复杂碰撞检测，兼容没有简单碰撞体的Mesh 如果不需要把它注释掉
    Params.bTraceComplex = true;

    bool bHit = PC->GetWorld()->LineTraceSingleByChannel(
        HitResult,
        WorldLocation,
        TraceEnd,
        Channel,
        Params
    );

    return bHit ? HitResult.GetActor() : nullptr;
}
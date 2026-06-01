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
    DynamicHandleMap.Empty();
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
        Entry.DetectFunc = MoveTemp(DetectFunc);
    }
    else
    {
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
        if (Entry->CurrentHitActor)
            Entry->OnHitChanged.Broadcast(nullptr, Entry->CurrentHitActor);
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

// ── Puerts / 蓝图包装实现 ────────────────────────────────

void UCustomTraceManager::BP_RegisterChannel(ECollisionChannel Channel)
{
    RegisterChannel(Channel);
}

void UCustomTraceManager::BP_UnregisterChannel(ECollisionChannel Channel)
{
    // 同时清理该通道下所有动态句柄记录，防止悬空
    TArray<int32> ToRemove;
    for (auto& Pair : DynamicHandleMap)
    {
        if (Pair.Value.Get<0>() == Channel)
            ToRemove.Add(Pair.Key);
    }
    for (int32 Id : ToRemove)
        DynamicHandleMap.Remove(Id);

    UnregisterChannel(Channel);
}

int32 UCustomTraceManager::BP_Subscribe(
    ECollisionChannel Channel, const FOnTraceHitChangedDynamic& Delegate)
{
    // 拷贝委托以在 Lambda 中捕获
    FOnTraceHitChangedDynamic DelegateCopy = Delegate;

    FDelegateHandle Handle = Subscribe(Channel,
        FOnTraceHitChanged::FDelegate::CreateLambda(
            [DelegateCopy](AActor* NewHit, AActor* OldHit)
            {
                if (DelegateCopy.IsBound())
                    DelegateCopy.Execute(NewHit, OldHit);
            }
        )
    );

    int32 Id = NextSubscribeId++;
    DynamicHandleMap.Add(Id, MakeTuple(Channel, Handle));
    return Id;
}

void UCustomTraceManager::BP_Unsubscribe(ECollisionChannel Channel, int32 SubscribeId)
{
    if (TTuple<ECollisionChannel, FDelegateHandle>* Found = DynamicHandleMap.Find(SubscribeId))
    {
        Unsubscribe(Found->Get<0>(), Found->Get<1>());
        DynamicHandleMap.Remove(SubscribeId);
    }
}

AActor* UCustomTraceManager::BP_GetCurrentHit(ECollisionChannel Channel) const
{
    return GetCurrentHit(Channel);
}

// ── Tick ────────────────────────────────────────────────

void UCustomTraceManager::Tick(float DeltaTime)
{
    // 不需要每帧都检测，16ms间隔足够
    AccumulatedTime += DeltaTime;
    if (AccumulatedTime < 0.016f) return;
    AccumulatedTime = 0.f;
    UGameInstance* GI = GetGameInstance();
    if (!GI) return;
    UWorld* World = GI->GetWorld();
    if (!World) return;
    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC) return;

    for (auto& Pair : Entries)
    {
        FTraceChannelEntry& Entry = Pair.Value;
        if (!Entry.DetectFunc.IsBound()) continue;

        AActor* NewHit = Entry.DetectFunc.Execute(PC);
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
    FVector WorldLocation, WorldDirection;
    if (!PC->DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
        return nullptr;

    FVector TraceEnd = WorldLocation + WorldDirection * 100000.f;

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(PC->GetPawn());
    Params.bTraceComplex = true;

    bool bHit = PC->GetWorld()->LineTraceSingleByChannel(
        HitResult, WorldLocation, TraceEnd, Channel, Params
    );

    return bHit ? HitResult.GetActor() : nullptr;
}
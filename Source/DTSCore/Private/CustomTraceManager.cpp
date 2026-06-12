#include "CustomTraceManager.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"

// ── 生命周期 ─────────────────────────────────────────────

void UCustomTraceManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 延迟一帧绑定 Viewport 点击输入，确保 PlayerController 与 Viewport 已就绪
    if (UWorld* World = GetGameInstance()->GetWorld())
    {
        World->GetTimerManager().SetTimerForNextTick([this]()
            {
                BindClickInput();
            });
    }
}

void UCustomTraceManager::Deinitialize()
{
    // 解绑 Viewport 点击输入
    if (UWorld* World = GetGameInstance()->GetWorld())
    {
        if (UGameViewportClient* Viewport = World->GetGameViewport())
            Viewport->OnInputKey().RemoveAll(this);
    }

    Entries.Empty();
    DynamicHandleMap.Empty();
    DynamicClickHandleMap.Empty();
    Super::Deinitialize();
}

void UCustomTraceManager::BindClickInput()
{
    UWorld* World = GetGameInstance()->GetWorld();
    if (!World) return;
    UGameViewportClient* Viewport = World->GetGameViewport();
    if (!Viewport) return;

    Viewport->OnInputKey().AddUObject(this, &UCustomTraceManager::OnViewportInputKey);
}

// ── Viewport 点击回调 ────────────────────────────────────

void UCustomTraceManager::OnViewportInputKey(const FInputKeyEventArgs& EventArgs)
{
    // 只处理鼠标按下
    if (EventArgs.Event != IE_Pressed || !EventArgs.Key.IsMouseButton()) return ;

    for (auto& Pair : Entries)
    {
        FTraceChannelEntry& Entry = Pair.Value;
        if (!Entry.OnClicked.IsBound()) continue;
        if (!Entry.CurrentHitActor)    continue;

        Entry.OnClicked.Broadcast(Entry.CurrentHitActor, EventArgs.Key);
    }

    return ;
}

// ── Static 工具 ──────────────────────────────────────────

UCustomTraceManager* UCustomTraceManager::GetInstance(const UObject* WorldContextObject)
{
    if (!WorldContextObject) return nullptr;
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return nullptr;
    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<UCustomTraceManager>() : nullptr;
}

// ── 悬停：C++ 原生接口 ────────────────────────────────────

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
        // 触发一次 nullptr 广播，让订阅者可以做清理
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

// ── 悬停：Puerts / 蓝图包装实现 ──────────────────────────

void UCustomTraceManager::BP_RegisterChannel(ECollisionChannel Channel)
{
    RegisterChannel(Channel);
}

void UCustomTraceManager::BP_UnregisterChannel(ECollisionChannel Channel)
{
    // 清理该通道下所有悬停动态句柄，防止悬空
    TArray<int32> ToRemove;
    for (auto& Pair : DynamicHandleMap)
        if (Pair.Value.Get<0>() == Channel)
            ToRemove.Add(Pair.Key);
    for (int32 Id : ToRemove)
        DynamicHandleMap.Remove(Id);

    // 同时清理该通道下所有点击动态句柄
    TArray<int32> ClickToRemove;
    for (auto& Pair : DynamicClickHandleMap)
        if (Pair.Value.Get<0>() == Channel)
            ClickToRemove.Add(Pair.Key);
    for (int32 Id : ClickToRemove)
        DynamicClickHandleMap.Remove(Id);

    UnregisterChannel(Channel);
}

int32 UCustomTraceManager::BP_Subscribe(
    ECollisionChannel Channel, const FOnTraceHitChangedDynamic& Delegate)
{
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

// ── 点击：C++ 原生接口 ────────────────────────────────────

FDelegateHandle UCustomTraceManager::SubscribeClick(
    ECollisionChannel Channel, FOnTraceClickChanged::FDelegate&& Delegate)
{
    // 复用悬停 Entry，通道必须已 RegisterChannel
    FTraceChannelEntry& Entry = Entries.FindOrAdd(Channel);
    return Entry.OnClicked.Add(MoveTemp(Delegate));
}

void UCustomTraceManager::UnsubscribeClick(ECollisionChannel Channel, FDelegateHandle Handle)
{
    if (FTraceChannelEntry* Entry = Entries.Find(Channel))
        Entry->OnClicked.Remove(Handle);
}

// ── 点击：Puerts / 蓝图包装实现 ──────────────────────────

int32 UCustomTraceManager::BP_SubscribeClick(
    ECollisionChannel Channel, const FOnTraceClickChangedDynamic& Delegate)
{
    FOnTraceClickChangedDynamic DelegateCopy = Delegate;

    FDelegateHandle Handle = SubscribeClick(Channel,
        FOnTraceClickChanged::FDelegate::CreateLambda(
            [DelegateCopy](AActor* ClickedActor, FKey Key)
            {
                if (DelegateCopy.IsBound())
                    DelegateCopy.Execute(ClickedActor, Key);
            }
        )
    );

    int32 Id = NextSubscribeId++;
    DynamicClickHandleMap.Add(Id, MakeTuple(Channel, Handle));
    return Id;
}

void UCustomTraceManager::BP_UnsubscribeClick(ECollisionChannel Channel, int32 SubscribeId)
{
    if (TTuple<ECollisionChannel, FDelegateHandle>* Found = DynamicClickHandleMap.Find(SubscribeId))
    {
        UnsubscribeClick(Found->Get<0>(), Found->Get<1>());
        DynamicClickHandleMap.Remove(SubscribeId);
    }
}

// ── Tick（悬停检测）─────────────────────────────────────

void UCustomTraceManager::Tick(float DeltaTime)
{
    // 16ms 间隔检测，不需要每帧
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

// ── 默认检测函数 ─────────────────────────────────────────

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
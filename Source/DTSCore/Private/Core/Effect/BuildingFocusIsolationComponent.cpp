#include "Core/Effect/BuildingFocusIsolationComponent.h"

#include "Core/Component/BuildingFloorComponent.h"
#include "Core/Effect/BuildingStencilChannels.h"
#include "Components/PostProcessComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UBuildingFocusIsolationComponent::UBuildingFocusIsolationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    // 项目内置的拆楼隔离后处理材质。仍可在 BuildingManager 蓝图组件上覆盖。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface>
        DefaultIsolationMaterial(
            TEXT("/DTSCore/Material/M_BuildingIsolation_PP.M_BuildingIsolation_PP"));
    if (DefaultIsolationMaterial.Succeeded())
    {
        IsolationMaterial = DefaultIsolationMaterial.Object;
    }

}

void UBuildingFocusIsolationComponent::BeginPlay()
{
    Super::BeginPlay();

    EnsureMaterialInstance();
    SetMaterialAlpha(0.0f);
    SetComponentTickEnabled(false);
}

void UBuildingFocusIsolationComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    DeactivateAllIsolation(true);

    if (IsValid(RuntimePostProcessComponent))
    {
        RuntimePostProcessComponent->DestroyComponent();
        RuntimePostProcessComponent = nullptr;
    }
    IsolationMID = nullptr;

    Super::EndPlay(EndPlayReason);
}

void UBuildingFocusIsolationComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (FMath::IsNearlyEqual(CurrentAlpha, TargetAlpha, KINDA_SMALL_NUMBER))
    {
        CurrentAlpha = TargetAlpha;
        SetMaterialAlpha(CurrentAlpha);

        if (bClearTargetAfterFade && FMath::IsNearlyZero(TargetAlpha))
        {
            RestoreTargetStencil();
            bHasActiveRequest = false;
            bClearTargetAfterFade = false;
        }

        SetComponentTickEnabled(false);
        return;
    }

    if (ActiveFadeTime <= KINDA_SMALL_NUMBER)
    {
        CurrentAlpha = TargetAlpha;
    }
    else
    {
        CurrentAlpha = FMath::FInterpConstantTo(
            CurrentAlpha,
            TargetAlpha,
            DeltaTime,
            1.0f / ActiveFadeTime);
    }

    SetMaterialAlpha(CurrentAlpha);
}

void UBuildingFocusIsolationComponent::ActivateIsolation(
    UBuildingFloorComponent* BuildingComponent)
{
    if (!IsValid(BuildingComponent) || !BuildingComponent->GetFocusSettings().bEnableIsolation)
    {
        DeactivateIsolationByOwner(
            EFocusIsolationOwner::BuildingDisassemble,
            false);
        return;
    }

    FIsolationRequest Request;
    Request.Owner = EFocusIsolationOwner::BuildingDisassemble;
    Request.Priority = 100;
    Request.FadeTime =
        BuildingComponent->GetFocusSettings().IsolationFadeTime;
    Request.bIncludeAttachedActors = true;

    /*
     * 保持现有拆楼视觉效果：拆楼目标临时屏蔽低三位描边通道，
     * 避免某些楼栋子物体被描边材质整面染色。
     * 设备请求不会开启这个选项。
     */
    Request.bClearOutlineChannels = true;

    for (const TPair<TObjectPtr<AActor>, TObjectPtr<UBuildingRuntimeNode>>& Pair :
         BuildingComponent->ActorMap)
    {
        if (IsValid(Pair.Key))
        {
            Request.TargetActors.Add(Pair.Key.Get());
        }
    }

    if (!HasValidTarget(Request))
    {
        DeactivateIsolationByOwner(
            EFocusIsolationOwner::BuildingDisassemble,
            false);
        return;
    }

    IsolationRequests.Add(Request.Owner, MoveTemp(Request));
    RefreshActiveRequest(false);
}

void UBuildingFocusIsolationComponent::DeactivateIsolation(
    const bool bImmediate)
{
    DeactivateIsolationByOwner(
        EFocusIsolationOwner::BuildingDisassemble,
        bImmediate);
}

void UBuildingFocusIsolationComponent::ActivateActorIsolation(
    AActor* TargetActor,
    const EFocusIsolationOwner Owner,
    const int32 Priority,
    const float FadeTime,
    const bool bIncludeAttachedActors)
{
    TArray<AActor*> TargetActors;
    if (IsValid(TargetActor))
    {
        TargetActors.Add(TargetActor);
    }

    ActivateActorsIsolation(
        TargetActors,
        Owner,
        Priority,
        FadeTime,
        bIncludeAttachedActors);
}

void UBuildingFocusIsolationComponent::ActivateActorsIsolation(
    const TArray<AActor*>& TargetActors,
    const EFocusIsolationOwner Owner,
    const int32 Priority,
    const float FadeTime,
    const bool bIncludeAttachedActors)
{
    FIsolationRequest Request;
    Request.Owner = Owner;
    Request.Priority = Priority;
    Request.FadeTime = FadeTime;
    Request.bIncludeAttachedActors = bIncludeAttachedActors;
    Request.bClearOutlineChannels = false;

    for (AActor* TargetActor : TargetActors)
    {
        if (IsValid(TargetActor))
        {
            Request.TargetActors.AddUnique(TargetActor);
        }
    }

    if (!HasValidTarget(Request))
    {
        DeactivateIsolationByOwner(Owner, false);
        return;
    }

    IsolationRequests.Add(Owner, MoveTemp(Request));
    RefreshActiveRequest(false);
}

bool UBuildingFocusIsolationComponent::AddActorToIsolationTargets(
    AActor* TargetActor,
    const EFocusIsolationOwner Owner,
    const int32 Priority,
    const float FadeTime,
    const bool bIncludeAttachedActors)
{
    if (!IsValid(TargetActor))
    {
        return false;
    }

    FIsolationRequest* Request = IsolationRequests.Find(Owner);
    if (!Request)
    {
        FIsolationRequest NewRequest;
        NewRequest.Owner = Owner;
        NewRequest.Priority = Priority;
        NewRequest.FadeTime = FadeTime;
        NewRequest.bIncludeAttachedActors = bIncludeAttachedActors;
        NewRequest.bClearOutlineChannels =
            Owner == EFocusIsolationOwner::BuildingDisassemble;
        IsolationRequests.Add(Owner, MoveTemp(NewRequest));
        Request = IsolationRequests.Find(Owner);
    }

    const bool bAlreadyCached = Request->TargetActors.ContainsByPredicate(
        [TargetActor](const TWeakObjectPtr<AActor>& CachedActor)
        {
            return CachedActor.Get() == TargetActor;
        });
    if (bAlreadyCached)
    {
        return false;
    }

    Request->TargetActors.Add(TargetActor);
    RefreshActiveRequest(false);
    return true;
}

int32 UBuildingFocusIsolationComponent::AddActorsToIsolationTargets(
    const TArray<AActor*>& TargetActors,
    const EFocusIsolationOwner Owner,
    const int32 Priority,
    const float FadeTime,
    const bool bIncludeAttachedActors)
{
    FIsolationRequest* Request = IsolationRequests.Find(Owner);
    int32 AddedCount = 0;

    for (AActor* TargetActor : TargetActors)
    {
        if (!IsValid(TargetActor))
        {
            continue;
        }

        if (!Request)
        {
            FIsolationRequest NewRequest;
            NewRequest.Owner = Owner;
            NewRequest.Priority = Priority;
            NewRequest.FadeTime = FadeTime;
            NewRequest.bIncludeAttachedActors = bIncludeAttachedActors;
            NewRequest.bClearOutlineChannels =
                Owner == EFocusIsolationOwner::BuildingDisassemble;
            IsolationRequests.Add(Owner, MoveTemp(NewRequest));
            Request = IsolationRequests.Find(Owner);
        }

        const bool bAlreadyCached = Request->TargetActors.ContainsByPredicate(
            [TargetActor](const TWeakObjectPtr<AActor>& CachedActor)
            {
                return CachedActor.Get() == TargetActor;
            });

        if (!bAlreadyCached)
        {
            Request->TargetActors.Add(TargetActor);
            ++AddedCount;
        }
    }

    if (AddedCount > 0)
    {
        RefreshActiveRequest(false);
    }

    return AddedCount;
}

bool UBuildingFocusIsolationComponent::RemoveActorFromIsolationTargets(
    AActor* TargetActor,
    const EFocusIsolationOwner Owner)
{
    if (!IsValid(TargetActor))
    {
        return false;
    }

    FIsolationRequest* Request = IsolationRequests.Find(Owner);
    if (!Request)
    {
        return false;
    }

    const int32 RemovedCount = Request->TargetActors.RemoveAll(
        [TargetActor](const TWeakObjectPtr<AActor>& CachedActor)
        {
            return !CachedActor.IsValid() ||
                   CachedActor.Get() == TargetActor;
        });
    if (RemovedCount <= 0)
    {
        return false;
    }

    if (!HasValidTarget(*Request))
    {
        IsolationRequests.Remove(Owner);
    }

    RefreshActiveRequest(false);
    return true;
}

int32 UBuildingFocusIsolationComponent::RemoveActorsFromIsolationTargets(
    const TArray<AActor*>& TargetActors,
    const EFocusIsolationOwner Owner)
{
    FIsolationRequest* Request = IsolationRequests.Find(Owner);
    if (!Request)
    {
        return 0;
    }

    TSet<AActor*> ActorsToRemove;
    for (AActor* TargetActor : TargetActors)
    {
        if (IsValid(TargetActor))
        {
            ActorsToRemove.Add(TargetActor);
        }
    }

    const int32 RemovedCount = Request->TargetActors.RemoveAll(
        [&ActorsToRemove](const TWeakObjectPtr<AActor>& CachedActor)
        {
            return !CachedActor.IsValid() ||
                   ActorsToRemove.Contains(CachedActor.Get());
        });

    if (RemovedCount <= 0)
    {
        return 0;
    }

    if (!HasValidTarget(*Request))
    {
        IsolationRequests.Remove(Owner);
    }

    RefreshActiveRequest(false);
    return RemovedCount;
}

void UBuildingFocusIsolationComponent::ClearIsolationTargets(
    const EFocusIsolationOwner Owner,
    const bool bImmediate)
{
    DeactivateIsolationByOwner(Owner, bImmediate);
}

TArray<AActor*>
UBuildingFocusIsolationComponent::GetIsolationTargetActors(
    const EFocusIsolationOwner Owner) const
{
    TArray<AActor*> Result;
    const FIsolationRequest* Request = IsolationRequests.Find(Owner);
    if (!Request)
    {
        return Result;
    }

    for (const TWeakObjectPtr<AActor>& CachedActor : Request->TargetActors)
    {
        if (AActor* TargetActor = CachedActor.Get(); IsValid(TargetActor))
        {
            Result.AddUnique(TargetActor);
        }
    }

    return Result;
}

FFocusIsolationRequestInfo
UBuildingFocusIsolationComponent::GetActiveIsolationRequest() const
{
    return MakeRequestInfo(FindHighestPriorityRequest());
}

FFocusIsolationRequestInfo
UBuildingFocusIsolationComponent::GetIsolationRequestByOwner(
    const EFocusIsolationOwner Owner) const
{
    return MakeRequestInfo(IsolationRequests.Find(Owner));
}

void UBuildingFocusIsolationComponent::ApplyIsolationRequest(
    const FFocusIsolationRequestInfo& RequestInfo)
{
    ActivateActorsIsolation(
        RequestInfo.TargetActors,
        RequestInfo.Owner,
        RequestInfo.Priority,
        RequestInfo.FadeTime,
        RequestInfo.bIncludeAttachedActors);
}

FFocusIsolationRequestInfo
UBuildingFocusIsolationComponent::MakeRequestInfo(
    const FIsolationRequest* Request) const
{
    FFocusIsolationRequestInfo Result;
    if (!Request || !HasValidTarget(*Request))
    {
        return Result;
    }

    Result.bIsValid = true;
    Result.bIsActive =
        bHasActiveRequest && ActiveOwner == Request->Owner;
    Result.Owner = Request->Owner;
    Result.Priority = Request->Priority;
    Result.FadeTime = Request->FadeTime;
    Result.bIncludeAttachedActors = Request->bIncludeAttachedActors;
    Result.bClearOutlineChannels = Request->bClearOutlineChannels;

    for (const TWeakObjectPtr<AActor>& CachedActor : Request->TargetActors)
    {
        if (AActor* TargetActor = CachedActor.Get(); IsValid(TargetActor))
        {
            Result.TargetActors.AddUnique(TargetActor);
        }
    }

    return Result;
}

void UBuildingFocusIsolationComponent::DeactivateIsolationByOwner(
    const EFocusIsolationOwner Owner,
    const bool bImmediate)
{
    const bool bWasActiveOwner =
        bHasActiveRequest && ActiveOwner == Owner;
    IsolationRequests.Remove(Owner);

    if (bWasActiveOwner)
    {
        RefreshActiveRequest(bImmediate);
    }
}

void UBuildingFocusIsolationComponent::DeactivateAllIsolation(
    const bool bImmediate)
{
    IsolationRequests.Reset();
    RefreshActiveRequest(bImmediate);
}

bool UBuildingFocusIsolationComponent::IsIsolationActive() const
{
    return bHasActiveRequest &&
           (CurrentAlpha > KINDA_SMALL_NUMBER ||
            TargetAlpha > KINDA_SMALL_NUMBER);
}

bool UBuildingFocusIsolationComponent::HasIsolationRequest(
    const EFocusIsolationOwner Owner) const
{
    return IsolationRequests.Contains(Owner);
}

void UBuildingFocusIsolationComponent::RefreshActiveRequest(
    const bool bImmediateWhenEmpty)
{
    const FIsolationRequest* HighestPriorityRequest =
        FindHighestPriorityRequest();

    if (HighestPriorityRequest)
    {
        if (!EnsureMaterialInstance())
        {
            return;
        }

        // 每次请求切换都同步可编辑材质参数。
        ApplyMaterialParameters();

        /*
         * 同一个 Owner 也可能更换目标设备，所以统一恢复旧目标并重新写入。
         * 当设备请求结束而拆楼请求仍存在时，这里会自动重新应用拆楼目标。
         */
        RestoreTargetStencil();
        ApplyTargetStencil(*HighestPriorityRequest);

        ActiveOwner = HighestPriorityRequest->Owner;
        bHasActiveRequest = true;
        ActiveFadeTime = HighestPriorityRequest->FadeTime >= 0.0f
            ? HighestPriorityRequest->FadeTime
            : FMath::Max(0.0f, DefaultFadeTime);
        TargetAlpha = 1.0f;
        bClearTargetAfterFade = false;

        if (ActiveFadeTime <= KINDA_SMALL_NUMBER)
        {
            CurrentAlpha = TargetAlpha;
            SetMaterialAlpha(CurrentAlpha);
            SetComponentTickEnabled(false);
            return;
        }

        SetComponentTickEnabled(true);
        return;
    }

    if (!bHasActiveRequest)
    {
        CurrentAlpha = 0.0f;
        TargetAlpha = 0.0f;
        SetMaterialAlpha(0.0f);
        RestoreTargetStencil();
        bClearTargetAfterFade = false;
        SetComponentTickEnabled(false);
        return;
    }

    if (bImmediateWhenEmpty)
    {
        CurrentAlpha = 0.0f;
        TargetAlpha = 0.0f;
        SetMaterialAlpha(0.0f);
        RestoreTargetStencil();
        bHasActiveRequest = false;
        bClearTargetAfterFade = false;
        SetComponentTickEnabled(false);
        return;
    }

    TargetAlpha = 0.0f;
    bClearTargetAfterFade = true;

    if (ActiveFadeTime <= KINDA_SMALL_NUMBER)
    {
        CurrentAlpha = 0.0f;
        SetMaterialAlpha(0.0f);
        RestoreTargetStencil();
        bHasActiveRequest = false;
        bClearTargetAfterFade = false;
        SetComponentTickEnabled(false);
        return;
    }

    SetComponentTickEnabled(true);
}

const FIsolationRequest*
UBuildingFocusIsolationComponent::FindHighestPriorityRequest() const
{
    const FIsolationRequest* Result = nullptr;

    for (const TPair<EFocusIsolationOwner, FIsolationRequest>& Pair :
         IsolationRequests)
    {
        const FIsolationRequest& Request = Pair.Value;
        if (!HasValidTarget(Request))
        {
            continue;
        }

        if (!Result ||
            Request.Priority > Result->Priority ||
            (Request.Priority == Result->Priority &&
             static_cast<uint8>(Request.Owner) >
                 static_cast<uint8>(Result->Owner)))
        {
            Result = &Request;
        }
    }

    return Result;
}

bool UBuildingFocusIsolationComponent::HasValidTarget(
    const FIsolationRequest& Request) const
{
    for (const TWeakObjectPtr<AActor>& TargetActor :
         Request.TargetActors)
    {
        if (TargetActor.IsValid())
        {
            return true;
        }
    }

    return false;
}

bool UBuildingFocusIsolationComponent::EnsureMaterialInstance()
{
    if (IsValid(IsolationMID))
    {
        return true;
    }

    if (!IsValid(IsolationMaterial))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Building focus isolation is enabled, but IsolationMaterial is not assigned on %s."),
            *GetNameSafe(GetOwner()));
        return false;
    }

    if (!EnsurePostProcessComponent())
    {
        return false;
    }

    IsolationMID = UMaterialInstanceDynamic::Create(IsolationMaterial, this);
    if (!IsValid(IsolationMID))
    {
        return false;
    }

    RuntimePostProcessComponent->AddOrUpdateBlendable(IsolationMID, 1.0f);
    ApplyMaterialParameters();
    return true;
}

bool UBuildingFocusIsolationComponent::EnsurePostProcessComponent()
{
    if (IsValid(RuntimePostProcessComponent))
    {
        return true;
    }

    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return false;
    }

    RuntimePostProcessComponent = NewObject<UPostProcessComponent>(
        Owner,
        TEXT("BuildingFocusIsolationPostProcess"));
    if (!IsValid(RuntimePostProcessComponent))
    {
        return false;
    }

    RuntimePostProcessComponent->bUnbound = true;
    RuntimePostProcessComponent->BlendWeight = 1.0f;
    RuntimePostProcessComponent->Priority = 0.0f;
    Owner->AddInstanceComponent(RuntimePostProcessComponent);
    RuntimePostProcessComponent->RegisterComponent();
    return true;
}

void UBuildingFocusIsolationComponent::ApplyTargetStencil(
    const FIsolationRequest& Request)
{
    TSet<AActor*> TargetActors;
    GatherTargetActors(Request, TargetActors);

    /*
     * 拆楼隔离只接管 Isolation 和既有的通道 1。
     * 设备 Hover(2) 与 Alarm(4) 必须独立保留，确保报警闪烁和悬停描边
     * 在隔离楼栋、退出隔离以及切换隔离目标时都不被清除或回滚。
     */
    const uint8 ManagedStencilMask =
        DTSBuildingStencil::Isolation |
        (Request.bClearOutlineChannels
             ? DTSBuildingStencil::SewerHighlight
             : 0);

    int32 MarkedPrimitiveCount = 0;
    for (AActor* TargetActor : TargetActors)
    {
        if (!IsValid(TargetActor))
        {
            continue;
        }

        TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(TargetActor);
        for (UPrimitiveComponent* Primitive : PrimitiveComponents)
        {
            if (!IsValid(Primitive))
            {
                continue;
            }

            const TWeakObjectPtr<UPrimitiveComponent> PrimitiveKey(Primitive);
            if (!OriginalStencilStates.Contains(PrimitiveKey))
            {
                FPrimitiveStencilState State;
                State.bRenderCustomDepth = Primitive->bRenderCustomDepth;
                State.StencilValue =
                    static_cast<uint8>(Primitive->CustomDepthStencilValue);
                const ERendererStencilMask OriginalWriteMask =
                    Primitive->CustomDepthStencilWriteMask;
                State.StencilWriteMask = static_cast<uint8>(
                    OriginalWriteMask ==
                        ERendererStencilMask::ERSM_255
                        ? DTSBuildingStencil::ResolveWriteMask(
                            State.StencilValue)
                        : OriginalWriteMask);
                State.ManagedStencilMask = ManagedStencilMask;
                OriginalStencilStates.Add(PrimitiveKey, State);
            }

            uint8 ActiveStencilValue =
                static_cast<uint8>(Primitive->CustomDepthStencilValue);
            if (Request.bClearOutlineChannels)
            {
                ActiveStencilValue &=
                    static_cast<uint8>(
                        ~DTSBuildingStencil::SewerHighlight);
            }

            const uint8 NewStencilValue =
                ActiveStencilValue | DTSBuildingStencil::Isolation;
            Primitive->SetCustomDepthStencilWriteMask(
                DTSBuildingStencil::ResolveWriteMask(
                    NewStencilValue));
            Primitive->SetCustomDepthStencilValue(NewStencilValue);
            Primitive->SetRenderCustomDepth(true);
            ++MarkedPrimitiveCount;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("Focus isolation owner=%d marked %d actors / %d primitive components."),
        static_cast<int32>(Request.Owner),
        TargetActors.Num(),
        MarkedPrimitiveCount);
}

void UBuildingFocusIsolationComponent::GatherTargetActors(
    const FIsolationRequest& Request,
    TSet<AActor*>& OutTargetActors) const
{
    for (const TWeakObjectPtr<AActor>& WeakTargetActor :
         Request.TargetActors)
    {
        AActor* TargetActor = WeakTargetActor.Get();
        if (!IsValid(TargetActor))
        {
            continue;
        }

        OutTargetActors.Add(TargetActor);

        if (!Request.bIncludeAttachedActors)
        {
            continue;
        }

        TArray<AActor*> AttachedActors;
        TargetActor->GetAttachedActors(
            AttachedActors,
            true,
            true);
        for (AActor* AttachedActor : AttachedActors)
        {
            if (IsValid(AttachedActor))
            {
                OutTargetActors.Add(AttachedActor);
            }
        }
    }
}

void UBuildingFocusIsolationComponent::RestoreTargetStencil()
{
    for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveStencilState>& Pair : OriginalStencilStates)
    {
        UPrimitiveComponent* Primitive = Pair.Key.Get();
        if (!IsValid(Primitive))
        {
            continue;
        }

        const FPrimitiveStencilState& OriginalState = Pair.Value;
        const uint8 CurrentStencilValue =
            static_cast<uint8>(Primitive->CustomDepthStencilValue);
        const uint8 ManagedStencilMask =
            OriginalState.ManagedStencilMask;
        const uint8 RestoredValue =
            (CurrentStencilValue & ~ManagedStencilMask) |
            (OriginalState.StencilValue & ManagedStencilMask);

        /*
         * 只把隔离请求自己管理的位恢复为进入前状态。
         * 隔离期间新出现的 Hover/Sewer 位属于其他效果，必须继续保留。
         */
        const uint8 OriginalUnmanagedStencil =
            OriginalState.StencilValue & ~ManagedStencilMask;
        const uint8 CurrentUnmanagedStencil =
            CurrentStencilValue & ~ManagedStencilMask;
        const bool bHasNewUnmanagedStencil =
            CurrentUnmanagedStencil != 0 &&
            CurrentUnmanagedStencil != OriginalUnmanagedStencil;

        Primitive->SetCustomDepthStencilValue(RestoredValue);
        Primitive->SetCustomDepthStencilWriteMask(
            bHasNewUnmanagedStencil
                ? DTSBuildingStencil::ResolveWriteMask(RestoredValue)
                : static_cast<ERendererStencilMask>(
                    OriginalState.StencilWriteMask));
        Primitive->SetRenderCustomDepth(
            OriginalState.bRenderCustomDepth || bHasNewUnmanagedStencil);
    }

    OriginalStencilStates.Reset();
}

void UBuildingFocusIsolationComponent::ApplyMaterialParameters()
{
    if (!IsValid(IsolationMID))
    {
        return;
    }

    IsolationMID->SetVectorParameterValue(
        BackgroundColorParameterName,
        BackgroundColor);
    IsolationMID->SetVectorParameterValue(
        HoverOutlineColorParameterName,
        HoverOutlineColor);
    IsolationMID->SetVectorParameterValue(
        AlarmHoverOutlineColorParameterName,
        AlarmHoverOutlineColor);
    IsolationMID->SetVectorParameterValue(
        AlarmBlinkColorParameterName,
        AlarmBlinkColor);
    SetMaterialAlpha(CurrentAlpha);
}

void UBuildingFocusIsolationComponent::SetAlarmHoverActive(
    const bool bAlarmActive)
{
    if (!IsValid(IsolationMID))
    {
        return;
    }

    /*
     * 兼容当前只暴露 HoverOutlineColor 输入的隔离材质。
     * 报警设备悬停时写入报警色，结束悬停后恢复普通色。
     */
    IsolationMID->SetVectorParameterValue(
        HoverOutlineColorParameterName,
        bAlarmActive
            ? AlarmHoverOutlineColor
            : HoverOutlineColor);
}

void UBuildingFocusIsolationComponent::SetMaterialAlpha(const float NewAlpha)
{
    if (IsValid(IsolationMID))
    {
        IsolationMID->SetScalarParameterValue(
            IsolationAlphaParameterName,
            FMath::Clamp(NewAlpha, 0.0f, 1.0f));
    }
}

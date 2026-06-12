#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "CustomTraceManager.generated.h"

/**
 * 自定义射线检测管理器（GameInstance级别单例Subsystem）
 *
 * 功能：
 *   - 支持注册多个碰撞通道，每个通道独立检测
 *   - 每帧Tick自动执行检测，命中变化时广播委托
 *   - 支持自定义检测函数（默认使用鼠标射线）
 *   - 点击检测复用悬停通道的 CurrentHitActor，不做额外射线
 *
 * 调用示例：
 * ─────────────────────────────────────────────
 * // 1. 获取实例
 * UCustomTraceManager* TM = UCustomTraceManager::GetInstance(this);
 *
 * // 2. 注册通道（使用默认鼠标射线检测）
 * TM->RegisterChannel(ECC_GameTraceChannel2);
 *
 * // 3. 注册通道（使用自定义检测函数）
 * TM->RegisterChannel(ECC_GameTraceChannel2,
 *     FTraceDetectFunc::CreateLambda([](APlayerController* PC) -> AActor*
 *     {
 *         return nullptr;
 *     })
 * );
 *
 * // 4. 订阅悬停命中变化
 * FDelegateHandle Handle = TM->Subscribe(ECC_GameTraceChannel2,
 *     FOnTraceHitChanged::FDelegate::CreateUObject(
 *         this, &UMyClass::OnHitChanged)
 * );
 * // 回调签名：void OnHitChanged(AActor* NewHit, AActor* OldHit)
 *
 * // 5. 订阅点击（复用同一通道，点击时取 CurrentHitActor）
 * FDelegateHandle ClickHandle = TM->SubscribeClick(ECC_GameTraceChannel2,
 *     FOnTraceClickChanged::FDelegate::CreateUObject(
 *         this, &UMyClass::OnClicked)
 * );
 * // 回调签名：void OnClicked(AActor* ClickedActor, FKey ButtonPressed)
 *
 * // 6. 取消订阅
 * TM->Unsubscribe(ECC_GameTraceChannel2, Handle);
 * TM->UnsubscribeClick(ECC_GameTraceChannel2, ClickHandle);
 *
 * // 7. 注销通道
 * TM->UnregisterChannel(ECC_GameTraceChannel2);
 *
 * // 8. 主动查询当前命中
 * AActor* Current = TM->GetCurrentHit(ECC_GameTraceChannel2);
 * ─────────────────────────────────────────────
 */

 // ── 悬停委托 ──────────────────────────────────────────────
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraceHitChanged, AActor*, AActor*);
DECLARE_DELEGATE_RetVal_OneParam(AActor*, FTraceDetectFunc, APlayerController*);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnTraceHitChangedDynamic, AActor*, NewHit, AActor*, OldHit);

// ── 点击委托 ──────────────────────────────────────────────
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraceClickChanged, AActor*, FKey);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnTraceClickChangedDynamic, AActor*, ClickedActor, FKey, ButtonPressed);


USTRUCT()
struct FTraceChannelEntry
{
    GENERATED_BODY()

    FTraceDetectFunc     DetectFunc;
    FOnTraceHitChanged   OnHitChanged;
    FOnTraceClickChanged OnClicked;      // 点击委托，复用同一 Entry

    UPROPERTY()
    AActor* CurrentHitActor = nullptr;
};


UCLASS()
class DTSCORE_API UCustomTraceManager : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return !IsTemplate(); }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UCustomTraceManager, STATGROUP_Tickables);
    }

    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager",
        meta = (WorldContext = "WorldContextObject"))
    static UCustomTraceManager* GetInstance(const UObject* WorldContextObject);

    // ── 悬停：C++ 原生接口 ────────────────────────────────
    void RegisterChannel(ECollisionChannel Channel, FTraceDetectFunc DetectFunc = {});
    void UnregisterChannel(ECollisionChannel Channel);
    FDelegateHandle Subscribe(ECollisionChannel Channel, FOnTraceHitChanged::FDelegate&& Delegate);
    void Unsubscribe(ECollisionChannel Channel, FDelegateHandle Handle);
    AActor* GetCurrentHit(ECollisionChannel Channel) const;

    // ── 悬停：Puerts / 蓝图包装接口 ──────────────────────
    /** 注册通道（使用默认鼠标射线检测） */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_RegisterChannel(ECollisionChannel Channel);

    /** 注销通道 */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_UnregisterChannel(ECollisionChannel Channel);

    /**
     * 订阅悬停命中变化，返回订阅 ID（用于取消订阅）
     * @param Channel   目标通道
     * @param Delegate  动态委托回调，签名：void(AActor* NewHit, AActor* OldHit)
     * @return          订阅 ID，传给 BP_Unsubscribe 使用
     */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    int32 BP_Subscribe(ECollisionChannel Channel, const FOnTraceHitChangedDynamic& Delegate);

    /** 取消悬停订阅 */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_Unsubscribe(ECollisionChannel Channel, int32 SubscribeId);

    /** 查询当前命中 Actor */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    AActor* BP_GetCurrentHit(ECollisionChannel Channel) const;

    // ── 点击：C++ 原生接口 ────────────────────────────────
    /**
     * 订阅点击事件，复用悬停通道的 CurrentHitActor，不做额外射线。
     * 通道必须已经 RegisterChannel，点击时若 CurrentHitActor 为空则不广播。
     */
    FDelegateHandle SubscribeClick(ECollisionChannel Channel, FOnTraceClickChanged::FDelegate&& Delegate);
    void UnsubscribeClick(ECollisionChannel Channel, FDelegateHandle Handle);

    // ── 点击：Puerts / 蓝图包装接口 ──────────────────────
    /**
     * 订阅点击事件，返回订阅 ID
     * @param Channel   目标通道（需已 RegisterChannel）
     * @param Delegate  动态委托回调，签名：void(AActor* ClickedActor, FKey ButtonPressed)
     * @return          订阅 ID，传给 BP_UnsubscribeClick 使用
     */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    int32 BP_SubscribeClick(ECollisionChannel Channel, const FOnTraceClickChangedDynamic& Delegate);

    /** 取消点击订阅 */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_UnsubscribeClick(ECollisionChannel Channel, int32 SubscribeId);

private:
    TMap<ECollisionChannel, FTraceChannelEntry> Entries;

    // 悬停动态句柄映射，key = 自增 ID
    int32 NextSubscribeId = 0;
    TMap<int32, TTuple<ECollisionChannel, FDelegateHandle>> DynamicHandleMap;

    // 点击动态句柄映射，key = 自增 ID（与悬停共用 NextSubscribeId）
    TMap<int32, TTuple<ECollisionChannel, FDelegateHandle>> DynamicClickHandleMap;

    static AActor* DefaultDetect(APlayerController* PC, ECollisionChannel Channel);

    float AccumulatedTime = 0.f;

    // 点击输入绑定
    void BindClickInput();
    void OnViewportInputKey(const FInputKeyEventArgs& EventArgs);
};
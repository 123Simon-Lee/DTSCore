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
 *         // 自定义检测逻辑，返回命中的Actor
 *         return nullptr;
 *     })
 * );
 *
 * // 4. 订阅命中变化
 * FDelegateHandle Handle = TM->Subscribe(ECC_GameTraceChannel2,
 *     FOnTraceHitChanged::FDelegate::CreateUObject(
 *         this, &UMyClass::OnHitChanged)
 * );
 * // 回调函数签名：void OnHitChanged(AActor* NewHit, AActor* OldHit)
 *
 * // 5. 取消订阅
 * TM->Unsubscribe(ECC_GameTraceChannel2, Handle);
 *
 * // 6. 注销通道（会触发一次 NewHit=nullptr 的广播）
 * TM->UnregisterChannel(ECC_GameTraceChannel2);
 *
 * // 7. 主动查询当前命中
 * AActor* Current = TM->GetCurrentHit(ECC_GameTraceChannel2);
 * ─────────────────────────────────────────────
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraceHitChanged, AActor*, AActor*);
DECLARE_DELEGATE_RetVal_OneParam(AActor*, FTraceDetectFunc, APlayerController*);
// Puerts/蓝图用动态委托
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnTraceHitChangedDynamic, AActor*, NewHit, AActor*, OldHit);

USTRUCT()
struct FTraceChannelEntry
{
    GENERATED_BODY()
    FTraceDetectFunc DetectFunc;
    FOnTraceHitChanged OnHitChanged;
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

    // ── C++ 原生接口 ──────────────────────────────────────
    void RegisterChannel(ECollisionChannel Channel, FTraceDetectFunc DetectFunc = {});
    void UnregisterChannel(ECollisionChannel Channel);
    FDelegateHandle Subscribe(ECollisionChannel Channel, FOnTraceHitChanged::FDelegate&& Delegate);
    void Unsubscribe(ECollisionChannel Channel, FDelegateHandle Handle);
    AActor* GetCurrentHit(ECollisionChannel Channel) const;

    // ── Puerts / 蓝图包装接口 ─────────────────────────────
    /** 注册通道（使用默认鼠标射线检测） */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_RegisterChannel(ECollisionChannel Channel);

    /** 注销通道 */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_UnregisterChannel(ECollisionChannel Channel);

    /**
     * 订阅命中变化，返回订阅 ID（用于取消订阅）
     * @param Channel   目标通道
     * @param Delegate  动态委托回调，签名：void(AActor* NewHit, AActor* OldHit)
     * @return          订阅 ID，传给 BP_Unsubscribe 使用
     */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    int32 BP_Subscribe(ECollisionChannel Channel, const FOnTraceHitChangedDynamic& Delegate);

    /** 取消订阅 */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    void BP_Unsubscribe(ECollisionChannel Channel, int32 SubscribeId);

    /** 查询当前命中 Actor */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager")
    AActor* BP_GetCurrentHit(ECollisionChannel Channel) const;

private:
    TMap<ECollisionChannel, FTraceChannelEntry> Entries;

    // 动态委托句柄映射表，key = 自增 ID
    int32 NextSubscribeId = 0;
    TMap<int32, TTuple<ECollisionChannel, FDelegateHandle>> DynamicHandleMap;

    static AActor* DefaultDetect(APlayerController* PC, ECollisionChannel Channel);
    float AccumulatedTime = 0.f; 
};
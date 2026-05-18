#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "CustomTraceManager.generated.h"

// 命中变化委托：参数1=新命中Actor，参数2=旧命中Actor（nullptr表示无）
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTraceHitChanged, AActor*, AActor*);
// 检测函数签名：传入PlayerController，返回命中的Actor（nullptr表示未命中）
DECLARE_DELEGATE_RetVal_OneParam(AActor*, FTraceDetectFunc, APlayerController*);

/**
 * 单个通道的检测入口
 * 包含：检测函数、命中变化回调、当前命中Actor
 */
USTRUCT()
struct FTraceChannelEntry
{
    GENERATED_BODY()
    // 该通道使用的检测函数（可自定义，默认为鼠标射线检测）
    FTraceDetectFunc DetectFunc;
    // 命中Actor发生变化时触发
    FOnTraceHitChanged OnHitChanged;
    // 当前帧命中的Actor
    UPROPERTY()
    AActor* CurrentHitActor = nullptr;
};

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
    /**
     * 获取单例实例
     * @param WorldContextObject 任意UObject均可传入，用于获取World
     */
    UFUNCTION(BlueprintCallable, Category = "CustomTraceManager",
        meta = (WorldContext = "WorldContextObject"))
    static UCustomTraceManager* GetInstance(const UObject* WorldContextObject);
    /**
    * 注册碰撞通道
    * @param Channel       要检测的碰撞通道
    * @param DetectFunc    自定义检测函数，不传则使用内置鼠标射线检测
    */
    void RegisterChannel(ECollisionChannel Channel, FTraceDetectFunc DetectFunc = {});
    /**
    * 注销碰撞通道
    * 注销时若有命中Actor，会触发一次 NewHit=nullptr 的广播通知订阅者清理状态
    */
    void UnregisterChannel(ECollisionChannel Channel);
    /**
     * 订阅命中变化事件
     * @param Channel   目标通道
     * @param Delegate  回调委托，签名：void(AActor* NewHit, AActor* OldHit)
     * @return          用于取消订阅的句柄
     */
    FDelegateHandle Subscribe(ECollisionChannel Channel, FOnTraceHitChanged::FDelegate&& Delegate);
    /**
     * 取消订阅
     * @param Channel   目标通道
     * @param Handle    Subscribe 返回的句柄
     */
    void Unsubscribe(ECollisionChannel Channel, FDelegateHandle Handle);
    /**
    * 主动查询当前帧命中的Actor
    * @param Channel   目标通道
    * @return          命中的Actor，未命中返回nullptr
    */
    AActor* GetCurrentHit(ECollisionChannel Channel) const;

private:
    // 通道 → 检测入口 映射表
    TMap<ECollisionChannel, FTraceChannelEntry> Entries;
    /**
    * 内置默认检测：从鼠标位置发射射线，打指定通道
    * 使用复杂碰撞检测（bTraceComplex=true），兼容无简单碰撞体的Mesh
    */
    //注意 简单碰撞检测需要再写一个实现的方法 
    static AActor* DefaultDetect(APlayerController* PC, ECollisionChannel Channel);
};
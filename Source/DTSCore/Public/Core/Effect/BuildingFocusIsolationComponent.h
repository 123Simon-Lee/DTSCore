#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildingFocusIsolationComponent.generated.h"
//拆楼组件
class UBuildingFloorComponent;
//材质接口
class UMaterialInterface;
//材质接口动态实例
class UMaterialInstanceDynamic;
//包含或生成某种几何体的场景组件
class UPrimitiveComponent;
//后期处理组件
class UPostProcessComponent;

/**
 * 场景聚焦隔离请求的来源。
 *
 * 不同来源分别提交和移除自己的请求，避免退出拆楼时误关设备隔离，
 * 或关闭设备隔离时丢失原来的拆楼隔离。
 */
UENUM(BlueprintType)
enum class EFocusIsolationOwner : uint8
{
    //拆楼
    BuildingDisassemble = 0 UMETA(DisplayName = "Building Disassemble"),
    //设备聚焦
    DeviceFocus = 1 UMETA(DisplayName = "Device Focus"),
    //业务特效
    BusinessEffect = 2 UMETA(DisplayName = "Business Effect")
};

struct FPrimitiveStencilState
{
    bool bRenderCustomDepth = false;
    uint8 StencilValue = 0;
    uint8 StencilWriteMask = 0;
    uint8 ManagedStencilMask = 0;
};

/** 组件内部使用的弱引用请求缓存，不直接暴露给蓝图或 TS。 */
struct FIsolationRequest
{
    EFocusIsolationOwner Owner =
        EFocusIsolationOwner::BuildingDisassemble;
    int32 Priority = 0;
    float FadeTime = -1.0f;
    bool bIncludeAttachedActors = true;
    bool bClearOutlineChannels = false;
    TArray<TWeakObjectPtr<AActor>> TargetActors;
};

/**
 * 可供蓝图和 Puerts 读取的隔离请求缓存。
 */
USTRUCT(BlueprintType)
struct DTSCORE_API FFocusIsolationRequestInfo
{
    GENERATED_BODY()

    /** 是否成功取得一个至少包含一个有效目标的请求。 */
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    bool bIsValid = false;

    /** 此请求当前是否为最高优先级并实际生效。 */
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    bool bIsActive = false;

    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    EFocusIsolationOwner Owner =
        EFocusIsolationOwner::BuildingDisassemble;
	//请求优先级，数值越大越优先。若为负数，则使用组件的 DefaultPriority。
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    int32 Priority = 0;
	//隔离淡入淡出时间，单位秒。若为负数，则使用组件的 DefaultFadeTime。
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    float FadeTime = -1.0f;
	//是否包含附加 Actor，默认 true。若为 false，则只隔离 TargetActors 中的 Actor。
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    bool bIncludeAttachedActors = true;
	/// 是否清除所有描边通道，默认 false。若为 true，则隔离时会清除所有目标已有的 Hover 等低位 Stencil 通道。
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    bool bClearOutlineChannels = false;

    /** 请求直接缓存的有效 Actor；不包含附加 Actor 的递归展开结果。 */
    UPROPERTY(BlueprintReadOnly, Category = "Focus Isolation")
    TArray<AActor*> TargetActors;
};

/**
 * 通用场景聚焦隔离效果。
 *
 * 组件仍由 BuildingManager 创建，以保持现有场景资产和材质配置不变，
 * 但目标已经泛化为任意 Actor 集合。拆楼、设备聚焦和业务效果分别以
 * Owner 提交隔离请求，当前显示优先级最高的请求。
 * 需要设置材质球基类的 英文是 Blendable Priority  中文是可混合优先级
 */
UCLASS(ClassGroup = (Building), meta = (BlueprintSpawnableComponent))
class DTSCORE_API UBuildingFocusIsolationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuildingFocusIsolationComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * 隔离后处理材质。
     * 材质应读取 CustomStencil，并暴露 IsolationAlpha 标量参数。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    TObjectPtr<UMaterialInterface> IsolationMaterial = nullptr;

    /** 后处理材质中控制隔离强度的标量参数名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FName IsolationAlphaParameterName = TEXT("IsolationAlpha");

    /** 后处理材质中控制非目标场景颜色的向量参数名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FName BackgroundColorParameterName = TEXT("BackgroundColor");

    /** 非目标场景的隔离颜色，默认压成黑色以突出当前拆楼楼栋。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FLinearColor BackgroundColor = FLinearColor::Black;

    /** 后处理材质中控制普通设备悬停描边颜色的向量参数名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FName HoverOutlineColorParameterName = TEXT("HoverOutlineColor");

    /** 普通设备悬停描边颜色。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FLinearColor HoverOutlineColor = FLinearColor(0.0f, 1.0f, 0.669f, 1.0f);

    /** 后处理材质中控制报警设备悬停描边颜色的向量参数名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FName AlarmHoverOutlineColorParameterName =
        TEXT("AlarmHoverOutlineColor");

    /** 报警设备悬停描边颜色，默认红色。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FLinearColor AlarmHoverOutlineColor = FLinearColor::Red;

    /** 后处理材质中控制报警设备持续闪烁颜色的向量参数名。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FName AlarmBlinkColorParameterName =
        TEXT("AlarmBlinkColor");

    /** 报警设备持续高亮的闪烁颜色，默认红色。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation")
    FLinearColor AlarmBlinkColor = FLinearColor::Red;

    /** 楼栋未单独配置淡入淡出时间时使用的默认值。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus Isolation", meta = (ClampMin = "0.0"))
    float DefaultFadeTime = 0.35f;

    /**
     * 兼容原拆楼入口。
     * 内部转换为 BuildingDisassemble 请求，现有 Manager 调用无需改变。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void ActivateIsolation(UBuildingFloorComponent* BuildingComponent);

    /**
     * 兼容原拆楼退出入口。
     * 只移除 BuildingDisassemble 请求，不影响 DeviceFocus 等其他请求。
     * @param bImmediate 为 true 时立即清理，常用于重新发现/销毁运行时楼栋。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void DeactivateIsolation(bool bImmediate = false);

    /**
     * 激活单个 Actor 的隔离效果。
     * 默认用于设备聚焦，并保留目标已有的 Hover 等低位 Stencil 通道。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void ActivateActorIsolation(
        AActor* TargetActor,
        EFocusIsolationOwner Owner = EFocusIsolationOwner::DeviceFocus,
        int32 Priority = 200,
        float FadeTime = -1.0f,
        bool bIncludeAttachedActors = true);

    /**
     * 使用一组 Actor 激活隔离效果。
     * 本调用会整体覆盖该 Owner 已缓存的目标；后续可通过 Add/Remove 接口增减。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void ActivateActorsIsolation(
        const TArray<AActor*>& TargetActors,
        EFocusIsolationOwner Owner,
        int32 Priority = 200,
        float FadeTime = -1.0f,
        bool bIncludeAttachedActors = true);

    /**
     * 向指定 Owner 的缓存目标中添加单个 Actor。
     * 如果该 Owner 尚未创建请求，则使用传入的配置创建并立即激活请求。
     * @return Actor 有效且此前不在缓存中时返回 true。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    bool AddActorToIsolationTargets(
        AActor* TargetActor,
        EFocusIsolationOwner Owner,
        int32 Priority = 200,
        float FadeTime = -1.0f,
        bool bIncludeAttachedActors = true);

    /**
     * 向指定 Owner 的缓存目标中批量添加 Actor。
     * 无效对象和重复对象会被忽略。
     * @return 实际新增的 Actor 数量。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    int32 AddActorsToIsolationTargets(
        const TArray<AActor*>& TargetActors,
        EFocusIsolationOwner Owner,
        int32 Priority = 200,
        float FadeTime = -1.0f,
        bool bIncludeAttachedActors = true);

    /**
     * 从指定 Owner 的缓存目标中移除单个 Actor。
     * 移除最后一个有效目标时会同时移除该 Owner 的隔离请求。
     * @return 实际移除目标时返回 true。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    bool RemoveActorFromIsolationTargets(
        AActor* TargetActor,
        EFocusIsolationOwner Owner);

    /**
     * 从指定 Owner 的缓存目标中批量移除 Actor。
     * @return 实际移除的缓存项数量。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    int32 RemoveActorsFromIsolationTargets(
        const TArray<AActor*>& TargetActors,
        EFocusIsolationOwner Owner);

    /** 清空指定 Owner 的目标缓存，并移除该 Owner 的隔离请求。 */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void ClearIsolationTargets(
        EFocusIsolationOwner Owner,
        bool bImmediate = false);

    /** 返回指定 Owner 当前缓存的全部有效目标，不包含附加 Actor 的展开结果。 */
    UFUNCTION(BlueprintPure, Category = "Focus Isolation")
    TArray<AActor*> GetIsolationTargetActors(
        EFocusIsolationOwner Owner) const;

    /**
     * 返回当前最高优先级且实际参与隔离的请求快照。
     * 没有有效请求时返回 bIsValid=false 的默认结构。
     */
    UFUNCTION(BlueprintPure, Category = "Focus Isolation")
    FFocusIsolationRequestInfo GetActiveIsolationRequest() const;

    /**
     * 按 Owner 返回请求快照。
     * Owner 不存在或已经没有有效目标时返回 bIsValid=false。
     */
    UFUNCTION(BlueprintPure, Category = "Focus Isolation")
    FFocusIsolationRequestInfo GetIsolationRequestByOwner(
        EFocusIsolationOwner Owner) const;

    /**
     * 将外部修改后的请求快照整体提交回组件。
     * TargetActors 为空时等同于清除该 Owner 的请求。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void ApplyIsolationRequest(
        const FFocusIsolationRequestInfo& RequestInfo);

    /** 只移除指定来源的隔离请求。 */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void DeactivateIsolationByOwner(
        EFocusIsolationOwner Owner,
        bool bImmediate = false);

    /** 清理全部隔离请求，主要用于组件销毁或场景完全重置。 */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void DeactivateAllIsolation(bool bImmediate = false);

    UFUNCTION(BlueprintPure, Category = "Focus Isolation")
    bool IsIsolationActive() const;

    /** 指定来源是否仍持有隔离请求。 */
    UFUNCTION(BlueprintPure, Category = "Focus Isolation")
    bool HasIsolationRequest(EFocusIsolationOwner Owner) const;

    /**
     * 切换当前设备悬停描边颜色。
     *
     * 当前隔离材质只需要保留一个 HoverOutlineColor 参数：
     * 报警设备悬停时临时使用 AlarmHoverOutlineColor，
     * 结束悬停后恢复普通 HoverOutlineColor。
     */
    UFUNCTION(BlueprintCallable, Category = "Focus Isolation")
    void SetAlarmHoverActive(bool bAlarmActive);

private:
    

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> IsolationMID = nullptr;

    /** 真正参与场景渲染的无边界后处理组件，运行时自动创建。 */
    UPROPERTY(Transient)
    TObjectPtr<UPostProcessComponent> RuntimePostProcessComponent = nullptr;

    TMap<EFocusIsolationOwner, FIsolationRequest> IsolationRequests;
    TMap<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveStencilState> OriginalStencilStates;

    EFocusIsolationOwner ActiveOwner =
        EFocusIsolationOwner::BuildingDisassemble;
    bool bHasActiveRequest = false;

    float CurrentAlpha = 0.0f;
    float TargetAlpha = 0.0f;
    float ActiveFadeTime = 0.35f;
    bool bClearTargetAfterFade = false;

    bool EnsureMaterialInstance();
    bool EnsurePostProcessComponent();
    void RefreshActiveRequest(bool bImmediateWhenEmpty = false);
    //拿到现在最高优先级的组件
    const FIsolationRequest* FindHighestPriorityRequest() const;
    bool HasValidTarget(const FIsolationRequest& Request) const;
    void ApplyTargetStencil(const FIsolationRequest& Request);
    void GatherTargetActors(
        const FIsolationRequest& Request,
        TSet<AActor*>& OutTargetActors) const;
    void RestoreTargetStencil();
    void ApplyMaterialParameters();
    void SetMaterialAlpha(float NewAlpha);
    FFocusIsolationRequestInfo MakeRequestInfo(
        const FIsolationRequest* Request) const;
};

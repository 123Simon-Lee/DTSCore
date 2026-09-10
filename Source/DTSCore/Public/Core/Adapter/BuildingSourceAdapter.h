#pragma once

#include "CoreMinimal.h"
#include "Core/DataTable/BuildingFloorConfig.h"
#include "Core/Runtime/BuildingRuntimeData.h"

class UWorld;

/**
 * 数据源的一个可管理建筑。
 *
 * 该结构只描述“发现了哪栋楼”，不包含楼层树和拆楼运行状态。
 * Adapter 在 DiscoverBuildings() 阶段创建它，BuildingManager 随后把它
 * 传给 BuildRuntimeData()，构建真正供拆楼组件使用的运行时数据。
 *
 * BuildingManager 与拆楼组件只消费这个通用描述。
 * 因此这里不能出现 ASpace、Datasmith Actor 等具体数据源类型。
 */
struct DTSCORE_API FBuildingSourceRecord
{
    /**
     * 数据源提供的稳定楼栋 ID，例如 A1、Building_01。
     *
     * 要求：
     * - 在当前场景中唯一；
     * - 多次初始化时保持不变；
     * - 推荐与 FBuildingFloorConfig 数据表的 RowName 对应。
     */
    FString BuildingId;

    /** 供日志、调试和 UI 使用的显示名称，不参与对象身份判断。 */
    FString DisplayName;

    /**
     * 拆楼器实际操作和挂载 UBuildingFloorComponent 的楼栋 Actor。
     * Adapter 只保存弱引用，不负责该 Actor 的创建与销毁。
     */
    TWeakObjectPtr<AActor> BuildingActor;

    /**
     * 可选的原始数据对象。
     *
     * BuildRuntimeData() 可以用它取回数据源特有信息。例如 Space Adapter
     * 在这里保存 ASpace；核心 Manager 和拆楼组件不会读取或转换它的类型。
     */
    TWeakObjectPtr<UObject> SourceObject;
};

/** BuildingManager 传给数据源 Adapter 的通用发现参数。 */
struct DTSCORE_API FBuildingSourceSettings
{
    /** 数据源用于识别楼栋的标签；具体解释方式由 Adapter 决定。 */
    FString BuildingMarker = TEXT("Building");

    /** 数据源用于识别楼层节点的标签；Manager 可以覆盖该默认值。 */
    FString FloorMarker = TEXT("Floor");

    /**
     * DiscoverBuildings() 前是否需要刷新数据源缓存或层级关系。
     * 例如 Space Adapter 可据此重新执行 InitializeSpaceData()。
     */
    bool bRefreshSourceOnDiscover = false;
};

/**
 * 拆楼数据源适配器。
 *
 * Space、ActorTag、DataAsset 或 Datasmith Metadata 都可以实现这个接口，
 * 输出相同的 FBuildingRuntimeData。
 *
 * Adapter 的职责仅有两项：
 * 1. 发现可管理楼栋；
 * 2. 把数据源自己的层级转换成通用 RuntimeNode 树。
 *
 * Adapter 不负责播放动画、处理点击、修改 SceneScope 或保存拆楼状态。
 */
class DTSCORE_API IBuildingSourceAdapter
{
public:
    virtual ~IBuildingSourceAdapter() = default;

    /**
     * 在指定 World 中发现所有可由拆楼器管理的楼栋。
     *
     * 实现要求：
     * - 调用开始时清空 OutBuildings，避免重复初始化；
     * - 忽略无效或即将销毁的对象；
     * - 每个结果必须提供非空 BuildingId 和有效 BuildingActor；
     * - 不要在此阶段构建完整楼层树，完整转换放在 BuildRuntimeData() 中。
     *
     * @param World         当前游戏世界；为空时应直接返回。
     * @param Settings      Manager 提供的发现设置。
     * @param OutBuildings  输出的通用楼栋描述数组。
     */
    virtual void DiscoverBuildings(
        UWorld* World,
        const FBuildingSourceSettings& Settings,
        TArray<FBuildingSourceRecord>& OutBuildings) = 0;

    /**
     * 把一栋楼的源数据转换成拆楼器通用运行时数据。
     *
     * 实现通常需要：
     * - 填充 BuildingId、BuildingActor 和 Config；
     * - 使用 NewObject<UBuildingRuntimeNode>(NodeOuter) 创建 Root 与所有子节点；
     * - 为节点填写 NodeId、RuntimeActor、Children、Tags 和 ControlledActors；
     * - 根据 Settings.FloorMarker 标记楼层，并解析 FloorIndex；
     * - 不在节点中保存对临时对象或栈对象的引用。
     *
     * @param Source      DiscoverBuildings() 返回的单栋楼描述。
     * @param NodeOuter   所有 RuntimeNode 的 Outer，用于保证 UObject 生命周期。
     * @param Settings    Manager 提供的通用数据源设置。
     * @param Config      当前楼栋匹配到的拆楼配置。
     * @param OutRuntime  构建完成的通用运行时数据；失败时不能交给组件使用。
     * @return true 表示数据完整可用；false 表示该楼栋初始化失败。
     */
    virtual bool BuildRuntimeData(
        const FBuildingSourceRecord& Source,
        UObject* NodeOuter,
        const FBuildingSourceSettings& Settings,
        const FBuildingFloorConfig& Config,
        FBuildingRuntimeData& OutRuntime) = 0;
};

/** 创建一个数据源 Adapter 实例的工厂函数类型。每次调用应返回独立实例。 */
using FBuildingSourceAdapterFactory = TFunction<TUniquePtr<IBuildingSourceAdapter>()>;

/**
 * 注册或清除默认数据源 Adapter 工厂。
 *
 * 该函数由独立数据源插件在 StartupModule() 中调用，使 DTSCore 无需链接
 * 任何具体场景模块；插件在 ShutdownModule() 中应传入空工厂解除注册。
 * 后注册的工厂会覆盖之前的默认工厂。
 */
DTSCORE_API void SetDefaultBuildingSourceAdapterFactory(
    FBuildingSourceAdapterFactory InFactory);

/**
 * 使用当前注册的默认工厂创建 Adapter。
 * 未注册工厂时返回 nullptr，调用方必须处理该情况。
 */
DTSCORE_API TUniquePtr<IBuildingSourceAdapter>
CreateDefaultBuildingSourceAdapter();

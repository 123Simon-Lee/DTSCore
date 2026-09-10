// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/TimelineComponent.h"
#include "Core/Runtime/BuildingRuntimeData.h"
#include "BuildingFloorComponent.generated.h"


/**
 * 分层动画完成事件
 * 触发时机：
 * - Drawer模式下楼栋分层展开完成
 * - Drawer模式下楼栋归位完成
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLayeringTimelineFinished);

/**
 * 楼层位移动画完成事件
 *
 * - Drawer：楼层抽出或收回完成
 * - LiftDrop：楼层上下分离或恢复完成
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFloorExpandTimelineFinished);

/**
 * 楼层显隐状态变化事件
 *
 * @param Floor      楼层编号
 * @param bVisible   当前楼层是否可见
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnFloorVisibilityChanged,
	int32,
	Floor,
	bool,
	bVisible
);
/// <summary>
/// Runtime Data
///       |
///Drawer   Visibility
///Hover    Click
/// Animation
/// </summary>
/// <param name=""></param>
///        Struct
/// BuildingFloorComponent
//          │
//          ├── Runtime
//          │     FBuildingRuntimeData
//          │
//          ├── Cache
//          │     FloorMap
//          │     ActorMap
//          │     DeviceMap
//          │
//          ├── Animation
//          │     Timeline
//          │     Curve
//          │
//          ├── Drawer
//          │
//          ├── Visibility
//          │
//          ├── Hover
//          │
//          └── Utility
///**********************************************************************************************************************
/**
 * 建筑拆楼组件
 *
 * 职责：
 * - 接收 BuildingManager 构建好的 FBuildingRuntimeData
 * - 缓存楼层节点、Actor节点
 * - 执行 Drawer 分层 / 抽屉动画
 * - 执行 Visibility 显隐模式
 * - 提供楼层查询、Actor查询、高亮等接口
 *
 * 注意：
 * - 组件不再负责解析 Space
 * - 组件不再直接解析 Datasmith Actor 层级
 * - RuntimeData 由 BuildingManager 构建并传入
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent), Blueprintable)
class DTSCORE_API UBuildingFloorComponent : public UActorComponent
{
	GENERATED_BODY()

	friend class ABuildingManager;
	friend class UBuildingFocusIsolationComponent;

public:

	UBuildingFloorComponent();

protected:

	virtual void BeginPlay() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	// =========================================================
	// Runtime Data
	// =========================================================

	/** BuildingManager 构建好的建筑运行时数据 */
	UPROPERTY()
	FBuildingRuntimeData BuildingRuntimeData;

	// =========================================================
	// Cache
	// =========================================================

	/**
	 * 楼层缓存
	 *
	 * Key   = FloorIndex，例如：-1、1、2、3
	 * Value = 对应的运行时节点
	 */
	UPROPERTY()
	TMap<int32, TObjectPtr<UBuildingRuntimeNode>> FloorMap;

	/**
	 * Actor到运行时节点的反向缓存
	 *
	 * 用途：
	 * - 鼠标点击命中Actor后快速找到对应RuntimeNode
	 * - Hover高亮
	 * - Manager处理交互时使用
	 */
	UPROPERTY()
	TMap<TObjectPtr<AActor>, TObjectPtr<UBuildingRuntimeNode>> ActorMap;

	/**
	 * 所有楼层节点缓存
	 *
	 * 用途：
	 * - Drawer模式遍历楼层
	 * - 计算总高度
	 * - 计算最大平面尺寸
	 * - Timeline批量更新楼层位置
	 */
	UPROPERTY()
	TArray<TObjectPtr<UBuildingRuntimeNode>> FloorNodes;

	/**
	 * 当前隐藏的楼层集合
	 *
	 * 主要用于 Visibility 模式
	 */
	UPROPERTY()
	TSet<int32> HiddenFloors;

	/** 每层按 Name.THRule 识别到的天花 Actor。 */
	TMap<int32, TArray<TWeakObjectPtr<AActor>>> FloorCeilingActorMap;

	/** 每层按 Name.WQRule 识别到的外墙 Actor。 */
	TMap<int32, TArray<TWeakObjectPtr<AActor>>> FloorWallActorMap;

	// =========================================================
	// Timeline
	// =========================================================

	/**
	 * 预留Timeline组件
	 *
	 * 当前主要使用 FTimeline；
	 * 如果后续需要蓝图Timeline组件或调试，可以使用该变量。
	 */
	UPROPERTY()
	TObjectPtr<UTimelineComponent> Timeline = nullptr;

	/** 楼栋分层 / 归位动画 */
	FTimeline LayeringTimeline;

	/** 单层抽屉拉出 / 收回动画 */
	FTimeline FloorExpandTimeline;
	/** LiftDrop 楼层掉落/抬升 Timeline */
	FTimeline LiftDropTimeline;

	/**
	 * Timeline共用曲线
	 *
	 * 优先级：
	 * 1. RuntimeData.Config.Animation.Curve
	 * 2. 构造函数中加载的默认曲线
	 */
	UPROPERTY()
	TObjectPtr<UCurveFloat> RuntimeCurve = nullptr;

	// =========================================================
	// Status
	// =========================================================

	/** Drawer动画是否正在运行 */
	bool bDrawerRunning = false;

	/** Visibility操作是否正在运行 */
	bool bVisibilityRunning = false;

	/** 当前被抽出的楼层，BUILDING_INVALID_FLOOR_INDEX 表示没有楼层被抽出 */
	int32 ActiveFloor = BUILDING_INVALID_FLOOR_INDEX;

	/** 当前是否已经处于分层展开状态 */
	bool bIsLayering = false;

	/** 分层/归位动画是否正在播放 */
	bool bLayeringRunning = false;

	/** 楼层抽屉动画是否正在播放 */
	bool bExpandRunning = false;

	/** 建筑楼层总高度 */
	float TotalBuildingHeight = 0.0f;

	/** 楼层最大X尺寸 */
	float MaxFloorSizeX = 0.0f;

	/** 楼层最大Y尺寸 */
	float MaxFloorSizeY = 0.0f;

	/**
	 * 分层完成后的楼层基准位置
	 *
	 * 抽屉动画以这个位置为基础再叠加横向偏移。
	 */
	TMap<UBuildingRuntimeNode*, FVector> LayeredBaseLocationMap;

	/**
	 * 抽屉动画开始时，每层楼的当前偏移
	 *
	 * 用于在切换抽屉楼层时做平滑过渡。
	 */
	TMap<UBuildingRuntimeNode*, FVector> ExpandStartOffsetMap;


	enum class ELiftDropTransition : uint8
	{
		None,
		DropIn,
		LiftOut
	};

	/** LiftDrop 动画是否正在运行 */
	bool bLiftDropRunning = false;

	/** 当前 LiftDrop 动画类型 */
	ELiftDropTransition LiftDropTransition = ELiftDropTransition::None;

	/** 每个动画楼层的开始位置 */
	TMap<UBuildingRuntimeNode*, FVector> LiftDropStartLocationMap;

	/** 每个动画楼层的目标位置 */
	TMap<UBuildingRuntimeNode*, FVector> LiftDropTargetLocationMap;

	/** 抬升完成后需要隐藏的楼层 */
	TSet<UBuildingRuntimeNode*> LiftDropHideOnFinished;

public:

	// =========================================================
	// Event
	// =========================================================

	/** 分层/归位动画完成事件 */
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnLayeringTimelineFinished OnLayeringTimeLineFinished;

	/** 楼层抽屉动画完成事件 */
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnFloorExpandTimelineFinished OnFloorExpandTimelineFinished;

	/** 楼层显隐变化事件 */
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FOnFloorVisibilityChanged OnFloorVisibilityChanged;

private:

	// =========================================================
	// Runtime Cache
	// =========================================================

	/**
	 * 构建运行时缓存
	 *
	 * 从 BuildingRuntimeData.Root 开始递归遍历，
	 * 填充 FloorMap / ActorMap / FloorNodes / HiddenFloors。
	 */
	void BuildRuntimeCache();

	/**
	 * 递归收集楼层节点与Actor节点
	 *
	 * @param Node 当前遍历的RuntimeNode
	 */
	void CollectFloorNodes(UBuildingRuntimeNode* Node);

	/** 初始化每层 TH/WQ Actor 缓存，仅在组件初始化时执行。 */
	void BuildNamedActorCache();

	/** 按配置规则匹配 Actor 标签或对象名称。 */
	bool MatchesActorNameRule(
		const AActor* Actor,
		const FNameMatchRule& Rule) const;

	void SetNamedActorsHidden(
		const TMap<int32, TArray<TWeakObjectPtr<AActor>>>& ActorMap,
		int32 FloorIndex,
		bool bHidden);

	void RestoreAllFloorCeilings();

	/**
	 * 计算楼层尺寸、高度等数据
	 *
	 * 包括：
	 * - TotalBuildingHeight
	 * - MaxFloorSizeX
	 * - MaxFloorSizeY
	 * - 每个节点的 Bounds
	 */
	void ComputeFloorMetrics();

	/**
	 * 获取节点包围盒
	 *
	 * @param Node 运行时节点
	 * @return 节点RuntimeActor的世界包围盒
	 */
	FBox GetNodeBounds(UBuildingRuntimeNode* Node) const;

	// =========================================================
	// Drawer Internal
	// =========================================================

	/**
	 * 获取抽屉拉出方向
	 *
	 * 根据 Config.Drawer.PullDirection 计算。
	 * Auto 模式下根据 MaxFloorSizeX / MaxFloorSizeY 自动判断。
	 */
	FVector GetPullDirection() const;

	/**
	 * 获取指定楼层的抽屉拉出距离
	 *
	 * 优先使用 FloorPullScaleOverride；
	 * 否则使用 Config.Drawer.PullDistanceScale。
	 *
	 * @param Node 楼层节点
	 */
	float GetPullDistance(UBuildingRuntimeNode* Node) const;

	/**
	 * Drawer模式下切换节点抽屉状态
	 *
	 * 如果该楼层未展开：抽出
	 * 如果该楼层已展开：收回
	 * 如果其他楼层已展开：旧楼层收回，新楼层抽出
	 */
	void DrawerNode(UBuildingRuntimeNode* Node);

	/**
	 * 播放楼层抽屉动画
	 */
	void PlayFloorExpandTimeline();

	/**
	 * 播放楼栋分层/归位动画
	 *
	 * @param bForward true=分层展开，false=归位
	 */
	void PlayLayeringTimeline(bool bForward);

	// =========================================================
	// Visibility Internal
	// =========================================================

	/**
	 * Visibility模式下切换节点显隐状态
	 *
	 * @param Node 目标节点
	 */
	void VisibilityNode(UBuildingRuntimeNode* Node);

	/**
	 * 设置节点及其子节点显隐
	 *
	 * @param Node     目标节点
	 * @param bVisible 是否显示
	 */
	void ApplyNodeVisibility(UBuildingRuntimeNode* Node, bool bVisible);

	/**
	 * 设置指定楼层显隐
	 *
	 * @param Floor    楼层编号
	 * @param bVisible 是否显示
	 */
	void ApplyFloorVisibility(int32 Floor, bool bVisible);





public:

	// =========================================================
	// Initialize
	// =========================================================

	/**
	 * 初始化拆楼组件
	 *
	 * 由 BuildingManager 调用。
	 * 会缓存 RuntimeData，构建查询缓存，初始化Transform，并初始化Timeline。
	 *
	 * @param InRuntimeData BuildingManager构建好的运行时数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Initialize")
	void Initialize(const FBuildingRuntimeData& InRuntimeData);

	/**
	 * 初始化所有RuntimeNode的Transform
	 *
	 * 会记录：
	 * - OriginalTransform
	 * - CurrentTransform
	 * - CurrentOffset
	 * - TargetOffset
	 */
	UFUNCTION(BlueprintCallable, Category = "Initialize")
	void InitializeTransforms();

	/**
	 * 初始化Timeline
	 *
	 * 设置：
	 * - 分层Timeline
	 * - 抽屉Timeline
	 * - Timeline回调函数
	 */
	UFUNCTION(BlueprintCallable, Category = "Initialize")
	void InitializeTimelines();

	// =========================================================
	// Drawer
	// =========================================================

	/**
	 * 开始楼栋分层展示
	 *
	 * 仅 Drawer 模式有效。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drawer")
	void LayeringDisplay();

	/**
	 * 楼栋归位
	 *
	 * 会将所有楼层恢复到初始位置。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drawer")
	void BackToNormal();

	/**
	 * 切换指定楼层抽屉状态
	 *
	 * @param Floor 楼层编号
	 */
	UFUNCTION(BlueprintCallable, Category = "Drawer")
	void ToggleFloor(int32 Floor);

	/**
	 * 切换指定节点
	 *
	 * 根据当前 DisassembleMode 自动分发到：
	 * - DrawerNode
	 * - VisibilityNode
	 *
	 * @param Node 目标运行时节点
	 */
	UFUNCTION(BlueprintCallable, Category = "Drawer")
	void ToggleNode(UBuildingRuntimeNode* Node);

	/**
	 * 强制重置拆楼状态
	 *
	 * 会停止所有动画，并恢复所有节点Transform和显隐状态。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drawer")
	void Reset();
	//是否是Drawer模式
	UFUNCTION(BlueprintPure, Category = "Runtime")
	bool IsDrawerDisassembleMode() const;

	// =========================================================
	// Visibility
	// =========================================================



	 /** 进入 Visibility 拆楼模式 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void EnterVisibilityDisassemble();

	/** 退出 Visibility 拆楼模式 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void ExitVisibilityDisassemble();

	/** 设置整栋楼显示隐藏 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void SetWholeBuildingVisibility(bool bVisible);

	/**
	 * 楼栋隔离显隐：隐藏时处理整棵 Actor 树；显示时只恢复
	 * Space 的 OrdinaryActors 和 Structs，其他附属 Actor 保持隐藏。
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void SetIsolationBuildingVisibility(bool bVisible);

	/** 当前组件是否是 Visibility 拆楼模式 */
	UFUNCTION(BlueprintPure, Category = "Visibility")
	bool IsVisibilityDisassembleMode() const;

	/** Visibility模式下是否隐藏其他楼栋 */
	UFUNCTION(BlueprintPure, Category = "Visibility")
	bool ShouldHideOtherBuildingsOnVisibilityEnter() const;

	/** Visibility模式退出时是否恢复其他楼栋 */
	UFUNCTION(BlueprintPure, Category = "Visibility")
	bool ShouldRestoreOtherBuildingsOnVisibilityExit() const;

	/** 获取当前组件对应的建筑 Actor。 */
	UFUNCTION(BlueprintPure, Category = "Runtime")
	AActor* GetBuildingActor() const;

	/** 获取数据源提供的稳定建筑 ID。 */
	UFUNCTION(BlueprintPure, Category = "Runtime")
	FString GetBuildingId() const;

	/**
	 * 计算当前楼栋的聚焦目标。
	 * 只使用 RuntimeData 中的楼层与受控 Actor，不递归场景中的无关附加节点。
	 */
	UFUNCTION(BlueprintPure, Category = "Focus")
	FBuildingFocusTarget GetBuildingFocusTarget() const;

	/** 计算指定楼层当前世界位置的聚焦目标。 */
	UFUNCTION(BlueprintPure, Category = "Focus")
	FBuildingFocusTarget GetFloorFocusTarget(int32 FloorIndex) const;

	/**
	 * 只使用本层 WQRule 识别的外墙 Actor 计算楼层聚焦目标。
	 * 相机机械臂距离按外墙轮廓自适应，TH 天花不参与计算。
	 * 本层没有匹配到 WQ 时自动回退到整层聚焦。
	 */
	UFUNCTION(BlueprintPure, Category = "Focus")
	FBuildingFocusTarget GetFloorWallFocusTarget(int32 FloorIndex) const;

	/** 获取指定 FloorIndex 的聚焦配置；未配置覆盖时返回楼栋默认 Focus。 */
	UFUNCTION(BlueprintPure, Category = "Focus")
	FBuildingFocusSettings GetFloorFocusSettings(int32 FloorIndex) const;

	/** 返回指定楼层中按 THRule 识别到的天花 Actor。 */
	UFUNCTION(BlueprintPure, Category = "Name")
	TArray<AActor*> GetFloorCeilingActors(int32 FloorIndex) const;

	/** 返回指定楼层中按 WQRule 识别到的外墙 Actor。 */
	UFUNCTION(BlueprintPure, Category = "Name")
	TArray<AActor*> GetFloorWallActors(int32 FloorIndex) const;

	/** 设置指定楼层中 TH Actor 的显隐。 */
	UFUNCTION(BlueprintCallable, Category = "Name")
	void SetFloorCeilingHidden(int32 FloorIndex, bool bHidden);

	/** 获取当前楼栋的相机聚焦配置。Drawer、Visibility、LiftDrop 统一使用楼栋级 Focus。 */
	const FBuildingFocusSettings& GetFocusSettings() const
	{
		return BuildingRuntimeData.Config.Focus;
	}

	/**
	 * 显示指定楼层及其以下楼层，隐藏其以上楼层
	 *
	 * @param Floor 目标楼层
	 */

	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void ToggleFloorVisibility(int32 Floor);

	/**
	 * 只显示指定楼层
	 *
	 * @param Floor 目标楼层
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void ShowOnlyFloor(int32 Floor);

	/**
	 * 显示指定楼层范围
	 *
	 * @param MinFloor 最小楼层
	 * @param MaxFloor 最大楼层
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void ShowFloorRange(int32 MinFloor, int32 MaxFloor);

	/**
	 * 显示所有楼层
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void ShowAll();

	/**
	 * 隐藏指定楼层以上的所有楼层
	 *
	 * @param Floor 基准楼层
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void HideAbove(int32 Floor);

	/**
	 * 设置所有楼层显隐
	 *
	 * @param bVisible true=显示，false=隐藏
	 */
	UFUNCTION(BlueprintCallable, Category = "Visibility")
	void SetAllFloorsVisibility(bool bVisible);

	/**
	 * 查询指定楼层是否可见
	 *
	 * @param Floor 楼层编号
	 * @return true=可见，false=隐藏
	 */
	UFUNCTION(BlueprintPure, Category = "Visibility")
	bool IsFloorVisible(int32 Floor) const;

	// =========================================================
	// Query
	// =========================================================

	/**
	 * 根据楼层编号查找楼层RuntimeNode
	 *
	 * @param FloorIndex 楼层编号
	 * @return 找到返回节点，否则返回 nullptr
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	UBuildingRuntimeNode* FindFloorWithIndex(int32 FloorIndex) const;

	/**
	 * 根据稳定 ID、Actor Label 或带楼栋前缀的楼层 ID 查找楼层。
	 * 例如 1F 可以匹配 A1_1F，B1F 可以匹配 A1_B1F。
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	UBuildingRuntimeNode* FindFloorWithId(const FString& FloorId) const;

	/** 当前可拆楼层数量；业务层不再依赖 ABuilding::bSingleStorey。 */
	UFUNCTION(BlueprintPure, Category = "Query")
	int32 GetFloorCount() const { return FloorNodes.Num(); }

	/** 获取楼层编号最大的 RuntimeNode，供 RF 无独立节点时兜底。 */
	UFUNCTION(BlueprintPure, Category = "Query")
	UBuildingRuntimeNode* GetHighestFloorNode() const;

	/**
	 * 根据Actor查找RuntimeNode
	 *
	 * 支持命中子Actor时向父级回溯。
	 *
	 * @param Actor 命中的Actor
	 * @return 找到返回节点，否则返回 nullptr
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	UBuildingRuntimeNode* FindNodeWithActor(AActor* Actor) const;

	/**
	 * 获取当前抽出的楼层
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	int32 GetActiveFloor() const { return ActiveFloor; }

	/**
	 * 当前是否处于分层状态
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	bool IsLayering() const { return bIsLayering; }

	/**
	 * 当前分层动画是否运行中
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	bool IsLayeringRunning() const { return bLayeringRunning; }

	/**
	 * 当前抽屉动画是否运行中
	 */
	UFUNCTION(BlueprintPure, Category = "Query")
	bool IsExpandRunning() const { return bExpandRunning; }

	//是否能拆楼
	UFUNCTION(BlueprintPure, Category = "Runtime")
	bool CanDisassembleBuilding() const;



	// =========================================================
	// Effect / Highlight
	// =========================================================

	/**
	 * 设置节点高亮
	 *
	 * @param Node       目标节点
	 * @param bHighlight true=开启高亮，false=关闭高亮
	 */
	UFUNCTION(BlueprintCallable, Category = "Highlight")
	void SetNodeHighlight(UBuildingRuntimeNode* Node, bool bHighlight);

	/** 进入 LiftDrop 模式，先显示整栋楼等待选层 */
	UFUNCTION(BlueprintCallable, Category = "LiftDrop")
	void EnterLiftDropDisassemble();

	/** 选择 LiftDrop 目标楼层 */
	UFUNCTION(BlueprintCallable, Category = "LiftDrop")
	bool SelectLiftDropFloor(int32 TargetFloor);

	/** 退出 LiftDrop，恢复全部楼层 */
	UFUNCTION(BlueprintCallable, Category = "LiftDrop")
	void ExitLiftDropDisassemble();

	UFUNCTION(BlueprintPure, Category = "LiftDrop")
	bool IsLiftDropDisassembleMode() const;

	UFUNCTION(BlueprintPure, Category = "LiftDrop")
	bool IsLiftDropRunning() const
	{
		return bLiftDropRunning;
	}
private:

	/** 根据一个或多个 Runtime 子树计算聚焦 Bounds。 */
	FBuildingFocusTarget BuildFocusTarget(
		const TArray<UBuildingRuntimeNode*>& FocusRoots,
		AActor* FallbackActor) const;

	/** 从给定 Actor 列表（含各自附着子树）汇总聚焦 Bounds。 */
	FBuildingFocusTarget BuildFocusTargetFromActors(
		const TArray<AActor*>& InActors,
		AActor* FallbackActor) const;

	// =========================================================
	// Timeline Callback
	// =========================================================

	/**
	 * 分层动画更新
	 *
	 * @param Alpha Timeline插值，范围通常为0~1
	 */
	UFUNCTION()
	void OnLayeringUpdate(float Alpha);

	/**
	 * 分层动画结束
	 */
	UFUNCTION()
	void OnLayeringFinished();

	/**
	 * 楼层抽屉动画更新
	 *
	 * @param Alpha Timeline插值，范围通常为0~1
	 */
	UFUNCTION()
	void OnFloorExpandUpdate(float Alpha);

	/**
	 * 楼层抽屉动画结束
	 */
	UFUNCTION()
	void OnFloorExpandFinished();

	void SetNodeTreeVisibility(UBuildingRuntimeNode* Node, bool bVisible);

	/** 楼层隔离显示 */
	void SetNodeStructuralTreeVisibility(
		UBuildingRuntimeNode* Node,
		bool bVisible);

	/** 楼栋隔离显示专用：只恢复 Space 管理的普通模型和空间结构。 */
	void SetNodeIsolationContentVisibility(
		UBuildingRuntimeNode* Node,
		bool bVisible);

	//普通情况使用 
	void SetActorTreeHidden(AActor* Actor, bool bHidden);


	void PlayLiftDropTimeline();

	UFUNCTION()
	void OnLiftDropUpdate(float Alpha);

	UFUNCTION()
	void OnLiftDropFinished();


};

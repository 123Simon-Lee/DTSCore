// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include <Components/TimelineComponent.h>
#include "BuildingFloorConfig.h"
#include "BuildingFloorComponent.generated.h"



DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLayeringTimelineFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFloorExpandTimelineFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFloorVisibilityChanged, int32, Floor, bool, bVisible);


USTRUCT()
struct FFloorTHList
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<AActor*> Actors;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent), Blueprintable)
class DTSCORE_API UBuildingFloorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBuildingFloorComponent();

protected:
    virtual void BeginPlay() override;

public:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    int32 ParseFloorFromName(const FString& Name);

    // =========================================================
    //  【通用】必须设置
    // =========================================================

    /** 【必须】DataSmith 导入的建筑根节点 Actor，组件所有解析逻辑都以此为起点 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "通用|必须设置")
    AActor* DataSmithActor;

    /** 【必须】拆楼交互模式：Drawer = 抽屉位移分层；Visibility = 显隐切换 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "通用|必须设置")
    EDisassembleMode DisassembleMode = EDisassembleMode::Drawer;

    // =========================================================
    //  【通用】可不设置
    // =========================================================

    /** 动画曲线，控制分层/抽屉动画的插值节奏，不设置则无动画 */
    UPROPERTY(EditAnywhere, Category = "通用|可不设置")
    UCurveFloat* Curve;

    // =========================================================
    //  【通用】运行时计算（只读）
    // =========================================================

    /** 楼层 Actor 与楼层号的映射，由 ParseFloors 自动填充 */
    UPROPERTY(BlueprintReadOnly, Category = "通用|运行时计算")
    TMap<int32, AActor*> FloorMap;

    /** Actor 到楼层号的反向映射 */
    UPROPERTY(BlueprintReadOnly, Category = "通用|运行时计算")
    TMap<AActor*, int32> ActorToFloorMap;

    /** 每层楼楼板底部的 Z 高度（相对值），由 ParseFloors 计算 */
    UPROPERTY(BlueprintReadWrite, Category = "通用|运行时计算")
    TMap<int32, float> FloorZMap;

    /** 楼层号到楼层高度的映射（Z 方向尺寸），由 ParseFloors 计算 */
    UPROPERTY( BlueprintReadOnly, Category = "通用|运行时计算")
    TMap<int32, float> FloorHeightMap;

    /** 建筑所有楼层的总高度（最低层底到最高层底的 Z 差值） */
    UPROPERTY(BlueprintReadOnly, Category = "通用|运行时计算")
    float TotalBuildingHeight = 0.f;

    /** 当前处于隐藏状态的楼层集合 */
    UPROPERTY(BlueprintReadOnly, Category = "通用|运行时计算")
    TSet<int32> HiddenFloors;

    /** 无楼层归属的散装 Actor，只参与整体抬升，不参与点击/抽屉 */
    UPROPERTY(BlueprintReadOnly, Category = "通用|运行时计算")
    TArray<AActor*> StandaloneActors;

    // =========================================================
    //  【通用】委托
    // =========================================================

    /** 分层/归位动画结束时广播 */
    UPROPERTY(BlueprintAssignable, Category = "通用|委托")
    FOnLayeringTimelineFinished OnLayeringTimeLineFinished;

    /** 楼层抽屉动画结束时广播 */
    UPROPERTY(BlueprintAssignable, Category = "通用|委托")
    FOnFloorExpandTimelineFinished OnFloorExpandTimelineFinished;

    /** 楼层显隐状态变化时广播（Floor = 楼层号，bVisible = 是否可见） */
    UPROPERTY(BlueprintAssignable, Category = "通用|委托")
    FOnFloorVisibilityChanged OnFloorVisibilityChanged;

    // =========================================================
    //  【Drawer 模式】必须设置
    // =========================================================

    // （无，DataSmithActor 已在通用组设置）

    // =========================================================
    //  【Drawer 模式】可不设置
    // =========================================================

    /** 分层时楼层抬升的基准高度。
     *  设置为园区内最高楼的总高可保证所有楼栋分层高度一致。
     *  为 0 则自动使用当前楼栋的 TotalBuildingHeight */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer 模式|可不设置")
    float CampusMaxBuildingHeight = 0.f;

    /** 分层后各楼层之间的间距系数，值越大层间距越宽，默认 1.2 为0则不分层 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer 模式|可不设置")
    float SpreadScale = 1.2f;

    /** 抽屉拉出方向，Auto 时根据建筑平面长边自动判断 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer 模式|可不设置")
    EPullDirection PullDirection = EPullDirection::Auto;

    /** 抽屉拉出距离的缩放系数，最终距离 = 建筑平面宽度 × 此值，默认 1.5 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer 模式|可不设置")
    float PullDistanceScale = 1.5f;

    /** 手动指定楼层平面 X 方向尺寸（单位：cm）。
     *  不为 0 时覆盖自动计算结果，适用于自动计算异常的情况 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Drawer 模式|可不设置")
    float MaxFloorSizeX = 0.f;

    /** 手动指定楼层平面 Y 方向尺寸（单位：cm）。
     *  不为 0 时覆盖自动计算结果，适用于自动计算异常的情况 */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Drawer 模式|可不设置")
    float MaxFloorSizeY = 0.f;

    /** 单独覆盖某楼层的拉出距离缩放系数。
 *  Key = 楼层号，Value = 该层专用缩放值。
 *  不在此 Map 中的楼层使用 PullDistanceScale */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer 模式|可不设置")
    TMap<int32, float> FloorPullScaleOverride;
    // =========================================================
    //  【Drawer 模式】运行时计算（只读）
    // =========================================================

    /** 分层完成后各楼层的世界坐标快照，抽屉位移以此为基准 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    TMap<int32, FVector> LayeredLoc;

    /** 每层楼当前帧的横向偏移量（动画过渡用） */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    TMap<int32, FVector> NowOffsetMap;

    /** 每层楼的目标横向偏移量 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    TMap<int32, FVector> TargetOffsetMap;

    /** 当前正在抽出的楼层号，0 表示无楼层被抽出 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    int32 ActiveFloor = 0;

    /** 分层/归位动画是否正在播放 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    bool bLayeringRunning = false;

    /** 楼层展开收起动画是否正在播放 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    bool bExpandRunning = false;
    
    /** Drawer 模式下所有楼层的 TH Actor 列表，Key=楼层号 */
    UPROPERTY()
    TMap<int32, FFloorTHList> FloorTHMap;

    /** Drawer 模式下散装 Actor 的 TH 列表 */
    UPROPERTY(BlueprintReadOnly, Category = "Drawer 模式|运行时计算")
    TArray<AActor*> StandaloneTHActors;

    // =========================================================
    //  【Visibility 模式】运行时计算（只读）
    // =========================================================

    /** TH（透明辅助层）Actor 列表，由 ParseFloors 自动收集 */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    TArray<AActor*> THActors;

    /** WQ（外墙）Actor 列表，由 ParseFloors 自动收集 */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    TArray<AActor*> WQActors;

    /** 建筑总高度，由所有 TH 包围盒合并后的 Z 尺寸算出 */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    float BuildingTotalHeight = 0.f;

    /** Actor 到 TH 中心点的映射 */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    TMap<AActor*, FVector> THCenterMap;

    /** 楼层号到该层 TH 中心点的映射，用于摄像机定位 */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    TMap<int32, FVector> FloorTHCenterMap;

    /** 显隐模式下当前聚焦的楼层 Actor */
    UPROPERTY(BlueprintReadOnly, Category = "Visibility 模式|运行时计算")
    AActor* TargetBuilding;

    // =========================================================
    //  【通用】蓝图方法
    // =========================================================
    /** 从 DataTable 配置行初始化组件参数 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void InitFromConfig( const FBuildingFloorConfig& Config);
    
    /** 解析 DataSmithActor 子层级，填充 FloorMap / FloorHeightMap 等所有数据，使用前必须调用一次 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void ParseFloors();

    /** 开始楼栋分层动画（Drawer 模式下楼层向上展开） */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void LayeringDisplay();

    /** 楼栋归位，恢复分层前的状态 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void BackToNormal();

    /** 隐藏指定楼层列表 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void HideFloors(const TArray<int32>& Floors);

    /** 显示指定楼层列表 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void ShowFloors(const TArray<int32>& Floors);

    /** 隐藏指定楼层以上的所有楼层 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void HideAbove(int32 Floor);

    /** 显示全部楼层 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void ShowAll();

    /** 设置所有楼层的可见性 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    void SetAllFloorsVisibility(bool bVisible);

    /** 将楼层字符串（如"3F"、"B1F"）转为楼层号整数 */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    int32 ParseFloorString(const FString& FloorStr);

    /** 将 Visibility 模式楼层名解析为楼层号（支持 RF、BxF、xF 格式） */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    int32 ParseVisibilityFloorName(const FString& Name);

    /** 获取地面 Z 高度（向下射线检测） */
    UFUNCTION(BlueprintCallable, Category = "通用|方法")
    float GetGroundZ();

    // =========================================================
    //  【Drawer 模式】蓝图方法
    // =========================================================

    /** 抽出或归位某一楼层（分层状态下有效）。
     *  同一楼层再次调用则归位，切换到新楼层则先归位上一层再抽出新层 */
    UFUNCTION(BlueprintCallable, Category = "Drawer 模式|方法")
    void ToggleFloor(int32 Floor);

    /** 强制停止所有动画并将楼层归位到当前状态的基准位置 */
    UFUNCTION(BlueprintCallable, Category = "Drawer 模式|方法")
    void ForceReset();

    /** 手动重新计算 Visibility 模式的 FloorZMap（通常由 ParseFloors 自动调用） */
    UFUNCTION(BlueprintCallable, Category = "Drawer 模式|方法")
    void CalculateFloorHeight();

    /** 根据分层状态同步 TH 层的显隐（bLayering = true 时只显示顶层 TH） */
    UFUNCTION(BlueprintCallable, Category = "Drawer 模式|方法")
    void UpdateTHVisibilityForDrawer(bool bLayering);

    // =========================================================
    //  【Visibility 模式】蓝图方法
    // =========================================================

    /** 显示该楼层及以下所有楼层，隐藏以上楼层，并显示对应 TH 层 */
    UFUNCTION(BlueprintCallable, Category = "Visibility 模式|方法")
    void ToggleFloorVisibility(int32 Floor);

    /** 只显示指定楼层，其余全部隐藏 */
    UFUNCTION(BlueprintCallable, Category = "Visibility 模式|方法")
    void ShowOnlyFloor(int32 Floor);

    /** 只显示 [MinFloor, MaxFloor] 范围内的楼层，其余隐藏 */
    UFUNCTION(BlueprintCallable, Category = "Visibility 模式|方法")
    void ShowFloorRange(int32 MinFloor, int32 MaxFloor);

    /** 查询某楼层是否当前可见 */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Visibility 模式|方法")
    bool IsFloorVisible(int32 Floor) const;

    /** 获取指定楼层 TH 中心点（用于摄像机定位），找不到时返回最近楼层的值 */
    UFUNCTION(BlueprintCallable, Category = "Visibility 模式|方法")
    FVector GetFloorTHCenter(int32 Floor) const;

    /** 根据 FOV 计算摄像机观察指定楼层所需的 ArmLength */
    UFUNCTION(BlueprintCallable, Category = "Visibility 模式|方法")
    float GetArmLengthForFloorByFOV(int32 Floor, float FOV, float ZoomScale) const;

private:

    /** 楼栋分层动画的 Timeline 实例（PlayFromStart = 展开，ReverseFromEnd = 归位） */
    FTimeline LayeringTimeline;

    /** 单层抽屉动画的 Timeline 实例 */
    FTimeline FloorExpandTimeline;

    /** 每层楼的初始世界坐标快照，ParseFloors 时记录，归位动画以此为终点 */
    UPROPERTY()
    TMap<int32, FVector> OriginLoc;

    /** 散装 Actor 及其所有子节点的初始坐标快照，归位时使用 */
    UPROPERTY()
    TMap<AActor*, FVector> StandaloneOriginLocMap;

    /** 当前是否处于分层展开状态（true = 已展开，false = 已归位） */
    UPROPERTY()
    bool bIsLayering = false;

    /** LayeringTimeline 每帧回调，根据插值值驱动各楼层 Z 轴位移 */
    UFUNCTION()
    void OnLayeringUpdate(float Value);

    /** LayeringTimeline 播放结束回调，更新 bIsLayering 状态并记录 LayeredLoc 快照 */
    UFUNCTION()
    void OnLayeringFinish();

    /** FloorExpandTimeline 每帧回调，根据插值值驱动目标楼层横向位移 */
    UFUNCTION()
    void OnFloorExpandUpdate(float Value);

    /** FloorExpandTimeline 播放结束回调，广播 OnFloorExpandTimelineFinished */
    UFUNCTION()
    void OnFloorExpandFinish();

    /** 内部统一执行楼层显隐，同步更新 HiddenFloors 集合并处理 TH 层显隐逻辑
     *  @param Floor               目标楼层号
     *  @param bVisible            是否可见
     *  @param CurrentToggleFloor  当前操作的最高楼层号，用于判断 TH 层是否显示（-1 = 强制隐藏所有 TH）*/
    void ApplyFloorVisibility(int32 Floor, bool bVisible, int32 CurrentToggleFloor);
};
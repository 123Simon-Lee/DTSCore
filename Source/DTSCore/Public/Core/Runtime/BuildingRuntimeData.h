#pragma once

#include "CoreMinimal.h"
#include "Core/DataTable/BuildingFloorConfig.h"
#include "BuildingRuntimeData.generated.h"

class AActor;

static constexpr int32 BUILDING_INVALID_FLOOR_INDEX = -999999;
/**
 * 建筑运行时节点
 *
 * RuntimeData 是 USTRUCT，
 * RuntimeNode 是 UObject。
 *
 * FBuildingRuntimeData
 *      |
 *      Root
 *      |
 * UBuildingRuntimeNode
 *      |
 *      Children
 */
//通用拆楼数据
UCLASS(BlueprintType)
class DTSCORE_API UBuildingRuntimeNode : public UObject
{
    GENERATED_BODY()

public:

    //----------------------------
    // Source/Actor
    //----------------------------

    /** 数据源中的稳定节点 ID，例如 1F、B1F */
    UPROPERTY(BlueprintReadOnly)
    FString NodeId;

    /** 可选的原始数据对象，只用于业务侧追溯，拆楼核心不读取其类型。 */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UObject> SourceObject = nullptr;

    /** 实际操作的 Actor，可能是 Space 本身，也可能是StaticMeshActor  */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> RuntimeActor = nullptr;

    /** 父节点 */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UBuildingRuntimeNode> Parent = nullptr;

    /** 子节点 */
    UPROPERTY(BlueprintReadOnly)
    TArray<TObjectPtr<UBuildingRuntimeNode>> Children;

    /** 随本节点一起显隐的 Actor，例如设备、摄像头、管线等。 */
    UPROPERTY(BlueprintReadOnly)

    TArray<TObjectPtr<AActor>> ControlledActors;

    /** 数据源提供的通用标签。 */
    UPROPERTY(BlueprintReadOnly)
    TSet<FString> Tags;

    //----------------------------
    // Runtime
    //----------------------------

    /** 是否是楼层节点 */
    UPROPERTY(BlueprintReadOnly)
    bool bIsFloor = false;

    /** 是否允许参与拆楼 */
    UPROPERTY(BlueprintReadOnly)
    bool bCanDisassemble = false;

    /** 楼层号 */
    UPROPERTY(BlueprintReadOnly)
    int32 FloorIndex = BUILDING_INVALID_FLOOR_INDEX;

    /** 初始Transform */
    UPROPERTY(BlueprintReadOnly)
    FTransform OriginalTransform;

    /** 当前Transform */
    UPROPERTY(BlueprintReadOnly)
    FTransform CurrentTransform;

    /** 当前抽屉偏移 */
    UPROPERTY(BlueprintReadOnly)
    FVector CurrentOffset = FVector::ZeroVector;

    /** 目标抽屉偏移 */
    UPROPERTY(BlueprintReadOnly)
    FVector TargetOffset = FVector::ZeroVector;

    /** 节点包围盒 */
    UPROPERTY(BlueprintReadOnly)
    FBox Bounds;

    /** 当前是否隐藏 */
    UPROPERTY(BlueprintReadOnly)
    bool bHidden = false;

    /** 当前是否抽出 */
    UPROPERTY(BlueprintReadOnly)
    bool bExpanded = false;

public:

    /** 根据 Actor 查找节点 */
    UBuildingRuntimeNode* FindNode(AActor* InActor);

    /** 收集所有楼层节点 */
    void CollectFloorNodes(TArray<UBuildingRuntimeNode*>& OutNodes);

    /** 收集指定 Mark 的节点 */
    void CollectNodesByMark(const FString& Mark,TArray<UBuildingRuntimeNode*>& OutNodes);

    /** 遍历整棵 Runtime 树 */
    void ForEachNode(TFunctionRef<void(UBuildingRuntimeNode*)> Func);

    /** 重置运行时状态 */
    void ResetRuntime();
};


/**
 * 整栋建筑运行时数据   Sum
 */
USTRUCT(BlueprintType)
struct DTSCORE_API FBuildingRuntimeData
{
    GENERATED_BODY()

public:

    /** 数据源中的稳定建筑 ID，例如 A1。 */
    UPROPERTY(BlueprintReadOnly)
    FString BuildingId;

    /** 建筑Actor */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<AActor> BuildingActor = nullptr;

    /** 配置 */
    UPROPERTY(BlueprintReadOnly)
    FBuildingFloorConfig Config;

    /** Runtime根节点 */
    UPROPERTY(BlueprintReadOnly)
    TObjectPtr<UBuildingRuntimeNode> Root;

public:

    UBuildingRuntimeNode* FindNode(AActor* Actor);

    void CollectFloorNodes(TArray<UBuildingRuntimeNode*>& OutNodes);

    void CollectNodesByMark(const FString& Mark,TArray<UBuildingRuntimeNode*>& OutNodes);

    void ResetRuntime();
};

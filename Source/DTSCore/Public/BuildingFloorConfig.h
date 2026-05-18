

#pragma once
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "BuildingFloorConfig.generated.h"

UENUM(BlueprintType)
enum class EPullDirection : uint8
{
    Auto        UMETA(DisplayName = "Auto"),
    Right       UMETA(DisplayName = "Right (+X)"),
    Left        UMETA(DisplayName = "Left (-X)"),
    Forward     UMETA(DisplayName = "Forward (+Y)"),
    Backward    UMETA(DisplayName = "Backward (-Y)")
};

UENUM(BlueprintType)
enum class EDisassembleMode : uint8
{
    Drawer      UMETA(DisplayName = "Drawer（分层抽屉位移）"),
    Visibility  UMETA(DisplayName = "Visibility（显隐切换）")
};

USTRUCT(BlueprintType)
struct FBuildingFloorConfig : public FTableRowBase
{
    GENERATED_BODY()

    // =========================================================
    // 通用
    // =========================================================

    /** 拆楼交互模式 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "通用")
    EDisassembleMode DisassembleMode = EDisassembleMode::Drawer;

    // =========================================================
    // Drawer 模式
    // =========================================================

    /** 抽屉拉出方向 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    EPullDirection PullDirection = EPullDirection::Auto;

    /** 全局拉出距离缩放系数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    float PullDistanceScale = 1.5f;

    /** 分层间距系数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    float SpreadScale = 1.2f;

    /** 园区统一基准高度，0 = 用自身高度 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    float CampusMaxBuildingHeight = 0.f;

    /** 手动指定楼层平面 X 尺寸，0 = 自动计算 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    float MaxFloorSizeX = 0.f;

    /** 手动指定楼层平面 Y 尺寸，0 = 自动计算 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    float MaxFloorSizeY = 0.f;

    /** 单独覆盖某楼层的拉出距离缩放系数，Key = 楼层号 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
    TMap<int32, float> FloorPullScaleOverride;
};
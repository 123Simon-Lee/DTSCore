#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/DataTable/BuildingFloorConfig.h"
#include "BuildingCameraFocusInterface.generated.h"

/**
 * 拆楼器使用的相机聚焦接口。
 *
 * BuildingManager 只发出通用 Bounds 聚焦请求，不依赖具体 Pawn、CameraActor
 * 或 PlayerController。任何相机实现该接口后都可以接入拆楼流程。
 */
UINTERFACE(BlueprintType)
class DTSCORE_API UBuildingCameraFocusInterface : public UInterface
{
    GENERATED_BODY()
};

class DTSCORE_API IBuildingCameraFocusInterface
{
    GENERATED_BODY()

public:
    /** 保存进入拆楼前的视角；同一次拆楼会话只应保存一次。 */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Building|CameraFocus")
    void SaveBuildingFocusView();

    /** 根据通用目标和配置聚焦当前楼栋。 */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Building|CameraFocus")
    void FocusBuildingTarget(
        const FBuildingFocusTarget& Target,
        const FBuildingFocusSettings& Settings);

    /** 恢复进入拆楼前保存的视角。 */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Building|CameraFocus")
    void RestoreBuildingFocusView();

    /** 结束聚焦会话但不移动相机，用于配置为退出时不恢复的场景。 */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Building|CameraFocus")
    void ClearBuildingFocusView();
};

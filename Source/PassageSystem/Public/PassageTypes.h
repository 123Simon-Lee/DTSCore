// 数据结构、方向、执行状态
#pragma once

#include "CoreMinimal.h"
#include "PassageTypes.generated.h"

class UPassageMovementComponent;

// 场景展示对象类型，不对应任何后端数据结构
UENUM(BlueprintType)
enum class EPassageSubjectType : uint8
{
  Person UMETA(DisplayName = "人物"),
  Car UMETA(DisplayName = "车辆")
};

// 场景中的通行方向
UENUM(BlueprintType)
enum class EPassageDirection : uint8
{
  Enter UMETA(DisplayName = "进入"),
  Exit UMETA(DisplayName = "出去")
};

// 移动组件向 Subsystem 通知完成。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FPassageMovementFinishedEvent,         // 移动组件完成事件
    UPassageMovementComponent *, Movement, // 移动组件
    bool, bSuccess,                        // 是否成功通行
    const FString &, Reason);              // 执行结果说明

// 实际开始移动后通知外部。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FSimplePassageStartedEvent,       // 通行开始事件
    FName, ChannelId,                 // 通道 ID
    EPassageSubjectType, SubjectType, // 通行对象类型
    bool, bEnter);                    // 是否进入

// 已停止移动并隐藏对象后通知外部。
// Reason 是执行结果说明，不是后端业务数据。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FSimplePassageFinishedEvent, // 通行完成事件
    FName, ChannelId,            // 通道 ID
    bool, bSuccess,              // 是否成功通行
    const FString &, Reason);    // 执行结果说明
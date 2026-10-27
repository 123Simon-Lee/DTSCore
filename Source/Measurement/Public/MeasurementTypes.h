/**
 * 测量工具模块的公共类型定义。
 *
 * 该模块实现数字孪生场景中的测距/测面积功能，
 * 交互逻辑在 UMeasureToolComponentBase 及其子类，
 * 渲染表现在 AMeasureVisualizationActor。
 */
#pragma once

#include "CoreMinimal.h"
#include "MeasurementTypes.generated.h"

/**
 * 测量工具模式。
 *
 * 决定当前启用哪种测量工具，同一时刻通常只激活一种。
 */
UENUM(BlueprintType)
enum class EMeasurementToolMode : uint8
{

  None UMETA(DisplayName = "无"),
  Distance UMETA(DisplayName = "测距离"),
  Area UMETA(DisplayName = "测面积")
};

/**
 * 测量工具状态机。
 *
 * Inactive          未激活（工具关闭）
 * WaitingFirstPoint 工具已激活，等待确认第一个点
 * Previewing        已有至少一个点，鼠标移动中实时预览
 * Finished          本次测量已完成
 */
UENUM(BlueprintType)
enum class EMeasurementToolState : uint8
{
  Inactive UMETA(DisplayName = "未激活"),
  WaitingFirstPoint UMETA(DisplayName = "等待起点"),
  Previewing UMETA(DisplayName = "预览中"),
  Finished UMETA(DisplayName = "已完成")
};
#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"

/**
 * DTSCore 统一使用的 Custom Stencil 位。
 *
 * CustomDepthStencilValue 是一个共享的 8 位值，多个效果必须通过位运算
 * 保留彼此的标记，不能直接把整个值覆盖为 0 或 1。
 */
namespace DTSBuildingStencil
{
    /** 既有多颜色描边材质使用的低三位业务通道。 */
    static constexpr uint8 OutlineChannels =
        (1u << 0) | (1u << 1) | (1u << 2); // 1 | 2 | 4

    /**
     * 当前拆楼楼栋：后处理材质据此保留楼栋原色。
     * 使用最高位，避开既有描边材质占用的低位颜色通道 1、2、4。
     */
    static constexpr uint8 Isolation = 1u << 7; // 128

    /** Sewer status highlight uses M_OutLine channel 1. */
    static constexpr uint8 SewerHighlight = 1u << 0; // 1

    /** 普通设备鼠标悬停描边。 */
    static constexpr uint8 Hover = 1u << 1; // 2

    /**
     * 报警设备持续高亮。
     * 后处理材质使用该位绘制红色闪烁；它不依赖鼠标悬停或楼栋隔离状态。
     */
    static constexpr uint8 AlarmHighlight = 1u << 2; // 4

    /** 兼容旧名称：报警设备悬停时同样保留持续报警高亮位。 */
    static constexpr uint8 AlarmHover = AlarmHighlight;

    /** 设备鼠标悬停独占的模板位；不能包含持续报警位。 */
    static constexpr uint8 DeviceHoverChannels = Hover; // 2

    /** 设备状态占用的模板位。 */
    static constexpr uint8 DeviceStatusChannels = AlarmHighlight; // 4

    /**
     * Only one stencil channel is written by a primitive at a time.
     * ERSM_255 can erase other channels in overlapping pixels and can
     * produce combined values that legacy outline materials misread.
     */
    FORCEINLINE ERendererStencilMask ResolveWriteMask(
        const uint8 StencilValue)
    {
        if ((StencilValue & Hover) != 0)
        {
            return ERendererStencilMask::ERSM_2;
        }

        if ((StencilValue & AlarmHighlight) != 0)
        {
            return ERendererStencilMask::ERSM_4;
        }

        if ((StencilValue & Isolation) != 0)
        {
            return ERendererStencilMask::ERSM_128;
        }

        if ((StencilValue & SewerHighlight) != 0)
        {
            return ERendererStencilMask::ERSM_1;
        }

        return ERendererStencilMask::ERSM_Default;
    }
}

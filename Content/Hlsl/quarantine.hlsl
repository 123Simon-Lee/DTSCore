/*
 * Building Focus Isolation
 *
 * Stencil bit 7 (128) marks the active building.
 * Stencil bit 1 (2) marks the hovered device.
 * Stencil bit 2 (4) marks the alarm device while its blink is on.
 *
 * 设备在拆楼隔离状态下：
 * 128 | 2 = 130
 * 128 | 4 = 132
 */

float Fade = saturate(IsolationAlpha);

uint StencilValue =
    (uint) round(max(CustomStencil, 0.0));

uint IsolationBit = (1u << 7);
uint HoverBit = (1u << 1);
uint AlarmBit = (1u << 2);

/*
 * 当前像素是否属于拆楼目标楼栋。
 *
 * 128 和 130 都包含 IsolationBit，
 * 所以悬停设备仍然属于目标楼栋。
 */
uint VisibleTargetBits =
    IsolationBit |
    HoverBit |
    AlarmBit;

float TargetMask =
    ((StencilValue & VisibleTargetBits) != 0u)
        ? 1.0
        : 0.0;

/*
 * 当前像素是否属于悬停设备。
 */
float CenterHoverMask =
    ((StencilValue & HoverBit) != 0u)
        ? 1.0
        : 0.0;

/*
 * 当前像素是否属于本次闪烁亮起阶段的报警设备。
 * C++ 定时写入/清除 AlarmBit，因此材质只负责绘制报警外描边。
 */
float CenterAlarmMask =
    ((StencilValue & AlarmBit) != 0u)
        ? 1.0
        : 0.0;

const uint CustomStencilTextureId = 25u;

float2 StencilUV = GetDefaultSceneTextureUV(
    Parameters,
    CustomStencilTextureId);

float2 StencilTexelSize = GetSceneTextureViewSize(
    CustomStencilTextureId).zw;

/*
 * 读取当前像素上下左右的 Custom Stencil。
 * 用于柔化拆楼目标的轮廓边缘。
 */
float LeftStencil = SceneTextureLookup(
    ClampSceneTextureUV(
        StencilUV + float2(-StencilTexelSize.x, 0.0),
        CustomStencilTextureId),
    CustomStencilTextureId,
    false).r;

float RightStencil = SceneTextureLookup(
    ClampSceneTextureUV(
        StencilUV + float2(StencilTexelSize.x, 0.0),
        CustomStencilTextureId),
    CustomStencilTextureId,
    false).r;

float UpStencil = SceneTextureLookup(
    ClampSceneTextureUV(
        StencilUV + float2(0.0, -StencilTexelSize.y),
        CustomStencilTextureId),
    CustomStencilTextureId,
    false).r;

float DownStencil = SceneTextureLookup(
    ClampSceneTextureUV(
        StencilUV + float2(0.0, StencilTexelSize.y),
        CustomStencilTextureId),
    CustomStencilTextureId,
    false).r;

/*
 * 提取相邻像素中的拆楼隔离位 128。
 */
float LeftMask =
    (
        (
            ((uint) round(max(LeftStencil, 0.0))) &
            IsolationBit
        ) != 0u
    )
        ? 1.0
        : 0.0;

float RightMask =
    (
        (
            ((uint) round(max(RightStencil, 0.0))) &
            IsolationBit
        ) != 0u
    )
        ? 1.0
        : 0.0;

float UpMask =
    (
        (
            ((uint) round(max(UpStencil, 0.0))) &
            IsolationBit
        ) != 0u
    )
        ? 1.0
        : 0.0;

float DownMask =
    (
        (
            ((uint) round(max(DownStencil, 0.0))) &
            IsolationBit
        ) != 0u
    )
        ? 1.0
        : 0.0;

/*
 * 中心像素使用双倍权重。
 * 保留原来的楼栋边缘柔化效果。
 */
float CoverageMask =
    (
        TargetMask * 2.0 +
        LeftMask +
        RightMask +
        UpMask +
        DownMask
    ) / 6.0;

float SoftTargetMask = lerp(
    TargetMask,
    CoverageMask,
    0.75);

/*
 * ============================================================
 * 根据相机距离动态计算设备悬停描边粗度
 * ============================================================
 *
 * 距离越近：描边越粗，最大 2px。
 * 距离越远：描边越细，最小 1px。
 * 达到 5000cm 后不再继续变细。
 */

const uint SceneDepthTextureId = 1u;

/*
 * 当前像素到相机的场景深度。
 * UE 默认单位为厘米。
 */
float CurrentSceneDepth = SceneTextureLookup(
    StencilUV,
    SceneDepthTextureId,
    false).r;

CurrentSceneDepth = max(
    CurrentSceneDepth,
    0.0);

/*
 * 描边最大值写死为 2px。
 */
const float MaxHoverOutlineWidth = 2.0;

/*
 * 远处最小描边为 1px。
 */
const float MinHoverOutlineWidth = 1.0;

const float NearOutlineDepth = 100.0;
const float FarOutlineDepth = 1200.0;


float DistanceAlpha = saturate(
    (CurrentSceneDepth - NearOutlineDepth) /
    (FarOutlineDepth - NearOutlineDepth));

/*
 * 反向线性插值：
 *
 * DistanceAlpha = 0 时，宽度为2px。
 * DistanceAlpha = 1 时，宽度为1px。
 */
float HoverOutlineWidth = lerp(
    MaxHoverOutlineWidth,
    MinHoverOutlineWidth,
    DistanceAlpha);

/*
 * 从八个方向检测设备悬停模板位边缘。
 */
const float2 HoverDirections[8] =
{
    float2(-1.0, 0.0),
    float2(1.0, 0.0),
    float2(0.0, -1.0),
    float2(0.0, 1.0),

    float2(-1.0, -1.0),
    float2(1.0, -1.0),
    float2(-1.0, 1.0),
    float2(1.0, 1.0)
};

float HoverOutlineMask = 0.0;
float AlarmOutlineMask = 0.0;

[unroll]
for (
int DirectionIndex = 0;
    DirectionIndex < 8;
    ++DirectionIndex)
{
    /*
     * 对角方向归一化，
     * 保证所有方向实际采样距离一致。
     */
float2 Direction = normalize(
        HoverDirections[DirectionIndex]);

float2 SampleOffset =
        Direction *
        StencilTexelSize *
        HoverOutlineWidth;

float2 SampleUV = ClampSceneTextureUV(
        StencilUV + SampleOffset,
        CustomStencilTextureId);

float SampledStencil = SceneTextureLookup(
        SampleUV,
        CustomStencilTextureId,
        false).r;

uint SampledStencilValue =
        (uint) round(max(SampledStencil, 0.0));

float SampledHoverMask =
        ((SampledStencilValue & HoverBit) != 0u)
            ? 1.0
            : 0.0;

float SampledAlarmMask =
        ((SampledStencilValue & AlarmBit) != 0u)
            ? 1.0
            : 0.0;

    /*
     * 中心像素和周围像素的 HoverBit 不一致，
     * 表示当前像素位于悬停设备轮廓边缘。
     *
     * 同时覆盖轮廓内侧和外侧，
     * 避免设备紧贴墙体时描边缺失。
     */
float CurrentEdgeMask = abs(
        CenterHoverMask -
        SampledHoverMask);

    HoverOutlineMask = max(
        HoverOutlineMask,
        CurrentEdgeMask);

    /*
     * 报警位只生成轮廓边缘，不使用 CenterAlarmMask 对设备内部填色。
     */
float CurrentAlarmEdgeMask = abs(
        CenterAlarmMask -
        SampledAlarmMask);

    AlarmOutlineMask = max(
        AlarmOutlineMask,
        CurrentAlarmEdgeMask);
}

HoverOutlineMask = saturate(
    HoverOutlineMask);

AlarmOutlineMask = saturate(
    AlarmOutlineMask);

/*
 * 非拆楼目标区域混合为隔离背景颜色。
 */
float3 OutsideColor = lerp(
    SceneColor.rgb,
    BackgroundColor.rgb,
    Fade);

/*
 * 保留当前拆楼目标楼栋原来的场景颜色。
 */
float3 IsolationResult = lerp(
    OutsideColor,
    SceneColor.rgb,
    SoftTargetMask);



/*
 * 最后绘制设备悬停描边。
 *
 * 模板值为130时显示；
 * 恢复为128时自动消失。
 */
float3 HoverResult = lerp(
    IsolationResult,
    HoverOutlineColor.rgb,
    HoverOutlineMask);

/*
 * 报警仅绘制 AlarmBlinkColor 外描边，不填充设备表面。
 * 闪烁由 C++ 定时开关模板位 4 控制。
 */
return lerp(
    HoverResult,
    AlarmBlinkColor.rgb,
    AlarmOutlineMask);

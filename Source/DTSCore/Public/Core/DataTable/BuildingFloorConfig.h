#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Curves/CurveFloat.h"
#include "BuildingFloorConfig.generated.h"

class UBuildingFloorComponent;

// ============================================================
// 名称匹配
// ============================================================

/** 名称匹配方式 */
UENUM(BlueprintType)
enum class ENameMatchType : uint8
{
	Equals UMETA(DisplayName = "完全匹配"),
	StartsWith UMETA(DisplayName = "前缀匹配"),
	EndsWith UMETA(DisplayName = "后缀匹配"),
	Contains UMETA(DisplayName = "包含匹配")
};

/** 名称匹配规则 */
USTRUCT(BlueprintType)
struct FNameMatchRule
{
	GENERATED_BODY()

public:
	/** 匹配方式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Name")
	ENameMatchType MatchType = ENameMatchType::EndsWith;

	/** 匹配字符串，例如 TH、WQ、DevicePoint */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Name")
	FString MatchString = TEXT("");

public:
	FNameMatchRule() = default;

	FNameMatchRule(ENameMatchType InMatchType, const FString& InMatchString)
		: MatchType(InMatchType), MatchString(InMatchString)
	{
	}

	FNameMatchRule(ENameMatchType InMatchType, const TCHAR* InMatchString)
		: MatchType(InMatchType), MatchString(InMatchString)
	{
	}
};

/** 名称识别配置 */
USTRUCT(BlueprintType)
struct FBuildingNameSettings
{
	GENERATED_BODY()

public:
	/** TH层识别规则 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Name")
	FNameMatchRule THRule = { ENameMatchType::EndsWith, TEXT("TH") };

	/** 外墙识别规则 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Name")
	FNameMatchRule WQRule = { ENameMatchType::EndsWith, TEXT("WQ") };

	/** 选择具体楼层拆楼时，自动隐藏该层识别到的 TH Actor。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Name")
	bool bHideCurrentFloorCeiling = true;
};

// ============================================================
// 动画配置
// ============================================================

/** 动画配置 */
USTRUCT(BlueprintType)
struct FBuildingAnimationSettings
{
	GENERATED_BODY()

public:
	/** 拆楼动画曲线 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TObjectPtr<UCurveFloat> Curve = nullptr;
};

// ============================================================
// 相机聚焦配置
// ============================================================

/** 拆楼器输出给相机系统的通用聚焦目标。 */
USTRUCT(BlueprintType)
struct FBuildingFocusTarget
{
	GENERATED_BODY()

	/** 当前目标是否拥有有效 Bounds。 */
	UPROPERTY(BlueprintReadOnly, Category = "Focus")
	bool bIsValid = false;

	/** 世界坐标包围盒中心，作为轨道相机的观察中心。 */
	UPROPERTY(BlueprintReadOnly, Category = "Focus")
	FVector Center = FVector::ZeroVector;

	/** 世界坐标包围盒半尺寸。 */
	UPROPERTY(BlueprintReadOnly, Category = "Focus")
	FVector Extent = FVector::ZeroVector;

	/** 建筑水平方向的长轴，已归一化。 */
	UPROPERTY(BlueprintReadOnly, Category = "Focus")
	FVector LongAxis = FVector::ForwardVector;

	/** 建筑水平方向的短轴，已归一化。 */
	UPROPERTY(BlueprintReadOnly, Category = "Focus")
	FVector ShortAxis = FVector::RightVector;
};

/** 拆楼模式的自适应相机聚焦配置。 */
USTRUCT(BlueprintType)
struct FBuildingFocusSettings
{
	GENERATED_BODY()

	/** 进入拆楼模式时是否自动聚焦当前楼栋。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	bool bAutoFocusOnEnter = true;

	/**
	 * Drawer 模式等待分层完成后再按展开后的 Bounds 聚焦。
	 * 仅 Drawer 模式显示，其他拆楼模式下无意义。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "DisassembleMode == EDisassembleMode::Drawer", EditConditionHides))
	bool bFocusAfterLayering = true;

	/** 选择具体楼层后是否把镜头从整栋楼收紧到该楼层。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	bool bAutoFocusFloorOnSelect = true;

	/**
	 * Drawer 模式收回当前楼层后是否重新聚焦整栋楼。
	 * 仅 Drawer 模式显示，其他拆楼模式下无意义。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus",
		meta = (EditCondition = "DisassembleMode == EDisassembleMode::Drawer", EditConditionHides))
	bool bRefocusBuildingOnFloorClose = true;

	/** 真正退出拆楼模式时恢复进入前的园区视角。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	bool bRestoreViewOnExit = true;

	/** 进入拆楼模式时，将当前楼栋以外的场景压黑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	bool bEnableIsolation = false;

	/** 隔离后处理淡入、淡出的时间（秒）；0 表示立即切换。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float IsolationFadeTime = 0.35f;

	/** 屏幕边缘留白倍率；1.0 为刚好装入，建议 1.2~1.5。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float Padding = 1.3f;

	/** 鸟瞰俯角，负值向下看。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float Pitch = -32.f;

	/** 是否根据楼栋长短轴和当前相机位置自动计算 Yaw。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	bool bUseAutoYaw = true;

	/** 相机绝对 Yaw；关闭 Use Auto Yaw 后生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (EditCondition = "!bUseAutoYaw", EditConditionHides))
	float Yaw = 0.f;

	/** 相机 Roll。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float Roll = 0.f;

	/** 沿建筑长轴加入少量侧向偏移，使长立面具有空间纵深。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DiagonalViewRatio = 0.25f;

	/**
	 * 聚焦时 TwinSpectatorPawn 的绝对世界坐标，分别对应 X、Y、Z 轴。
	 * 保持 (0,0,0) 时自动使用楼栋或楼层的 Bounds 中心。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus")
	FVector CameraLocation = FVector::ZeroVector;

	/** 相机飞行时间（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float InterpTime = 0.8f;

	/** 聚焦时允许的最小 SpringArm 长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinDistance = 1000.f;

	/** 聚焦时允许的最大 SpringArm 长度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Focus", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxDistance = 60000.f;
};

/** 单个 FloorIndex 的相机聚焦覆盖配置。 */
USTRUCT(BlueprintType)
struct FBuildingFloorFocusSettings
{
	GENERATED_BODY()

	/** 屏幕边缘留白倍率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float Padding = 1.3f;

	/** 相机 Pitch。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float Pitch = -32.f;

	/** 是否根据楼栋方向自动计算 Yaw。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus")
	bool bUseAutoYaw = true;

	/** 关闭 Use Auto Yaw 后使用的绝对 Yaw。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (EditCondition = "!bUseAutoYaw", EditConditionHides))
	float Yaw = 0.f;

	/** 相机 Roll。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float Roll = 0.f;

	/** 沿建筑长轴加入的观察方向比例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DiagonalViewRatio = 0.25f;

	/** TwinSpectatorPawn 的绝对世界坐标；(0,0,0) 表示使用楼层 Bounds 中心。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus")
	FVector CameraLocation = FVector::ZeroVector;

	/** 相机移动时间（秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float InterpTime = 0.8f;

	/** 聚焦距离下限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinDistance = 1000.f;

	/** 聚焦距离上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floor Focus", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxDistance = 60000.f;
};

// ============================================================
// 拆楼模式
// ============================================================

/** 拆楼模式 */
UENUM(BlueprintType)
enum class EDisassembleMode : uint8
{
	/** 分层 + 抽屉位移 */
	Drawer UMETA(DisplayName = "Drawer（分层抽屉位移）"),

	/** 楼层显隐切换 */
	Visibility UMETA(DisplayName = "Visibility（显隐切换）"),

	/** 目标层保持不动，下层掉落，上层升起 */
	LiftDrop UMETA(DisplayName = "LiftDrop（上下分离）")
};

// ============================================================
// Drawer配置
// ============================================================

/** 抽屉拉出方向 */
UENUM(BlueprintType)
enum class EPullDirection : uint8
{
	/** 不指定拉出方向，由外部逻辑控制。 */
	None UMETA(DisplayName = "无"),

	/** 根据建筑长宽自动选择最佳拉出方向。 */
	Auto UMETA(DisplayName = "自动"),

	/** 沿世界坐标 X 正方向拉出。 */
	Right UMETA(DisplayName = "X轴正方向"),

	/** 沿世界坐标 X 负方向拉出。 */
	Left UMETA(DisplayName = "X轴负方向"),

	/** 沿世界坐标 Y 正方向拉出。 */
	Forward UMETA(DisplayName = "Y轴正方向"),

	/** 沿世界坐标 Y 负方向拉出。 */
	Backward UMETA(DisplayName = "Y轴负方向")
};

/** Drawer模式配置 */
USTRUCT(BlueprintType)
struct FBuildingDrawerSettings
{
	GENERATED_BODY()

public:
	/** 楼层抽屉拉出方向 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
	EPullDirection PullDirection = EPullDirection::Auto;

	/** 抽屉拉出距离倍率 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Drawer",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PullDistanceScale = 1.5f;

	/** 控制楼层分层间距倍率，0表示不额外展开 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Drawer",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpreadScale = 1.2f;

	/** 分层后整体离地高度，0表示自动根据模型高度计算 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Drawer",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CampusMaxBuildingHeight = 0.f;

	/** 手动覆盖楼层最大X尺寸，0表示自动计算 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Drawer",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxFloorSizeX = 0.f;

	/** 手动覆盖楼层最大Y尺寸，0表示自动计算 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Drawer",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxFloorSizeY = 0.f;

	/**
	 * 指定楼层的拉出倍率覆盖
	 *
	 * 示例：
	 * Key = 3
	 * Value = 2.0
	 *
	 * 表示 3F 使用 2 倍拉出距离。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drawer")
	TMap<int32, float> FloorPullScaleOverride;
};

/** LiftDrop模式配置 */
USTRUCT(BlueprintType)
struct FBuildingLiftDropSettings
{

	GENERATED_BODY()

public:
	/** 新目标楼层从多高的位置掉落，单位厘米 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiftDrop", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DropStartHeight = 3000.f;

	/** 退出显示范围的楼层向上抬升多高后隐藏 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiftDrop", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float LiftOutHeight = 3000.f;
};

// Visibility 楼层显示模式
UENUM(BlueprintType)
enum class EVisibilityFloorDisplayMode : uint8
{
	/** 显示当前楼层及以下，隐藏当前楼层以上 */
	ShowCurrentAndBelow UMETA(DisplayName = "显示当前楼层及以下"),

	/** 只显示当前楼层 */
	ShowOnlyCurrent UMETA(DisplayName = "只显示当前楼层"),

	/** 显示当前楼层及以上 */
	ShowCurrentAndAbove UMETA(DisplayName = "显示当前楼层及以上")
};
// Visibility模式配置
USTRUCT(BlueprintType)
struct FBuildingVisibilitySettings
{
	GENERATED_BODY()

public:
	/** 进入 Visibility 拆楼时，是否隐藏其他楼栋 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	bool bHideOtherBuildingsOnEnter = true;

	/** 进入 Visibility 拆楼时，当前楼栋是否先显示全部楼层 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	bool bShowAllFloorsOnEnter = true;

	/** 退出 Visibility 拆楼时，是否恢复其他楼栋显示 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	bool bRestoreOtherBuildingsOnExit = true;

	/** 点击楼层后的楼层显示方式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visibility")
	EVisibilityFloorDisplayMode FloorDisplayMode =
		EVisibilityFloorDisplayMode::ShowCurrentAndBelow;
};

// ============================================================
// Component配置
// ============================================================

/** Component设置 */
USTRUCT(BlueprintType)
struct FBuildingComponentSettings
{
	GENERATED_BODY()

public:
	/** 指定拆楼组件类，不填则默认使用 UBuildingFloorComponent */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Component")
	TSubclassOf<UBuildingFloorComponent> ComponentClass;
};

// ============================================================
// 可拆 / 不可拆配置
// ============================================================

/** 可拆配置 */
// 可拆排除配置
USTRUCT(BlueprintType)
struct FBuildingDisassembleSettings
{
	GENERATED_BODY()

public:
	/**
	 * 带有这个 Mark 的 Space 不参与拆楼
	 *
	 * 示例：
	 * Space Marks 包含 NoDisassemble，则这个节点不可拆。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disassemble")
	FString DisableDisassembleMark = TEXT("NoDisassemble");

	/**
	 * 指定 Space.Code 不参与拆楼
	 *
	 * 示例：
	 * SW
	 *
	 * 适合 SW 这种只存设备、不参与拆楼的节点。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disassemble")
	TSet<FString> DisabledSpaceCodes;

	/**
	 * 指定楼层号不参与拆楼
	 *
	 * 示例：
	 * -1
	 * 0
	 * 9999
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disassemble")
	TSet<int32> DisabledFloors;
};

// ============================================================
// 楼层编号配置
// ============================================================

/** 楼层编号解析方式 */
UENUM(BlueprintType)
enum class EFloorIndexResolveMode : uint8
{
	/** 自动从 Space.Code / DisplayName / ActorName 解析，例如 1F、2F、B1F、RF */
	Auto UMETA(DisplayName = "自动解析"),

	/** 只按 Space.Code 映射 */
	CodeMap UMETA(DisplayName = "按 Space.Code 映射"),

	/** 只按 Space.DisplayName 映射 */
	DisplayNameMap UMETA(DisplayName = "按 DisplayName 映射"),

	/** 只按 ActorName 映射 */
	ActorNameMap UMETA(DisplayName = "按 ActorName 映射"),

	/** 优先按 Space.Code 映射，找不到再自动解析 */
	CodeMapThenAuto UMETA(DisplayName = "Code映射优先，失败自动解析"),

	/** 优先按 DisplayName 映射，找不到再自动解析 */
	DisplayNameMapThenAuto UMETA(DisplayName = "DisplayName映射优先，失败自动解析"),

	/** 优先按 ActorName 映射，找不到再自动解析 */
	ActorNameMapThenAuto UMETA(DisplayName = "ActorName映射优先，失败自动解析")
};

/** 楼层编号配置 */
USTRUCT(BlueprintType)
struct FBuildingFloorIndexSettings
{
	GENERATED_BODY()

public:
	/** 楼层编号解析方式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FloorIndex")
	EFloorIndexResolveMode ResolveMode = EFloorIndexResolveMode::Auto;

	/**
	 * 手动楼层映射表
	 *
	 * 根据 ResolveMode 决定 Key 使用什么：
	 *
	 * CodeMap / CodeMapThenAuto:
	 * Key = Space.Code
	 *
	 * DisplayNameMap / DisplayNameMapThenAuto:
	 * Key = Space.DisplayName
	 *
	 * ActorNameMap / ActorNameMapThenAuto:
	 * Key = ActorName
	 *
	 * 示例：
	 * FSSJQ -> -1
	 * RF    -> 9999
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "FloorIndex",
		meta = (EditCondition = "ResolveMode != EFloorIndexResolveMode::Auto",
			EditConditionHides))
	TMap<FString, int32> FloorIndexMap;
};

// ============================================================
// 建筑拆楼配置表
// ============================================================

// 建筑拆楼配置
USTRUCT(BlueprintType)
struct FBuildingFloorConfig : public FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 整栋建筑是否允许拆楼
	 *
	 * false：
	 * - 不显示拆楼模式
	 * - 不显示楼层编号配置
	 * - 不显示 Drawer 配置
	 * - 不显示动画配置
	 * - 不显示名称规则
	 * - 运行时也不会执行拆楼逻辑
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	bool bCanDisassemble = true;

	/** 拆楼模式 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	EDisassembleMode DisassembleMode = EDisassembleMode::Drawer;

	/**
	 * 可拆 / 不可拆配置
	 *
	 * 用于判断某些 Space、某些楼层是否允许参与拆楼。
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	FBuildingDisassembleSettings Disassemble;

	/**
	 * 楼层编号配置
	 *
	 * 用于处理 FSSJQ、RF、SW 等非标准命名。
	 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	FBuildingFloorIndexSettings FloorIndex;

	/** Component配置 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	FBuildingComponentSettings Component;

	/** 动画配置 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	FBuildingAnimationSettings Animation;

	/** 进入/退出拆楼模式时的自适应相机聚焦配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	FBuildingFocusSettings Focus;

	/**
	 * 按解析后的 FloorIndex 覆盖楼层聚焦配置。
	 * 例如：-1=B1，1=1F，2=2F，9999=RF；没有对应 Key 时回退 Focus。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
	TMap<int32, FBuildingFloorFocusSettings> FloorFocusOverrides;

	/** Drawer模式配置 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble && DisassembleMode == EDisassembleMode::Drawer",
			EditConditionHides))
	FBuildingDrawerSettings Drawer;

	/** Visibility模式配置 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble && DisassembleMode == EDisassembleMode::Visibility",
			EditConditionHides))
	FBuildingVisibilitySettings Visibility;

	/** 名称规则 */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "Building",
		meta = (EditCondition = "bCanDisassemble",
			EditConditionHides))
	FBuildingNameSettings Name;

	/** LiftDrop模式配置 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building",
		meta = (EditCondition = "bCanDisassemble && DisassembleMode == EDisassembleMode::LiftDrop", EditConditionHides))
	FBuildingLiftDropSettings LiftDrop;
};

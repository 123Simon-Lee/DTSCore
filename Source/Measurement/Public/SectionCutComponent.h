#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ProceduralMeshComponent.h"
#include "SectionCutComponent.generated.h"

class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
// X,Y,Z轴方向
// X = FVector::ForwardVector; // (1,0,0) 左右剖切
// Y = FVector::RightVector;   // (0,1,0) 前后剖切
// Z = FVector::UpVector;      // (0,0,1) 上下剖切

/**
 /** 剖切后保留平面的哪一侧。
 *
 * NegativeSide：保留法线反方向一侧（默认，滑块从"地面"向"顶部"推进时常用）
 * PositiveSide：保留法线正方向一侧
 */
UENUM(BlueprintType)
enum class ESectionCutKeepSide : uint8
{
  NegativeSide UMETA(DisplayName = "保留法线反方向"),
  PositiveSide UMETA(DisplayName = "保留法线正方向")
};

/**
 * 包围盒与剖切平面的关系（用于快速分类，避免逐模型切割）。
 * 3种关系：Keep、Remove、Intersect
 *
 * Keep：完全在保留侧 → 原样显示，无需切割
 * Remove：完全在移除侧 → 直接隐藏源组件，无需切割
 * Intersect：与平面相交 → 真正执行 SliceProceduralMesh
 */
UENUM()
enum class ESectionBoundsRelation : uint8
{
  Keep,
  Remove,
  Intersect
};

// 缓存未切割的ProceduraMesh数据
USTRUCT()
struct FSectionProcMeshSourceCache
{
  GENERATED_BODY()

  UPROPERTY()
  TArray<FProcMeshSection> Sections;

  UPROPERTY()
  int32 LODIndex = 0;
};

/**
 * 单个剖切目标的运行时状态（一个源 StaticMeshComponent 对应一条）。
 *
 * 三组件协作模型：
 * Source（原始网格，切割期间隐藏）
 *   → Working（不注册到世界的临时 PMC，承载切片计算）
 *   → Result（注册到世界的 PMC，渲染切割结果）
 */
USTRUCT()
struct FSectionCutRuntimeItem
{
  GENERATED_BODY()

  /** 原始静态网格组件（剖切目标）。 */
  UPROPERTY(Transient)
  TObjectPtr<UStaticMeshComponent> SourceComponent = nullptr;

  /*
   * 最终显示结果。
   */
  UPROPERTY(Transient)
  TObjectPtr<UProceduralMeshComponent> ResultComponent = nullptr;

  /*
   * 不注册到世界、不显示，只负责执行 SliceProceduralMesh。
   */
  UPROPERTY(Transient)
  TObjectPtr<UProceduralMeshComponent> WorkingComponent = nullptr;

  /** 进入剖切前源组件的可见性（Restore 时还原用）。 */
  bool bOriginalVisible = true;
  /** 进入剖切前源组件的 bHiddenInGame。 */
  bool bOriginalHiddenInGame = false;
  /** 是否参与本次剖切（源组件本来就隐藏的不参与）。 */
  bool bParticipates = true;
};

/** 一轮剖切（预览或最终）全部处理完时广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSectionCutFinished, bool, bFinalCut);

UCLASS(ClassGroup = (Measurement), meta = (BlueprintSpawnableComponent))
class MEASUREMENT_API USectionCutComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  USectionCutComponent();

  // 开始裁剪选中的建筑
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetTargetBuilding(AActor *NewTargetBuilding);

  // 0~1 控制剖切程度
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetCutAmount(float NewAmount, bool bFinalCut = false);

  // 设置世界空间剖切方向
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetCutNormal(const FVector &NewWorldNormal, bool bFinalCut = false);

  // 设置任意旋转
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetCutRotation(const FRotator &NewRotation, bool bFinalCut = false);

  /** 直接给定剖切平面的世界变换（Z 轴为法线），反算 CutAmount 后执行切割。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetCutPlaneTransform(const FTransform &WorldTransform, bool bFinalCut = false);

  /** 切换保留侧：平面语义翻转，需要重新切割。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void SetKeepSide(ESectionCutKeepSide NewKeepSide, bool bFinalCut = false);
  // 执行最终高质量裁剪
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void CommitCut();

  // 无损还原
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void Restore();

  // 完全退出并释放动态对象
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void ClearTarget();

  /** 清空源网格的 ProcMesh 缓存（StaticMesh 资产变更后调用）。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementSectionCut")
  void ClearMeshCache();

  /** 当前剖切平面世界变换（Z 轴 = 法线）。 */
  UFUNCTION(BlueprintPure, Category = "MeasurementSectionCut")
  FTransform GetCutPlaneTransform() const;

  UFUNCTION(BlueprintPure, Category = "MeasurementSectionCut")
  float GetCutAmount() const
  {
    return CutAmount;
  }

public:
  /** 截面覆盖材质（SliceProceduralMesh 生成封口面时使用）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  TObjectPtr<UMaterialInterface> SectionCapMaterial = nullptr;

  /** 是否生成截面盖。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  bool bEnableCap = true;

  /*
   * 拖动过程建议不封口，松开后再封口。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  bool bFillCapDuringPreview = false;

  /** 剖切后保留平面的哪一侧。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  ESectionCutKeepSide KeepSide = ESectionCutKeepSide::NegativeSide;

  /** 切割/转换源网格时使用的 LOD（预览可用低 LOD 提速）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  int32 RuntimeLODIndex = 0;

  /** 预览更新的最小间隔秒数（拖动节流，最终切割不受限）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  float PreviewUpdateInterval = 0.1f;

  /** Tick 内单帧处理切割队列的时间预算（毫秒），超时顺延到下一帧。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  float FrameBudgetMilliseconds = 3.0f;

  /** 投影范围向两侧外扩的余量（cm），保证滑块两端能完全切光/完整显示。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SectionCut")
  float BoundsPadding = 1.0f;

  /** 一轮切割（预览或最终）完成时广播。 */
  UPROPERTY(BlueprintAssignable)
  FOnSectionCutFinished OnSectionCutFinished;

protected:
  virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *TickFunction) override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
  /** 收集目标建筑下所有 StaticMeshComponent，建立 RuntimeItems。 */
  void CollectTargetComponents();

  /** 递归收集 Actor 及其挂载子 Actor 上的有效 StaticMeshComponent（VisitedActors 防环）。 */
  static void CollectStaticMeshComponentsRecursive(AActor *Actor, TSet<AActor *> &VisitedActors, TArray<UStaticMeshComponent *> &OutComponents);

  /** 把所有参与模型的包围盒角点投影到法线上，求 [ProjectionMin, ProjectionMax] 归一化范围。 */
  void CalculateProjectionRange();
  /** 由 CutAmount 在投影范围内插值出剖切平面原点。 */
  void UpdatePlaneOrigin();

  /** 请求一次切割（预览节流/最终立即），开启 Tick 排队处理。 */
  void RequestCutUpdate(bool bFinalCut);
  /** 开始一轮切割：先按包围盒三分类，只把与平面相交的模型排进队列。 */
  void BeginQueuedUpdate(bool bFinalCut);
  /** 消费切割队列：在帧预算内逐个 RebuildCutMesh。 */
  void ProcessQueuedWork();

  /** 包围盒快速分类：完全在保留侧/移除侧/与平面相交。 */
  ESectionBoundsRelation ClassifyComponent(const UStaticMeshComponent *Component) const;

  /** 对单个模型执行"源缓存 → Working 重置 → 切片 → 复制到 Result"全流程。 */
  bool RebuildCutMesh(FSectionCutRuntimeItem &Item, bool bFinalCut);

  /** 确保该条目的 Result/Working 两个 PMC 已创建（懒创建，Result 挂到源组件父级）。 */
  bool EnsureProceduralComponents(FSectionCutRuntimeItem &Item);

  /** 取源网格的 ProcMesh Sections 缓存，没有则用临时组件转换一次并缓存。 */
  const FSectionProcMeshSourceCache *
  GetOrCreateSourceCache(UStaticMeshComponent *SourceComponent);

  /** 把 Working 重置为"未切割的完整源网格"，并按源组件的材质槽赋材质。 */
  bool ResetWorkingComponent(FSectionCutRuntimeItem &Item, const FSectionProcMeshSourceCache &Cache);

  /** 把 Working 的全部 Sections 和材质复制到目标 PMC（含封口新增段）。 */
  static void CopyProceduralMeshSections(UProceduralMeshComponent *From, UProceduralMeshComponent *To);

  /** 把期望保留侧的世界法线换算成 SliceProceduralMesh 需要的法线（含缩放/旋转补偿）。 */
  FVector CalculateSliceNormalForComponent(const UProceduralMeshComponent *Component) const;

  /** 把 RuntimeLODIndex 钳制到该网格实际 LOD 数量内。 */
  int32 ResolveLODIndex(const UStaticMesh *StaticMesh) const;

  void RestoreItem(FSectionCutRuntimeItem &Item);
  void HideSource(FSectionCutRuntimeItem &Item);
  void HideResult(FSectionCutRuntimeItem &Item);
  void ShowResult(FSectionCutRuntimeItem &Item);

  /** KeepSide 是否为正方向侧。 */
  bool IsKeepingPositiveSide() const;

private:
  /** 当前剖切目标（建筑根 Actor，递归收集其下所有模型）。 */
  UPROPERTY(Transient)
  TObjectPtr<AActor> TargetBuilding = nullptr;

  /** 每个参与剖切的源组件对应的运行时状态。 */
  UPROPERTY(Transient)
  TArray<FSectionCutRuntimeItem> RuntimeItems;

  /** 源网格 → ProcMesh Sections 缓存（按 StaticMesh 资产共享，避免重复转换）。 */
  UPROPERTY(Transient)
  TMap<TObjectPtr<UStaticMesh>, FSectionProcMeshSourceCache> SourceMeshCache;

  // 剖切平面：法线 + 原点；BoundsCenter 为全部参与模型的世界包围盒中心
  FVector CutNormal = FVector::UpVector;
  FVector PlaneOrigin = FVector::ZeroVector;
  FVector BoundsCenter = FVector::ZeroVector;

  // 滑块进度 0~1，以及模型在法线方向上的投影范围
  // CutAmount 为当前剖切平面原点在法线方向上的投影比例
  float CutAmount = 0.0f;
  float ProjectionMin = 0.0f;
  float ProjectionMax = 0.0f;

  /** 投影范围是否有效（CollectTargetComponents/法线变化后重算）。 */
  bool bProjectionValid = false;

  // 更新请求与预览节流状态
  bool bUpdateRequested = false;
  bool bRequestedFinalCut = false;
  double LastPreviewUpdateTime = -1.0;

  // 本轮待切割的 RuntimeItems 索引队列及消费游标
  TArray<int32> PendingItemIndices;
  int32 PendingItemCursor = 0;

  bool bQueuedFinalCut = false;
  bool bQueueActive = false;
};

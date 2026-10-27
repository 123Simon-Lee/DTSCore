#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MeasureVisualizationActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstance;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
class UTextRenderComponent;

/**
 * 测量结果可视化 Actor（表现层）。
 *
 * 统一渲染一次测量产生的所有图元：
 * - 点（球体）、线段（圆柱）、矩形面（Plane）分别用三个
 *   InstancedStaticMeshComponent 管理，增删改都走 Instance，
 *   避免测量过程中反复 Spawn/Destroy Actor；
 * - 测量结果标签用 TextRenderComponent，每帧朝向相机（Billboard）。
 *
 * 工具组件通过 Add 系列方法拿到的返回值是 Instance 索引，
 * 预览拖动时用索引原地 Update，确认落点后保留。
 */
UCLASS(BlueprintType, Blueprintable)
class MEASUREMENT_API AMeasureVisualizationActor : public AActor
{
  GENERATED_BODY()

public:
  AMeasureVisualizationActor();

  virtual void Tick(float DeltaSeconds) override;
  virtual void OnConstruction(const FTransform &Transform) override;

  /** 添加一个测量点，返回实例索引（失败返回 INDEX_NONE）。 */
  int32 AddPoint(const FVector &WorldPosition);

  /** 原地更新测量点位置（预览拖动时使用）。 */
  bool UpdatePoint(int32 InstanceIndex, const FVector &WorldPosition);

  /** 添加一条测量线段，返回实例索引（失败返回 INDEX_NONE）。 */
  int32 AddLine(const FVector &Start, const FVector &End);

  /** 原地更新线段两端位置（预览拖动时使用）。 */
  bool UpdateLine(int32 InstanceIndex, const FVector &Start, const FVector &End);

  /**
   * 添加一个矩形测量面。
   * @param Origin 矩形起始角点
   * @param AxisU/AxisV 矩形两条边方向
   * @param SignedWidth/SignedHeight 带符号宽高（符号决定边延伸方向）
   */
  int32 AddRectangle(const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight);

  /** 原地更新矩形面（预览拖动时使用）。 */
  bool UpdateRectangle(int32 InstanceIndex, const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight);
  // 设置标签
  void SetLabel(const FText &Text, const FVector &WorldPosition);
  // 隐藏标签
  void HideLabel();
  // 清除所有
  void ClearAll();
  // 更新面积面
  bool UpdateAreaPolygon(
      const TArray<FVector> &WorldPoints,
      const FVector &PlaneOrigin,
      const FVector &PlaneAxisX,
      const FVector &PlaneAxisY,
      const FVector &PlaneNormal);
  // 更新面积标签（沿法线抬高避免嵌进面片）
  void UpdateAreaLabel(
      const FVector &LabelPosition,
      const FString &LabelText,
      const FVector &PlaneNormal);

  // 创建矩形面积面网格（四角点 + 两个三角形）
  void CreateAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneAxisX, const FVector &PlaneNormal);

  // 原地更新矩形面积面的四个顶点（预览拖动时使用）
  void UpdateAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneNormal);
  /**
   * 设置矩形面积面。
   * 第一次调用创建 Mesh，后续调用只更新四个顶点。
   */
  void SetAreaRectangle(const TArray<FVector> &WorldPoints, const FVector &PlaneAxisX, const FVector &PlaneNormal);
  /** 清除矩形面积面。 */
  void ClearAreaPolygon();

protected:
  virtual void BeginPlay() override;

  /** 把当前 Mesh/Material/文字样式配置应用到各渲染组件。 */
  void ApplyAssets();

  /** 由点位置计算球体实例的 Transform（按源网格直径换算缩放）。 */
  FTransform MakePointTransform(const FVector &Position) const;

  /** 由两端点计算圆柱实例的 Transform（居中、沿方向旋转、按长度缩放）。 */
  FTransform MakeLineTransform(const FVector &Start, const FVector &End) const;

  /** 由角点和两轴计算矩形实例的 Transform（含表面偏移防 Z-Fighting）。 */
  FTransform MakeRectangleTransform(const FVector &Origin, const FVector &AxisU, const FVector &AxisV, double SignedWidth, double SignedHeight) const;

public:
  // 场景根组件
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<USceneComponent> SceneRoot;
  // 实例化点静态网格体
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<UInstancedStaticMeshComponent> PointRenderer;
  // 实例化线静态网格体
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<UInstancedStaticMeshComponent> LineRenderer;
  // 实例化面静态网格体
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<UInstancedStaticMeshComponent> AreaRenderer;
  // 不规则面积多边形渲染组件（ProceduralMesh 动态建模）
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<UProceduralMeshComponent> AreaPolygonRenderer;
  // 字体渲染组件
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  TObjectPtr<UTextRenderComponent> TextRenderer;
  // 静态mesh对象
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UStaticMesh> PointMesh;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UStaticMesh> LineMesh;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UStaticMesh> AreaMesh;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UMaterialInterface> PointMaterial;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UMaterialInterface> LineMaterial;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementAssets")
  TObjectPtr<UMaterialInterface> AreaMaterial;
  // 点的世界空间直径 厘米
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float PointDiameter = 12.0f;
  // 线条的世界空间直径 里面
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float LineDiameter = 3.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float PointSourceDiameter = 100.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float LineSourceDiameter = 100.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float LineSourceLength = 100.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float AreaSourceSize = 100.0f;

  /**
   * 面积面离开被测表面的偏移，避免Z-Fighting。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementStyle")
  float AreaSurfaceOffset = 1.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  float TextWorldSize = 32.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  FColor TextColor = FColor(82, 230, 210);

  /**
   * 如果文字方向相反，可在蓝图里设置Yaw=180。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  FRotator TextFacingOffset = FRotator::ZeroRotator;

  /**
   * 标签材质（可选）。
   * 需要半透明 + Disable Depth Test 才能不被场景遮挡；
   * 不设置时使用 TextRender 默认材质（会被遮挡）。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  TObjectPtr<UMaterialInterface> LabelMaterial;

  /**
   * 标签屏幕大小系数：WorldSize = 相机距离 × 该系数。
   * 调大字变大，保证远处标签依然可读（近似恒定屏幕占比）。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText",
            meta = (ClampMin = "0.001"))
  float LabelDistanceScale = 0.02f;

  /** 动态字号的上下限（世界空间单位），防止过小看不见/过大糊屏。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  float LabelMinWorldSize = 8.0f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementText")
  float LabelMaxWorldSize = 200.0f;

private:
  // 矩形面积面是否已创建（区分 Create/Update 路径）
  bool bAreaRectangleCreated = false;
  // 缓存的字号
  float LastAppliedTextWorldSize = -1.0f;
};
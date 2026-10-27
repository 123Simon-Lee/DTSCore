#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "MeasureToolComponentBase.h"
#include "AreaMeasureComponent.generated.h"

// 面积测量变化委托
// AreaMeters：平方米面积；DisplayText：格式化显示文本；LabelWorldLocation：标签建议摆放位置
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAreaMeasurementChanged, double, AreaMeters, FText, DisplayText, FVector, LabelWorldLocation);

UCLASS(ClassGroup = (Measurement), meta = (BlueprintSpawnableComponent))

class MEASUREMENT_API UAreaMeasureComponent : public UMeasureToolComponentBase
{
  GENERATED_BODY()

public:
  UAreaMeasureComponent();
  virtual void ConfirmPoint() override;
  virtual void UndoLastPoint() override;
  virtual void FinishMeasurement() override;
  virtual void ResetMeasurement() override;

  /** 当前测量面积（平方米，只读）。 */
  UFUNCTION(BlueprintPure, Category = "MeasurementArea")
  double GetAreaSquareMeters() const
  {
    return AreaSquareMeters;
  }

  /** 预览中实时广播（拖动鼠标时持续触发）。 */
  UPROPERTY(BlueprintAssignable, Category = "MeasurementArea")
  FOnAreaMeasurementChanged OnAreaChanged;

  /** 确认提交时广播一次（UI 落定显示用）。 */
  UPROPERTY(BlueprintAssignable, Category = "MeasurementArea")
  FOnAreaMeasurementChanged OnAreaCommitted;

  /** 追加一个已确认顶点，并刷新点/边线可视化（≥3 点时同时重建面积面）。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  void AddConfirmedPoint(const FVector &WorldPoint);

  /** 按已确认顶点重建面积多边形网格并更新标签。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  void RefreshAreaMesh();

  /** 多边形顶点均值中心（沿法线抬高），作为标签摆放位置。 */
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  FVector GetPolygonLabelPosition() const;
  // 世界坐标 → 测量平面 2D 坐标（平面由原点和两条正交轴定义）
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  FVector2D ProjectPointToPlane2D(
      const FVector &WorldPoint,
      const FVector &InPlaneOrigin,
      const FVector &InPlaneAxisX,
      const FVector &InPlaneAxisY);
  // 测量平面 2D 坐标 → 世界坐标（投影的逆变换）
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  FVector Plane2DToWorld(
      const FVector2D &Point,
      const FVector &InPlaneOrigin,
      const FVector &InPlaneAxisX,
      const FVector &InPlaneAxisY);
  // 鞋带公式计算多边形面积（平方厘米），顶点需在同一平面内
  UFUNCTION(BlueprintCallable, Category = "MeasurementArea")
  double CalculatePolygonAreaCm2(const TArray<FVector> &WorldPoints,
                                 const FVector &InPlaneOrigin,
                                 const FVector &InPlaneAxisX,
                                 const FVector &InPlaneAxisY);

protected:
  virtual void HandleCursorHit(const FHitResult &HitResult) override;

  /** 记录首个顶点，并用命中结果建立测量平面（原点+法线+基向量）。 */
  void StartMeasurement(const FHitResult &HitResult);

  /** 用新的预览点更新面片与面积值。 */
  void UpdateMeasurement(const FVector &NewEndPoint);

  /** 由首个命中点建立测量平面的坐标基（Origin/AxisU/AxisV/Normal）。 */
  void BuildPlaneBasis(const FVector &WorldPoint);

  /** 把任意世界坐标点投影到测量平面上，顶点序列全部约束在平面内。 */
  FVector ProjectToMeasurementPlane(const FVector &WorldPoint) const;

  /** 按已确认顶点重建测量点与边线的可视化实例（顶点数量动态变化）。 */
  void RefreshPointsAndLines();

  /** 格式化面积显示文本。 */
  FText MakeAreaText() const;

protected:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementArea")
  int32 DecimalPlaces = 2;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MeasurementArea")
  float LabelNormalOffset = 20.0f;
  // 落点数组：已确认的多边形顶点（世界坐标，全部约束在测量平面内）
  UPROPERTY()
  TArray<FVector> ConfirmedWorldPoints;

  // 面积面渲染组件：ProceduralMesh 动态建模，顶点变化时重建网格段
  UPROPERTY()
  TObjectPtr<UProceduralMeshComponent> AreaMesh = nullptr;

private:
  // 当前楼层测量平面：原点 + 法线 + 两条正交轴构成的坐标基
  FVector PlaneOrigin = FVector::ZeroVector;
  FVector PlaneNormal = FVector::UpVector;
  FVector PlaneAxisU = FVector::ForwardVector;
  FVector PlaneAxisV = FVector::RightVector;

  // 预览矩形的四个角点（矩形测量模式使用）
  TArray<FVector> RectangleCorners;

  // 预览矩形的带符号宽高（符号决定矩形相对起点的延伸方向）
  double SignedWidth = 0.0;
  double SignedHeight = 0.0;
  // 当前测量面积（平方米）
  double AreaSquareMeters = 0.0;

  // 可视化 Actor 中测量点/边线对应的实例索引（与顶点一一对应）
  TArray<int32> PointInstances;
  TArray<int32> LineInstances;

  // 预览矩形面实例索引（INDEX_NONE 表示未创建）
  int32 AreaInstance = INDEX_NONE;

  // 最近一次光标命中的世界坐标（预览终点）
  FVector CursorWorldPosition = FVector::ZeroVector;
};

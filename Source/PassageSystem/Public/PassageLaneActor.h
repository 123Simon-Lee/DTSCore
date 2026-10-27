#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PassageTypes.h"

#include "PassageLaneActor.generated.h"

class UArrowComponent;

/**
 * 通行路线配置 Actor。
 *
 * 配置人物/车辆 Pawn 蓝图类，描述一条「进出闸口」的通行路线：
 * - Enter：OutsidePoint -> InsidePoint（进入）
 * - Exit：InsidePoint -> OutsidePoint（离开）
 *
 * 本类只负责配置与校验；UPassageSubsystem 从对象池租用独立实例完成通行。
 */
UCLASS(BlueprintType, Blueprintable)
class PASSAGESYSTEM_API APassageLaneActor : public AActor
{
  GENERATED_BODY()

public:
  APassageLaneActor();

  // 闸口外侧的地面路径点。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage")
  TObjectPtr<UArrowComponent> OutsidePoint;

  // 闸口内侧的地面路径点。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Passage")
  TObjectPtr<UArrowComponent> InsidePoint;

  // 通道唯一编号。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  FName ChannelId;

  // 与后端消息中的 deviceCode 对应。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  TArray<FString> DeviceCodes;

  // 人行通道或车辆通道。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage")
  EPassageSubjectType SubjectType = EPassageSubjectType::Person;

  // 推荐配置：人物或车辆蓝图类。相同类的通道共用对象池，不共用正在通行的实例。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Passage", meta = (DisplayName = "Passage Class"))
  TSubclassOf<APawn> PassageActorClass;

  UClass *ResolvePassageClass() const;
  UClass *GetPoolSource() const;

  // 闸机引用
  UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Passage")
  TObjectPtr<AActor> GateActor = nullptr;

  // 0：使用通行组件的 DefaultSpeed。
  // 大于 0：覆盖组件速度，单位为厘米/秒。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Passage", meta = (ClampMin = "0.0"))
  float SpeedOverride = 0.0f;

  /** 检查设备、路线及绑定对象是否有效；失败时输出原因。 */
  bool ValidateConfiguration(FString &OutError) const;

  /** 根据进出方向获取世界坐标路线（起点、终点）。 */
  void GetRoute(EPassageDirection Direction, FVector &OutStart, FVector &OutEnd) const;

protected:
  // 开始运行时向子系统注册本通道。
  virtual void BeginPlay() override;

  // 结束运行时向子系统注销本通道。
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

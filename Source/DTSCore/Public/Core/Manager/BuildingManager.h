// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/DataTable/BuildingFloorConfig.h"
#include "Core/Runtime/BuildingRuntimeData.h"
#include "Core/Adapter/BuildingSourceAdapter.h"
#include "Core/Camera/BuildingCameraFocusInterface.h"
#include "BuildingManager.generated.h"

class UBuildingFloorComponent;
class UBuildingFocusIsolationComponent;
// 运行时模式枚举
UENUM(BlueprintType)
enum class EBuildingActiveDisassembleMode : uint8
{
  None UMETA(DisplayName = "None"),
  Drawer UMETA(DisplayName = "Drawer"),
  Visibility UMETA(DisplayName = "Visibility"),
  LiftDrop UMETA(DisplayName = "LiftDrop")

};

USTRUCT(BlueprintType)
struct FSceneScope
{
  GENERATED_BODY()

public:
  /** 当前正在拆楼的楼栋 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  TObjectPtr<AActor> NowBuilding = nullptr;

  /** 楼栋 Code。 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  FString NowBuildingId;

  /** 当前选中的楼层 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  TObjectPtr<AActor> NowFloor = nullptr;

  /** 楼层 Code。 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  FString NowFloorId;

  /** 当前楼层编号 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  int32 NowFloorIndex = BUILDING_INVALID_FLOOR_INDEX;

  /** 当前活跃拆楼组件 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  TObjectPtr<UBuildingFloorComponent> NowComponent = nullptr;

  /** 当前活跃楼层节点 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  TObjectPtr<UBuildingRuntimeNode> NowFloorNode = nullptr;

  /** 当前拆楼模式 */
  UPROPERTY(BlueprintReadOnly, Category = "SceneScope")
  EBuildingActiveDisassembleMode NowMode =
      EBuildingActiveDisassembleMode::None;

public:
  bool HasActiveBuilding() const
  {
    return NowBuilding != nullptr && NowComponent != nullptr;
  }

  void Reset()
  {
    NowBuilding = nullptr;
    NowBuildingId.Reset();
    NowFloor = nullptr;
    NowFloorId.Reset();
    NowFloorIndex = BUILDING_INVALID_FLOOR_INDEX;
    NowComponent = nullptr;
    NowFloorNode = nullptr;
    NowMode = EBuildingActiveDisassembleMode::None;
  }
};

// 楼层点击事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnBuildingNodeClicked,
    UBuildingRuntimeNode *,
    Node,
    int32,
    FloorIndex);
// 楼层Hover事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnBuildingNodeHovered,
    UBuildingRuntimeNode *,
    Node,
    int32,
    FloorIndex);

/**
 * 建筑拆楼管理器
 *
 * Manager 只消费 IBuildingSourceAdapter 输出的通用 Actor RuntimeData。
 * 具体数据源由独立 Adapter 插件注册，可使用 Space、ActorTag、DataAsset 或 Datasmith Metadata。
 *
 * Manager 负责：
 * - 通过 Adapter 找到建筑
 * - 根据 DataTable 读取拆楼配置
 * - 递归构建 FBuildingRuntimeData
 * - 给每栋楼动态挂载 UBuildingFloorComponent
 * - 支持多栋楼独立拆楼
 * - 支持场景点击 / Hover
 */

// 通用层级：园区 Root -> Building Node -> Floor Node -> Controlled Actors
UCLASS()
class DTSCORE_API ABuildingManager : public AActor
{
  GENERATED_BODY()

public:
  ABuildingManager();

protected:
  virtual void BeginPlay() override;

public:
  virtual void Tick(float DeltaSeconds) override;

public:
  // =========================================================
  // Config
  // =========================================================

  /** 建筑拆楼配置表，RowName 推荐使用数据源提供的 BuildingId */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  TObjectPtr<UDataTable> BuildingFloorConfigTable = nullptr;

  /** Adapter 识别建筑的标签 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  FString BuildingMark = TEXT("Building");

  /** Adapter 识别楼层的标签 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  FString FloorMark = TEXT("BuildingFloor");

  /** 发现建筑前是否要求 Adapter 刷新自己的源数据。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  bool bRefreshSourceDataOnBeginPlay = false;

  /** 没找到建筑专属配置时，是否使用 Default Row */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  bool bUseDefaultConfigRow = true;

  /** 默认配置行名 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
  FName DefaultConfigRowName = TEXT("Default");

public:
  // =========================================================
  // Input
  // =========================================================

  /** 是否启用点击 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  bool bEnableClick = true;

  /** 是否启用 Hover */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  bool bEnableHover = true;

  /** 射线长度 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  float TraceLength = 100000.f;

  /** 射线通道 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
  TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

  /** 可选的拆楼相机实现；不指定时自动使用本地玩家 Pawn 上的接口实现。 */
  UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Focus")
  TScriptInterface<IBuildingCameraFocusInterface> CameraFocusProvider;

  /**
   * 通用聚焦隔离后处理。
   *
   * 组件仍挂在 Manager 上以兼容现有场景配置；拆楼和设备聚焦通过不同
   * Owner 提交请求，互不覆盖。材质仍可在 Manager 实例上统一配置。
   */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Focus|Isolation")
  TObjectPtr<UBuildingFocusIsolationComponent> FocusIsolationComponent = nullptr;

public:
  // =========================================================
  // Runtime
  // =========================================================
  // 当前拆楼器状态
  UPROPERTY(BlueprintReadOnly, Category = "Runtime")
  FSceneScope SceneScope;
  /** 每栋楼对应一个拆楼组件  缓存的楼栋对应的拆楼组件Map*/
  UPROPERTY(BlueprintReadOnly, Category = "Runtime")
  TMap<TObjectPtr<AActor>, TObjectPtr<UBuildingFloorComponent>> BuildingMap;

  /** 当前 Hover 节点 */
  UPROPERTY(BlueprintReadOnly, Category = "Runtime")
  TObjectPtr<UBuildingRuntimeNode> HoverNode = nullptr;

  /** 当前 Hover 组件 */
  UPROPERTY(BlueprintReadOnly, Category = "Runtime")
  TObjectPtr<UBuildingFloorComponent> HoverComponent = nullptr;

public:
  // =========================================================
  // Event
  // =========================================================

  UPROPERTY(BlueprintAssignable, Category = "Event")
  FOnBuildingNodeClicked OnBuildingNodeClicked;

  UPROPERTY(BlueprintAssignable, Category = "Event")
  FOnBuildingNodeHovered OnBuildingNodeHovered;

public:
  // =========================================================
  // Public API
  // =========================================================

  /** 初始化场景内所有建筑 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void InitializeBuildings();

  /** 非 Space 数据源可直接注册已经构建好的通用 RuntimeData。 */
  bool RegisterBuildingRuntimeData(const FBuildingRuntimeData &RuntimeData);

  /** 在 InitializeBuildings 前替换默认数据源适配器。 */
  void SetBuildingSourceAdapter(TUniquePtr<IBuildingSourceAdapter> &&InAdapter);

  /** 获取指定建筑的拆楼组件 */
  UFUNCTION(BlueprintPure, Category = "Manager")
  UBuildingFloorComponent *GetBuildingComponent(AActor *BuildingActor) const;

  /** 设置当前正在拆楼的楼栋 */
  void SetSceneScopeBuilding(
      AActor *BuildingActor,
      UBuildingFloorComponent *Component,
      EBuildingActiveDisassembleMode Mode);

  /** 设置当前选中的楼层 */
  void SetSceneScopeFloor(UBuildingRuntimeNode *FloorNode);

  /** 清空当前楼层 */
  void ClearSceneScopeFloor();

  /** 清空当前拆楼范围 */
  void ClearSceneScope();

  // 此时激活状态集合
  UFUNCTION(BlueprintPure, Category = "Runtime")
  const FSceneScope &GetSceneScope() const
  {
    return SceneScope;
  }
  // 此时激活楼栋
  UFUNCTION(BlueprintPure, Category = "Runtime")
  AActor *GetNowBuilding() const
  {
    return SceneScope.NowBuilding;
  }
  // 此时激活楼层 正在拆的楼层
  UFUNCTION(BlueprintPure, Category = "Runtime")
  AActor *GetNowFloor() const
  {
    return SceneScope.NowFloor;
  }
  // 此时激活的楼层number
  UFUNCTION(BlueprintPure, Category = "Runtime")
  int32 GetNowFloorIndex() const
  {
    return SceneScope.NowFloorIndex;
  }

  /** 指定建筑分层  拆楼核心调用*/
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void LayeringBuilding(AActor *BuildingActor);

  /** 指定建筑归位 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void BackBuildingToNormal(AActor *BuildingActor);

  /** 指定建筑切换楼层抽屉/显隐  */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void ToggleBuildingFloor(AActor *BuildingActor, int32 Floor);

  UFUNCTION(BlueprintCallable, Category = "Manager")
  void LayeringCurrentBuilding();

  /** 所有建筑同时分层 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void LayeringAllBuildings();

  /** 所有建筑同时归位 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  void BackAllBuildingsToNormal();

  /**
   * 通过各楼栋的拆楼组件统一设置全部楼栋显隐。
   * 隐藏前会退出当前拆楼状态并重置组件，显示时恢复全部楼栋。
   */
  UFUNCTION(BlueprintCallable, Category = "Visibility")
  void SetAllBuildingsVisibility(bool bVisible);

  /**
   * 隐藏所有拆楼组件管理的楼栋内容，但保留当前拆楼 SceneScope、楼层选择和模型位置
   *
   * 用于拆楼及选层全部完成后的设备物理独显:
   * SetAllBuildingsVisibility(false) 不同，本方法不会退出拆楼、重置楼层或清空当前楼栋/楼层状态
   */
  UFUNCTION(BlueprintCallable, Category = "Visibility")
  void HideAllBuildingContentPreserveDisassemble();

  /**
   * 设备独显结束后，按照仍保留的 SceneScope 重新应用拆楼显隐。
   * 不重置楼层位置、不退出拆楼，也不改变当前楼栋/楼层选择。
   */
  UFUNCTION(BlueprintCallable, Category = "Visibility")
  void RestoreBuildingContentFromPreservedDisassemble();

  // 根据稳定 ID 查找楼栋 Actor
  UFUNCTION(BlueprintPure, Category = "Manager")
  AActor *FindBuildingActorByCode(const FString &BuildingCode) const;

  /** 根据楼栋 Code 进入拆楼 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool LayeringBuildingByCode(const FString &BuildingCode);

  /**
   * 根据楼栋 Code 强制进入 Visibility 拆楼。
   * 仅供业务直接入口使用，不修改楼栋配置的默认拆楼模式。
   */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool EnterVisibilityBuildingByCode(const FString &BuildingCode);

  /**
   * 在强制 Visibility 拆楼中按楼层应用显隐。
   * 不经过配置模式自动分发，因此 Drawer 配置的楼栋也可以临时走显隐拆楼。
   */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool ToggleVisibilityBuildingFloorByCode(
      const FString &BuildingCode,
      int32 Floor);

  /** 根据楼栋 Code 还原 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool BackBuildingByCode(const FString &BuildingCode);

  /** 根据楼栋 Code 切换楼层 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool ToggleBuildingFloorByCode(const FString &BuildingCode, int32 Floor);

  /** 当前楼栋切换楼层 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool ToggleCurrentBuildingFloor(int32 Floor);

  /** 还原当前拆楼楼栋 */
  UFUNCTION(BlueprintCallable, Category = "Manager")
  bool BackCurrentBuildingToNormal();

private:
  // =========================================================
  // Initialize
  // =========================================================

  bool InitializeBuilding(const FBuildingSourceRecord &Source, const FBuildingFloorConfig &Config);

  FBuildingFloorConfig *FindConfigForBuilding(const FBuildingSourceRecord &Source) const;

  TUniquePtr<IBuildingSourceAdapter> BuildingSourceAdapter;

  /** 命中 Actor 的反向索引，避免射线交互时遍历全部楼栋。 */
  UPROPERTY(Transient)
  TMap<TObjectPtr<AActor>, TObjectPtr<UBuildingFloorComponent>> HitComponentMap;

  UPROPERTY(Transient)
  TMap<TObjectPtr<AActor>, TObjectPtr<UBuildingRuntimeNode>> HitNodeMap;

  void IndexComponentHitActors(UBuildingFloorComponent *Component);

  // =========================================================
  // Input
  // =========================================================

  bool DoTrace(FHitResult &OutHit);

  void HandleHover(const FHitResult &Hit);

  void HandleClick(const FHitResult &Hit);

  void ClearHover();

  bool ResolveHitNode(
      AActor *HitActor,
      UBuildingFloorComponent *&OutComponent,
      UBuildingRuntimeNode *&OutNode) const;

  // =========================================================
  // Utility
  // =========================================================

  // 高亮效果 自定义渲染通道控制开启关闭
  void SetActorHighlight(AActor *Actor, bool bEnable);

  // 退出当前激活建筑拆楼模式 每次切换新楼栋/模式都要调用
  // bRestoreCamera是都要进行视角记录
  void ExitActiveDisassemble(bool bRestoreCamera);

  UObject *ResolveCameraFocusProvider() const;
  void SaveCameraFocusViewIfNeeded();
  bool RequestBuildingFocus(UBuildingFloorComponent *Component);
  void RequestFloorFocus(
      UBuildingFloorComponent *Component,
      UBuildingRuntimeNode *FloorNode);
  // 回退相机聚焦
  void RestoreCameraFocusView();
  // 清除相机聚焦缓存
  void ClearCameraFocusView();

  UFUNCTION()
  void HandleLayeringFocusFinished();

  UFUNCTION()
  void HandleFloorFocusFinished();

  bool bCameraFocusViewSaved = false;

  // 进入当前激活建筑拆楼模式Drawer
  void EnterDrawerBuilding(
      AActor *BuildingActor,
      UBuildingFloorComponent *Component);
  // 进入当前激活建筑拆楼模式Visibility
  void EnterVisibilityBuilding(
      AActor *BuildingActor,
      UBuildingFloorComponent *Component);

  // 把所有建筑的楼层都设置为可见
  void RestoreAllBuildingsVisibility();

  // 只把一个建筑设置为可见，其他建筑都设置为不可见
  void SetOnlyBuildingVisible(UBuildingFloorComponent *VisibleComponent);

  // LiftDrop

  UFUNCTION(BlueprintPure, Category = "LiftDrop")
  bool IsLiftDropFocusReady() const
  {
    return SceneScope.NowMode ==
               EBuildingActiveDisassembleMode::LiftDrop &&
           bLiftDropFocusReady;
  }

  /** LiftDrop 镜头是否已经完成楼栋聚焦 */
  bool bLiftDropFocusReady = false;

  /** 等待 LiftDrop 相机聚焦完成 */
  FTimerHandle LiftDropFocusReadyTimer;

  /** 进入 LiftDrop 模式 */
  void EnterLiftDropBuilding(AActor *BuildingActor, UBuildingFloorComponent *Component);

  /** 相机移动时间结束，允许操作楼层 */
  void HandleLiftDropFocusReady();

public:
  // Debug

  /** 是否启用拆楼 Debug 快捷键 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
  bool bEnableDebugHotkeys = false;

  /** 当前 Debug 测试的楼栋下标 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
  int32 DebugBuildingIndex = 0;

  /** Debug：切换到指定下标楼栋并拆楼 */
  UFUNCTION(BlueprintCallable, Category = "Debug")
  void DebugSelectBuildingByIndex(int32 Index);

  /** Debug：切换下一栋楼并拆楼 */
  UFUNCTION(BlueprintCallable, Category = "Debug")
  void DebugNextBuilding();

  /** Debug：拆当前楼栋 */
  UFUNCTION(BlueprintCallable, Category = "Debug")
  void DebugLayeringCurrentBuilding();

  /** Debug：还原当前楼栋 */
  UFUNCTION(BlueprintCallable, Category = "Debug")
  void DebugBackCurrentBuilding();

  /** Debug：切换当前楼栋楼层 */
  UFUNCTION(BlueprintCallable, Category = "Debug")
  void DebugToggleCurrentFloor(int32 Floor);

private:
  void HandleDebugHotkeys();

  void GetDebugBuildingActors(TArray<AActor *> &OutBuildings) const;
};

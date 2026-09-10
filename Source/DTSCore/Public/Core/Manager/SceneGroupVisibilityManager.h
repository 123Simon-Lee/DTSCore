// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SceneGroupVisibilityManager.generated.h"

// 配置
USTRUCT(BlueprintType)
struct FSceneVisibilityGroupConfig
{
  GENERATED_BODY()
public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FString GroupKey;

  UPROPERTY(EditInstanceOnly, BlueprintReadWrite)
  TObjectPtr<AActor> RootActor = nullptr;

  /**
   * 同一个组需要包含多个互不挂接的 Actor 树时，在这里补充它们的根节点。
   */
  UPROPERTY(EditInstanceOnly, BlueprintReadWrite)
  TArray<TObjectPtr<AActor>> AdditionalRootActors;

  /**
   * 可选的运行时分组标签。
   * Outliner 文件夹不会保留为可靠的运行时父子关系；需要按文件夹批量控制时，
   * 可以给文件夹中的 Actor 添加同一个 Actor Tag，并在这里填写该 Tag。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FName ActorTag = NAME_None;
};
// 运行时缓存
USTRUCT()
struct FSceneVisibilityRuntimeGroup
{
  GENERATED_BODY()
public:
  UPROPERTY(Transient)
  TArray<TObjectPtr<AActor>> Actors;
};

UCLASS()
class DTSCORE_API ASceneGroupVisibilityManager : public AActor
{
  GENERATED_BODY()

public:
  // Sets default values for this actor's properties
  ASceneGroupVisibilityManager();

public:
  /**
   * 在编辑器中配置场景根节点
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scene Visibility")
  TArray<FSceneVisibilityGroupConfig> SceneGroups;

public:
  UFUNCTION(BlueprintCallable, Category = "Scene Visibility")
  void InitializeSceneGroupCache();

  /**
   * 根据 Selected Keys 统一处理单选、多选
   *
   * 空数组：
   * 全部隐藏
   *
   * 一个元素：
   * 单选
   *
   * 多个元素：
   * 多选
   */
  UFUNCTION(BlueprintCallable, Category = "Scene Visibility")
  void ApplySelectedGroups(const TArray<FString> &SelectedKeys);

  /**
   * 单独控制一个场景组
   */
  UFUNCTION(BlueprintCallable, Category = "Scene Visibility")
  void SetGroupVisible(const FString &GroupKey, bool bVisible);

  /**
   * 显示全部
   */
  UFUNCTION(BlueprintCallable, Category = "Scene Visibility")
  void ShowAllGroups();

  /**
   * 隐藏全部
   */
  UFUNCTION(BlueprintCallable, Category = "Scene Visibility")
  void HideAllGroups();

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;
  // Called every frame
  virtual void Tick(float DeltaTime) override;

private:
  /**
   * Key -> Actor缓存
   */
  UPROPERTY(Transient)
  TMap<FString, FSceneVisibilityRuntimeGroup> SceneGroupCache;

  /**
   * 当前显示中的组
   */
  TSet<FString> VisibleGroupKeys;

  /** 缓存是否至少执行过一次初始化。 */
  bool bSceneGroupCacheInitialized = false;

  /**
   * 上一次整体状态
   */
  bool bLastAnyVisible = false;

private:
  /**
   * 递归收集：
   *
   * RootActor
   * +
   * 所有 Attached Actor
   */
  void CollectActorTree(AActor *RootActor, TArray<TObjectPtr<AActor>> &OutActors);

  /** 在公开显隐接口被过早调用时，自动补建一次缓存。 */
  void EnsureSceneGroupCacheInitialized();

  /**
   * 内部设置一个组显隐
   */
  void SetGroupVisibleInternal(const FString &GroupKey, bool bVisible);
};

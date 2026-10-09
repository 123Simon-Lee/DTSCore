// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActorActionUtility.h"
#include "Engine/DataTable.h"
#include "SpaceScript.generated.h"

/**
 * 空间脚本
 * 用于在编辑器中根据空间结构数据表，在世界中生成和维护 SpaceActor(ASpace)。
 */
UCLASS()
class SPACESYSTEMUNITEDITOR_API USpaceScript : public UActorActionUtility
{
  GENERATED_BODY()

public:
  /**
   * 在世界中寻找 SpaceActor(ASpace)，先根据其 Label 初始化 Code，
   * 再根据 FWorldSpaceStructureRow 数据表设置其 DisplayName。
   *
   * @param DataTable FWorldSpaceStructureRow 类型的数据表
   */
  UFUNCTION(CallInEditor, BlueprintCallable, Category = "Space Script")
  void InitializeSpaceActorsFromDataTable(UDataTable *DataTable);

  /**
   * 根据数据表各行的 ParentCode，自动在世界中建立 Space 之间的附加关系。
   * Code 在数据表中找不到对应行，或 ParentCode 为空（根空间）时，
   * 解除该 Space 与父空间的附加关系。
   *
   * @param DataTable FWorldSpaceStructureRow 类型的数据表
   */
  UFUNCTION(CallInEditor, BlueprintCallable, Category = "Space Script")
  void AttachSpacesFromDataTable(UDataTable *DataTable);

  /**
   * 仅处理选中的 Actor：分割 Label 后取指定段作为表行名，按表行自身的 Code 查找 Space 并直接附加，保持世界变换。
   * 遍历世界查找目标 Space，只修改选中 Actor 的附加关系，目标 Space 无需选中。
   * 找不到表行或对应 Space 时跳过，保留原附加关系。
   * @param DataTable FWorldSpaceStructureRow 类型的数据表
   * @param Separator Label 的划分符号，例如 "-"
   * @param ValueIndex 从 0 开始的取值索引，例如 1 取 A_1F_FJ_005-ColorBlock 中的 ColorBlock
   */
  UFUNCTION(CallInEditor, BlueprintCallable, Category = "Space Script")
  void AttachSelectedFromDataTable(UDataTable *DataTable, FString Separator = TEXT("-"), int32 ValueIndex = 1);
};

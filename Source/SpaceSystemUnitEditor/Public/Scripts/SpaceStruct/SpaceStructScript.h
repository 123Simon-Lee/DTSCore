// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActorActionUtility.h"
#include "SpaceStructScript.generated.h"

class UDataTable;

/**
 * 空间结构编辑器脚本。
 */
UCLASS()
class SPACESYSTEMUNITEDITOR_API USpaceStructScript : public UActorActionUtility
{
	GENERATED_BODY()

public:
	/**
	 * 根据各空间结构的 GetAttachParentCode 规则附加到目标空间，保持世界变换。
	 * @param DataTable FWorldSpaceStructureRow 类型的空间结构数据表
	 * @param bTraverseWholeWorld true 遍历当前编辑器世界；false 仅处理当前选中的空间结构
	 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "SpaceStruct Scripts")
	void AttachSpaceStructsFromDataTable(UDataTable* DataTable, bool bTraverseWholeWorld = false);
};

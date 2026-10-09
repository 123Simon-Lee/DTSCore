// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Core/DataTable/BuildingFloorConfig.h"
#include "Space/SpaceSubsystem.h"
#include "BuildingSpaceStructureRow.generated.h"

/**
 * 合并后的空间结构 + 楼栋拆楼配置行。
 *
 * 用于替代原来的两张表：
 * - DT_WorldSpaceStructure（FHarbinWorldSpaceStructureRow）：空间拓扑 + 楼层序号；
 * - DT_BuildingFloorConfig（FBuildingFloorConfig）：楼栋级拆楼配置。
 *
 * 字段来源：
 * - Code / DisplayName / ParentCode / SubCode 来自 FWorldSpaceStructureRow；
 * - Index 为楼层序号（原 FHarbinWorldSpaceStructureRow.Index，例如 -1 / 1 / 9999(RF)）；
 * - BuildingConfig 为楼栋级拆楼配置，只有楼栋行（ParentCode 为空）填写，
 *   楼层 / 房间 / 室外行保持默认值。
 *
 * RowName 约定为 Code，与 DT_WorldSpaceStructure 一致。
 */
USTRUCT(BlueprintType)
struct DTSCORE_API FBuildingSpaceRow : public FWorldSpaceStructureRow
{
	GENERATED_BODY()

	/** 楼层序号，例如 -1、1、2、9999(RF)；非楼层行不使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldSpaceStructureRow")
	FString Index = "";

	/** 楼栋级拆楼配置，仅楼栋行（ParentCode 为空）填写。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building Config")
	FBuildingFloorConfig BuildingConfig;

#if WITH_EDITOR
	using FWorldSpaceStructureRow::OnDataTableChanged;
#endif
};

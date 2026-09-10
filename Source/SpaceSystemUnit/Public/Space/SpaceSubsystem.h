// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceSubsystem.generated.h"

/**
 * 描述当前场景空间结构数据表行
 */
USTRUCT(BlueprintType)
struct FWorldSpaceStructureRow : public FTableRowBase
{
	GENERATED_BODY()
	
	// 自身Code
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FString Code = "";

	// 自身命名
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FString Name = "";
	
	// 父Code
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	FString ParentCode = "";
	
	// 子Code
	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	TSet<FString> SubCode;
};

class USpacePartition;
class ASpace;

/**
 * 空间子系统
 * 拥有空间映射以及空间的一些帮助函数
 */

UCLASS()
class SPACESYSTEMUNIT_API USpaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 注册一个空间使其可查
	 * @param Space 空间类指针
	 * @return 是否成功注册该空间
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	bool RegisterSpace(ASpace* Space);

	/**
	 * 取消注册一个空间，使其不可查
	 * @param SpaceCode 空间编码
	 * @return 是否取消注册成功 
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	bool UnregisterSpace(FString SpaceCode);

	/**
	 * 寻找一个空间
	 * @param SpaceCode 该空间的编号 
	 * @return 空间指针，未找到则返回空指针
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	ASpace* GetSpace(FString SpaceCode);

	/**
	 * 销毁一个空间
	 * @param SpaceCode 待移除空间的编码
	 * @param bReassignChildrenToParent 是否维持上下级关系（将删除的子空间附加到父空间上） 
	 * @return 是否销毁成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	bool DestroySpace(FString SpaceCode,bool bReassignChildrenToParent);

	/**
	 * 获取所有已注册 Space
	 * @retrun 系统内所有的Space
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	const TArray<ASpace*>& GetSpaces() const;
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	const TMap<FString, ASpace*>& GetSpaceMap() const;


	/**
	 * 根据 Mark 获取所有已注册 Space
	 * @param Mark 待查询Mark
	 * @return 返回拥有此Mark的全部Space
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")

	TArray<ASpace*> GetSpacesByMark(const FString& Mark) const;
private:
	// 所有空间数组
	UPROPERTY()
	TArray<ASpace*> Spaces;
	
	// 空间编码与空间指针映射
	UPROPERTY()
	TMap<FString,ASpace*> SpaceMap;

	// 空间Mark集合
	UPROPERTY()
	TSet<FString> SpaceMarkSet;
	
	// 空间分区
	UPROPERTY()
	TMap<FString,USpacePartition*> SpacePartitionMap;
};

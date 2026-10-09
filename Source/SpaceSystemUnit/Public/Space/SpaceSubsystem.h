// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceSubsystem.generated.h"

/**
 * 空间选择模式
 * 控制空间子系统是否允许选择以及选择的规则
 */
UENUM(BlueprintType)
enum class ESpaceSelectMode : uint8
{
	// 不可选择，任何选择请求都会被拒绝
	None = 0,
	// 单选
	Single = 1,
	// 多选
	Multi = 2,
};

/**
 * 描述当前场景空间结构数据表行
 */
USTRUCT(BlueprintType)
struct SPACESYSTEMUNIT_API FWorldSpaceStructureRow : public FTableRowBase
{
	GENERATED_BODY()
	// 自身Code
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString Code = "";

	// 自身命名
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString DisplayName = "";

	// 父Code
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	FString ParentCode = "";

	// 子Code
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TSet<FString> SubCode;

#if WITH_EDITOR
	// 在编辑器中修改行数据时，自动把行名同步为 Code 的值
	virtual void OnDataTableChanged(const UDataTable* InDataTable, const FName InRowName) override;
#endif
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
	bool DestroySpace(FString SpaceCode, bool bReassignChildrenToParent);

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

	/**
	 * 获取目前选择的空间
	 * @return 目前选择的空间
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	TArray<ASpace*> GetSelection() { return SelectedSpaces; }

	/**
	 * 清空当前所有已选择空间，并向其发送取消选择通知
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	void ClearSelection();

	/**
	 * 设置空间选择模式，设置前会先清空当前选择
	 * @param NewMode 新的选择模式
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	void SetSelectMode(ESpaceSelectMode NewMode);

	/**
	 * 互斥单选一个空间，并通知该 Space 上的 USpaceSelectorComponent
	 * @param NewSpace 待选中的空间
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	void ToggleSingleSelect(ASpace* NewSpace);

	/**
	 * 互斥多选一组空间
	 * @param NewSpaces 待选中的空间数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	void ToggleMultipleSelect(const TArray<ASpace*>& NewSpaces);

	/**
	 * 在多选模式下追加选择一组空间
	 * @param NewSpaces 待追加选择的空间数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space Subsystem")
	void AddMultipleSelect(const TArray<ASpace*>& NewSpaces);

private:
	// 所有空间数组
	UPROPERTY()
	TArray<ASpace*> Spaces;

	// 空间编码与空间指针映射
	UPROPERTY()
	TMap<FString, ASpace*> SpaceMap;

	// 空间Mark集合
	UPROPERTY()
	TSet<FString> SpaceMarkSet;

	// 空间分区
	UPROPERTY()
	TMap<FString, USpacePartition*> SpacePartitionMap;

	// 当前已选择的空间数组
	UPROPERTY()
	TArray<ASpace*> SelectedSpaces;

	// 当前空间选择模式
	UPROPERTY()
	ESpaceSelectMode SelectMode = ESpaceSelectMode::None;
};

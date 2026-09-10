// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Space.generated.h"


class ASpace;
class ASpaceStruct;

/*
 * 空间类型枚举
 */
UENUM()
enum class ESpaceType : uint8
{
	// 未知类型,该空间尚未初始化
	UnKnown = 0,
	// 根空间，即上层无父节点
	Root = 1,
	// 分支空间，同时拥有上层以及下层空间
	Branch = 2,
	// 叶子空间，即无下层空间
	Leaf = 3,
	// 孤立空间，即无下层空间也无上层空间
	Isolated = 4, 
};

/**
 * 空间分区
 * 空间编码映射的包装对象
 */
UCLASS()
class SPACESYSTEMUNIT_API USpacePartition : public UObject
{
	GENERATED_BODY()
public:
	// 空间编码映射
	UPROPERTY()
	TMap<FString,ASpace*> SpacesMap;
};

/**
 * 空间结构分区
 * 空间结构编码映射的包装对象
 */
UCLASS()
class SPACESYSTEMUNIT_API USpaceStructPartition : public UObject
{
	GENERATED_BODY()
public:
	// 空间结构编码映射
	UPROPERTY()
	TMap<FString,ASpaceStruct*> StructMap;
};

/**
 * 空间类
 * 描述一个空间信息，比如停车场，园区，或者某栋楼，某个楼层
 * 他可以有一个上级空间，可以拥有多个下级空间，通过附加关系决定
 * 他会拥有一个空间结构类数组以及一个其他结构类数组。不做特殊处理的统一视为其他结构。
 * 编码不允许重复
 */
UCLASS()
class SPACESYSTEMUNIT_API ASpace : public AActor
{
	GENERATED_BODY()

	friend class USpaceSubsystem;

public:
	// Sets default values for this actor's properties
	ASpace();

	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// 构造时获取信息函数
	void GatherConstructionData();

	// 初始化空间
	UFUNCTION(BlueprintCallable, Category = "Space")
	void Init();

	/**
	 * 根据附加关系初始化空间数据
	 */
	UFUNCTION(BlueprintCallable,CallInEditor, Category = "Space")
	void InitializeSpaceData();

	/**
	 * 清空空间数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	void ResetSpaceData();

	
	//				Space方法			//

	/**
	 * 该空间类是否拥有某一个Mark
	 * @param Mark 待判断Mark
	 * @return 是否拥有该标记
	 */
	UFUNCTION(BlueprintPure, Category = "Space")
	bool HasMark(const FString& Mark) const;
	
	/**
	 * 获取父空间指针
	 * @return 父空间指针
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	ASpace* GetParentSpace() const;

	/**
	 * 获取子空间指针数组
	 * @return 子空间指针数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	const TArray<ASpace*>& GetSubspaces() const;

	/**
	 * 获取全部子空间指针与代码映射
	 * @return 子空间指针与代码映射
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	const TMap<FString, ASpace*>& GetSubspaceMap() const;
	
	/**
	 * 根据Mark在空间分区查询并获取所有该分区子空间
	 * 即获取所有拥有该Mark的子空间
	 * @param Mark 查询Mark
	 * @return 拥有该Mark的子空间数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	TArray<ASpace*> GetSubspacesByMark(const FString& Mark) const;

	/**
	 * 设置当前空间内普通附属 Actor 的显隐状态。
	 *
	 * 普通附属 Actor 不包含子空间（ASpace）和空间结构（ASpaceStruct），
	 * 因此可用于只显隐楼栋/楼层模型，而不影响设备、摄像头、管线等结构对象。
	 *
	 * @param bNewHidden true 隐藏，false 显示
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	void SetOrdinaryActorsHidden(bool bNewHidden);


	//				Struct方法			//

	/**
	 * 该空间内，是否有空间结构拥有此Mark
	 * @param Mark 查询Mark
	 * @return 是否拥有该Mark所标记的空间结构
	 */
	UFUNCTION(BlueprintPure, Category = "Space")
	bool HasStructWithMark(const FString& Mark) const;

	/**
	 * 获取该空间内空间结构的全部Mark集合
	 * @return 该空间内空间结构的全部Mark集合
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	const TSet<FString>& GetStructMarksSet() const;
	
	/**
	 * 返回此空间的所有空间结构
	 * @return 空间结构数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	const TArray<ASpaceStruct*>& GetStructs() const;

	/**
	 * 在此空间的所有空间结构查询对应Code的空间结构
	 * @param TargetCode 查询Code
	 * @return 对应的空间结构
	 */
	UFUNCTION(BlueprintPure, Category = "Space")
	ASpaceStruct* GetStructByCode(const FString& TargetCode) const;

	/**
	 * 根据Mark在空间结构分区查询并获取所有该分区空间空间结构
	 * 即获取所有拥有该Mark的空间结构
	 * @param Mark 查询Mark
	 * @return 拥有该Mark的空间结构数组
	 */
	UFUNCTION(BlueprintCallable, Category = "Space")
	TArray<ASpaceStruct*> GetStructByMark(const FString& Mark) const;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	/**
	 * 销毁函数
	 */
	virtual void Destroyed() override;
	
	/**
	 * 根据类别区分附加Actor数据
	 * @param Actor 附加的Actor
	 * @return 是否将此附加Actor区分完成
	 */
	virtual bool DistinguishAttachActor(AActor* Actor);

	/**
	 * 注册到子系统
	 * @return 是否成功
	 */
	bool RegisterSpace();

public:
	// 空间编码
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame , Category = "SpaceSetting")
	FString Code = "";
	
	// 空间显示名称
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame, Category = "SpaceSetting")
	FString DisplayName = "Space";

	// 空间标记，可以依据这个来寻找空间
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame, Category = "SpaceSetting")
	TSet<FString> Marks;
	
protected:
	
	// 该空间是否有效：已经初始化且进入管理器
	UPROPERTY()
	bool bIsValid = false;
	
	// 附加的actor
	UPROPERTY()
	TArray<AActor*> AttachActors;

	// 无需查找的普通对象结构,排除空间类和空间结构类
	UPROPERTY()
	TArray<AActor*> OrdinaryActors;
	
		/*空间*/
	
	// 空间类型
	ESpaceType SpaceType = ESpaceType::UnKnown;
	
	// 上级空间指针
	UPROPERTY()
	ASpace* ParentSpace = nullptr;
	
	// 子空间指针
	UPROPERTY()
	TArray<ASpace*> Subspaces;
	
	// 子空间Code与指针Map
	UPROPERTY()
	TMap<FString,ASpace*> SubspaceMap;

	// 子空间Mark集合
	UPROPERTY()
	TSet<FString> SubspaceMarkSet;

	// 子空间分区
	UPROPERTY()
	TMap<FString,USpacePartition*> SubspacePartitionMap;

		/*空间结构*/
	
	// 空间内含空间结构
	UPROPERTY()
	TArray<ASpaceStruct*> Structs;

	// 空间结构映射
	UPROPERTY()
	TMap<FString, ASpaceStruct*> StructMap;

	// 空间结构Mark集合
	UPROPERTY()
	TSet<FString> StructMarksSet;

	// 空间结构分区，Mark与空间分区的Map
	UPROPERTY()
	TMap<FString,USpaceStructPartition*> StructPartitionMap;
};

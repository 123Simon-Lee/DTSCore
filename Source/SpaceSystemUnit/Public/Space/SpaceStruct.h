// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceSystemUnit/Public/Space/Space.h"
#include "SpaceStruct.generated.h"


class ASpace;

/**
 * 空间结构
 * 描述一个空间包含的特殊内容物。比如设备，漫游点。
 * 他只会拥有一个上级空间。
 * 编码不允许重复
 */

UCLASS()
class SPACESYSTEMUNIT_API ASpaceStruct : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASpaceStruct();
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	/** 获取当前空间结构所属的父空间。 */
	UFUNCTION(BlueprintPure, Category = "SpaceStruct")
	ASpace* GetParentSpace() const;
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// 构造时获取信息函数
	void GatherConstructionData();

public:

	// 空间结构编码
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame , Category = "SpaceStructSetting")
	FString Code = "";
	
	// 空间结构显示名称
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame, Category = "SpaceStructSetting")
	FString DisplayName = "SpaceStruct";

	// 空间结构标记，可以依据这个来寻找或区分空间结构
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, SaveGame, Category = "SpaceStructSetting")
	TSet<FString> Marks;

protected:
	friend class ASpace;
	// 父类空间指针
	UPROPERTY()
	TWeakObjectPtr<ASpace> Parent; 
};

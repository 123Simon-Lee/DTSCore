// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Space/Space.h"

#include "SpaceSelectorComponent.generated.h"

/**
 * 空间选择组件，实现此接口以在当前空间收到选择信息的时候触发逻辑
 * @attention 由于空间子系统只能选择空间，请将此组件挂载到空间子类
 */
UCLASS(Abstract, meta = (BlueprintSpawnableComponent))
class SPACESYSTEMUNIT_API USpaceSelectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USpaceSelectorComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 当前空间被选择
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceSelector")
	virtual void OnSpaceSelected();

	/**
	 * 当前空间取消选择
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceSelector")
	virtual void OnSpaceDeselected();

	UFUNCTION(BlueprintPure, Category = "SpaceSelector")
	bool IsSpaceSelected() const { return bSpaceSelected; }

	UFUNCTION(BlueprintPure, Category = "SpaceSelector")
	ASpace* GetSpaceOwner() const { return Cast<ASpace>(GetOwner()); }

protected:
	UPROPERTY(BlueprintReadOnly, Category = "SpaceSelector")
	bool bSpaceSelected = false;
};

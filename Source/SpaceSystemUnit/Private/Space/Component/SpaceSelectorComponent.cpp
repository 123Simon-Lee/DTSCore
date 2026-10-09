// Fill out your copyright notice in the Description page of Project Settings.


#include "Space/Component/SpaceSelectorComponent.h"
#include "SpaceSystemUnit.h"


// Sets default values for this component's properties
USpaceSelectorComponent::USpaceSelectorComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void USpaceSelectorComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

}


// Called every frame
void USpaceSelectorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

// 当前空间被选择：打印当前空间信息
void USpaceSelectorComponent::OnSpaceSelected()
{
	bSpaceSelected = true;
	// 获取组件所属的所有者并转换为空间
	ASpace* CurrentSpace = Cast<ASpace>(GetOwner());
	if (!IsValid(CurrentSpace))
	{
		UE_LOG(LogSpaceSystem, Warning, TEXT("OnSpaceSelected: Owner is not a valid Space."));
		return;
	}

	// 打印当前空间编码与显示名
	UE_LOG(
		LogSpaceSystem,
		Log,
		TEXT("OnSpaceSelected: Space=[%s] DisplayName=[%s]"),
		*CurrentSpace->Code,
		*CurrentSpace->DisplayName
	);
}

// 当前空间取消选择：打印当前空间信息
void USpaceSelectorComponent::OnSpaceDeselected()
{
	bSpaceSelected = false;
	// 获取组件所属的所有者并转换为空间
	ASpace* CurrentSpace = Cast<ASpace>(GetOwner());
	if (!IsValid(CurrentSpace))
	{
		UE_LOG(LogSpaceSystem, Warning, TEXT("OnSpaceDeselected: Owner is not a valid Space."));
		return;
	}

	// 打印当前空间编码与显示名
	UE_LOG(
		LogSpaceSystem,
		Log,
		TEXT("OnSpaceDeselected: Space=[%s] DisplayName=[%s]"),
		*CurrentSpace->Code,
		*CurrentSpace->DisplayName
	);
}

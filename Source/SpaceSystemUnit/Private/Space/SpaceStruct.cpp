// Fill out your copyright notice in the Description page of Project Settings.


#include "Space/SpaceStruct.h"


// Sets default values
ASpaceStruct::ASpaceStruct()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	this->RootComponent = CreateDefaultSubobject<USceneComponent>(FName("Root"));
	this->RootComponent->SetMobility(EComponentMobility::Movable);
	this->GatherConstructionData();
}

ASpace* ASpaceStruct::GetParentSpace() const
{
	return Parent.Get();
}

// Called when the game starts or when spawned
void ASpaceStruct::BeginPlay()
{
	Super::BeginPlay();
}

void ASpaceStruct::GatherConstructionData()
{
	// 默认使用自身命名作为编码
	this->Code = this->GetActorNameOrLabel();
}

// Called every frame
void ASpaceStruct::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


// Fill out your copyright notice in the Description page of Project Settings.


#include "SceneWidgetActor.h"

// Sets default values
ASceneWidgetActor::ASceneWidgetActor()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	// Root
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// WidgetComponent
	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	WidgetComp->SetupAttachment(Root);
}

// Called when the game starts or when spawned
void ASceneWidgetActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASceneWidgetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ASceneWidgetActor::SetWidgetClass(TSubclassOf<UUserWidget> InClass)
{
	if (!InClass) return;

	WidgetClass = InClass;
	WidgetComp->SetWidgetClass(InClass);
}
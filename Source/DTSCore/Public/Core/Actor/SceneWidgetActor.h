// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <Components/WidgetComponent.h>
#include "SceneWidgetActor.generated.h"


UCLASS()
class DTSCORE_API ASceneWidgetActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ASceneWidgetActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
	// 根组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	USceneComponent* Root;

	// Widget组件（核心）
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite)
	UWidgetComponent* WidgetComp;

	// 设置Widget类
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<UUserWidget> WidgetClass;

	// 设置Widget
	UFUNCTION(BlueprintCallable)
	void SetWidgetClass(TSubclassOf<UUserWidget> InClass);

};

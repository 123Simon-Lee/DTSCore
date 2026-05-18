// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/TimelineComponent.h"
#include "DoorTrigger.generated.h"
///此类用于实现自动门功能，玩家进入触发区域后门会自动打开，离开后自动关闭。
//自动门状态枚举

UENUM(BlueprintType)
enum class EDoorState : uint8
{
	Closed      UMETA(DisplayName = "Closed"),
	Opening     UMETA(DisplayName = "Opening"),
	Opened      UMETA(DisplayName = "Opened"),
	Closing     UMETA(DisplayName = "Closing")
};

UCLASS()
class DTSCORE_API ADoorTrigger : public AActor
{
	GENERATED_BODY()
public:
	UPROPERTY()
	EDoorState DoorState = EDoorState::Closed;
	/** ===== Components ===== */
	UPROPERTY(VisibleAnywhere)
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere)
	UBoxComponent* TriggerBox;

	UPROPERTY()
	int32 OverlapPawnCount = 0;

	/** ===== Door ===== */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")
	AActor* DoorActor = nullptr;

	UPROPERTY(EditAnywhere, Category = "Door")
	float OpenAngle = 90.f;

	/** ===== Timeline ===== */
	UPROPERTY()
	UTimelineComponent* DoorTimeline;

	UPROPERTY(EditAnywhere, Category = "Door")
	UCurveFloat* DoorCurve;

	/** ===== Rotation Cache ===== */
	FRotator ClosedRot;
	FRotator OpenRot;

	bool bDirectionResolved = false;
	
public:	
	// Sets default values for this actor's properties
	ADoorTrigger();

	/** ===== Trigger ===== */
	UFUNCTION(BlueprintCallable, Category = "Trigger Event")
	void OnTriggerBegin(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);

	UFUNCTION(BlueprintCallable, Category = "Trigger Event")
	void OnTriggerEnd(
		UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex
	);

	UFUNCTION(BlueprintCallable, Category = "TimeLine Event")
	void OnTimelineUpdate(float Alpha);
	UFUNCTION(BlueprintCallable, Category = "TimeLine Event")
	void OnTimelineFinished();
	UFUNCTION(BlueprintCallable, Category = "DoorFunction")
	FVector GetDoorNormal();
	UFUNCTION(BlueprintCallable, Category = "DoorTriggerInit")
	void InitDoorTrigger();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

};

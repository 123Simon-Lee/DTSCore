// Fill out your copyright notice in the Description page of Project Settings.


#include "DoorTrigger.h"

// Sets default values
ADoorTrigger::ADoorTrigger()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	TriggerBox->SetupAttachment(Root);
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));

	DoorTimeline = CreateDefaultSubobject<UTimelineComponent>(TEXT("DoorTimeline"));
}



// Called when the game starts or when spawned
void ADoorTrigger::BeginPlay()
{
	Super::BeginPlay();
	if (!DoorActor || !TriggerBox) return;
	//缓存初始旋转
	ClosedRot = DoorActor->GetActorRotation();
	//绑定触发事件
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ADoorTrigger::OnTriggerBegin);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ADoorTrigger::OnTriggerEnd);
	//绑定timeline
	if (DoorCurve)
	{
		FOnTimelineFloat UpdateFunc;
		UpdateFunc.BindUFunction(this, FName("OnTimelineUpdate"));
		DoorTimeline->AddInterpFloat(DoorCurve, UpdateFunc);
		FOnTimelineEvent FinishedFunc;
		FinishedFunc.BindUFunction(this, FName("OnTimelineFinished"));
		DoorTimeline->SetTimelineFinishedFunc(FinishedFunc);
	}
	FVector Origin;
	FVector Extent;
	DoorActor->GetActorBounds(true, Origin, Extent);
	TriggerBox->SetBoxExtent(Extent + FVector(10.f));
	SetActorLocation(Origin);
}

// Called every frame
void ADoorTrigger::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ADoorTrigger::OnTriggerBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!DoorActor) return;


	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) return;
	OverlapPawnCount++;
	if (DoorState != EDoorState::Closed)
		return;

	// ===== 人相对门的位置 =====
	FVector DoorToPawn = Pawn->GetActorLocation() - DoorActor->GetActorLocation();
	DoorToPawn.Z = 0.f;

	if (DoorToPawn.SizeSquared() < 10.f)
	{
		return;
	}
	DoorToPawn.Normalize();

	// ===== 1. 取门面法线（可能正也可能反）=====
	FVector DoorNormal = GetDoorNormal();
	DoorNormal.Z = 0.f;
	DoorNormal.Normalize();


	if (FVector::DotProduct(DoorNormal, DoorToPawn) > 0.f)
	{
		DoorNormal *= -1.f;
	}

	// ===== 3. 用修正后的法线算门侧向 =====
	const FVector DoorSide =
		FVector::CrossProduct(FVector::UpVector, DoorNormal).GetSafeNormal();

	// ===== 4. 判断人在哪一侧 =====
	const float SideDot = FVector::DotProduct(DoorSide, DoorToPawn);
	const float OpenSign = (SideDot > 0.f) ? 1.f : -1.f;

	// ===== 5. 生成目标旋转 =====
	OpenRot = ClosedRot + FRotator(0.f, OpenAngle * OpenSign, 0.f);

	bDirectionResolved = true;
	DoorState = EDoorState::Opening;
	if (DoorTimeline)
	{
		DoorTimeline->Play();
	}
}

void ADoorTrigger::OnTriggerEnd(
	UPrimitiveComponent* OverlappedComp,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex
)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn) return;

	OverlapPawnCount = FMath::Max(0, OverlapPawnCount - 1);

	if (OverlapPawnCount == 0 && DoorState == EDoorState::Opened)
	{
		DoorState = EDoorState::Closing;
		DoorTimeline->Reverse();
	}
}

void ADoorTrigger::OnTimelineUpdate(float Alpha)
{
	if (!DoorActor) return;

	const FRotator NewRot = FMath::Lerp(ClosedRot, OpenRot, Alpha);
	DoorActor->SetActorRotation(NewRot);
}
void ADoorTrigger::OnTimelineFinished()
{
	if (DoorTimeline->GetPlaybackPosition() <= 0.f)
	{
		DoorState = EDoorState::Closed;
	}
	else
	{
		DoorState = EDoorState::Opened;
	}
}

FVector ADoorTrigger::GetDoorNormal()
{
	if (!DoorActor)return FVector();
	UStaticMeshComponent* Mesh = DoorActor->FindComponentByClass<UStaticMeshComponent>();
	if (!Mesh)
	{
		// 兜底
		return DoorActor->GetActorForwardVector();
	}

	const FVector Extent = Mesh->Bounds.BoxExtent;

	// 找最小轴（门厚）
	if (Extent.X < Extent.Y)
	{
		// X 是厚度 → 门面朝 Y
		return DoorActor->GetActorRightVector();
	}
	else
	{
		// Y 是厚度 → 门面朝 X
		return DoorActor->GetActorForwardVector();
	}
}

void ADoorTrigger::InitDoorTrigger()
{
	if (!DoorActor || !TriggerBox)return;
	ClosedRot = DoorActor->GetActorRotation();
	TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ADoorTrigger::OnTriggerBegin);
	TriggerBox->OnComponentEndOverlap.AddDynamic(this, &ADoorTrigger::OnTriggerEnd);
	if (DoorCurve)
	{
		FOnTimelineFloat UpdateFunc;
		UpdateFunc.BindUFunction(this, FName("OnTimelineUpdate"));
		DoorTimeline->AddInterpFloat(DoorCurve, UpdateFunc);
		FOnTimelineEvent FinishedFunc;
		FinishedFunc.BindUFunction(this, FName("OnTimelineFinished"));
		DoorTimeline->SetTimelineFinishedFunc(FinishedFunc);
	}
	FVector Origin;
	FVector Extent;
	DoorActor->GetActorBounds(true, Origin, Extent);

	TriggerBox->SetBoxExtent(Extent + FVector(80.f));
	SetActorLocation(Origin);
}
// Fill out your copyright notice in the Description page of Project Settings.


#include "Space/Space.h"
#include "Space/SpaceStruct.h"
#include "Space/SpaceSubsystem.h"

namespace
{
	void SetOrdinaryActorTreeHidden(AActor* Actor, bool bNewHidden)
	{
		if (!IsValid(Actor) || Actor->IsA<ASpace>() || Actor->IsA<ASpaceStruct>())
		{
			return;
		}

		Actor->SetActorHiddenInGame(bNewHidden);

		TArray<AActor*> AttachedActors;
		Actor->GetAttachedActors(AttachedActors, true, false);
		for (AActor* AttachedActor : AttachedActors)
		{
			SetOrdinaryActorTreeHidden(AttachedActor, bNewHidden);
		}
	}
}


// Sets default values
ASpace::ASpace()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	this->RootComponent = CreateDefaultSubobject<USceneComponent>(FName("Root"));
	// 拆楼动画需要移动楼层 Space，Root 必须是 Movable。
	this->RootComponent->SetMobility(EComponentMobility::Movable);
	this->GatherConstructionData();
}

void ASpace::Init()
{
	// 初始化自身数据
	this->InitializeSpaceData();
	// 尝试注册到子系统
	this->bIsValid = this->RegisterSpace();
}

void ASpace::InitializeSpaceData()
{
	// 先清空自身数据
	this->ResetSpaceData();

	// 获取附加父类，尝试转换为空间类
	ASpace* CastParentSpace = Cast<ASpace>(this->GetAttachParentActor());
	// 要判断编码是否有效
	if (CastParentSpace && !CastParentSpace->Code.IsEmpty())
	{
		this->ParentSpace = CastParentSpace;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ParentSpace code not valid."));
	}

	// 获取附加子类
	this->GetAttachedActors(this->AttachActors, true);

	for (AActor* Actor : this->AttachActors)
	{
		// 开始区分附加Actor
		bool bIsDistinguish = this->DistinguishAttachActor(Actor);
		if (bIsDistinguish == false)
			// 如果都不是或者编码无效，则加入包含数组，不特殊处理
			this->OrdinaryActors.Add(Actor);
	}

	// 开始判断空间类型
	bool HasParent = this->ParentSpace != nullptr;
	bool HasChildren = !this->Children.IsEmpty();
	if (HasParent && HasChildren)
	{
		this->SpaceType = ESpaceType::Branch;
	}
	else if (HasParent && !HasChildren)
	{
		this->SpaceType = ESpaceType::Leaf;
	}
	else if (!HasParent && HasChildren)
	{
		this->SpaceType = ESpaceType::Root;
	}
	else
	{
		this->SpaceType = ESpaceType::Isolated;
	}

}

void ASpace::ResetSpaceData()
{
	// 清空重置各种缓存和状态
	this->AttachActors.Empty();
	
	this->SpaceType = ESpaceType::UnKnown;
	this->OrdinaryActors.Empty();
	
	this->ParentSpace = nullptr;
	
	this->Subspaces.Empty();
	this->SubspaceMap.Empty();
	this->SubspaceMarkSet.Empty();
	this->SubspacePartitionMap.Empty();
	
	this->Structs.Empty();
	this->StructMap.Empty();
	this->StructMarksSet.Empty();
	this->StructPartitionMap.Empty();
}


ASpace* ASpace::GetParentSpace() const
{
	return ParentSpace;
}

bool ASpace::DistinguishAttachActor(AActor* Actor)
{
	// 进行两次转换以进行区分
	ASpace* Subspace = Cast<ASpace>(Actor);
	ASpaceStruct* SpaceStruct = Cast<ASpaceStruct>(Actor);

	// 如果成功转换成了子类空间
	if (Subspace)
	{
		if (!Subspace->Code.IsEmpty())
		{
			// 重复添加警告
			if (this->SubspaceMap.Contains(Subspace->Code))
			{
				UE_LOG(LogTemp, Warning, TEXT("ChildrenSpace \"%s:%s\" is repetitive."),
					*Subspace->Code, *Subspace->DisplayName);
				return false;
			}
			// 添加进入存储以及映射
			this->Subspaces.Add(Subspace);

			this->SubspaceMap.Add(Subspace->Code, Subspace);

			this->SubspaceMarkSet.Append(Subspace->Marks);

			// 添加进入分区方便查找
			// 遍历子空间的Mark
			for (FString Mark : Subspace->Marks)
			{
				// 如果已经存在这个分区，往这个分区里面添加该空间
				if (this->SubspacePartitionMap.Contains(Mark))
				{
					USpacePartition* Partition = this->SubspacePartitionMap.FindRef(Mark);
					// 添加进入存储结构
					Partition->SpacesMap.Add(Subspace->Code, Subspace);
				}
				// 否则创建新分区并添加
				else
				{
					USpacePartition* Partition = NewObject<USpacePartition>(this);
					// 添加进入存储结构
					Partition->SpacesMap.Add(Subspace->Code, Subspace);
					this->SubspacePartitionMap.Add(Mark, Partition);
				}
			}
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("ChildrenSpace code not valid."));
	}

	// 如果是空间结构类，则加入空间结构数组
	if (SpaceStruct)
	{
		if (!SpaceStruct->Code.IsEmpty())
		{
			// 设置他的父空间指针
			SpaceStruct->Parent = this;
			// 重复添加警告
			if (this->StructMap.Contains(SpaceStruct->Code))
			{
				UE_LOG(LogTemp, Warning, TEXT("SpaceStruct \"%s:%s\" is repetitive."),
					*SpaceStruct->Code, *SpaceStruct->DisplayName);
				return false;
			}

			this->Structs.Add(SpaceStruct);
			this->StructMap.Add(SpaceStruct->Code, SpaceStruct);
			// 添加空间结构标记集
			this->StructMarksSet.Append(SpaceStruct->Marks);

			// 添加进入分区方便查找
			// 遍历Mark
			for (FString Mark : SpaceStruct->Marks)
			{
				// 如果已经存在这个分区，往这个分区里面添加该结构
				if (this->StructPartitionMap.Contains(Mark))
				{
					USpaceStructPartition* Partition = this->StructPartitionMap.FindRef(Mark);
					// 添加进入存储结构
					Partition->StructMap.Add(SpaceStruct->Code, SpaceStruct);
				}
				// 否则创建新分区并添加
				else
				{
					USpaceStructPartition* Partition = NewObject<USpaceStructPartition>(this);
					// 添加进入存储结构
					Partition->StructMap.Add(SpaceStruct->Code, SpaceStruct);
					this->StructPartitionMap.Add(Mark, Partition);
				}
			}
			return true;
		}
		UE_LOG(LogTemp, Warning, TEXT("SpaceStruct code not valid."));
	}

	return false;
}


bool ASpace::RegisterSpace()
{
	// 注册到管理器
	UWorld* CurrentWorld = this->GetWorld();
	// 判断世界
	if (CurrentWorld)
	{
		USpaceSubsystem* SpaceSubsystem = CurrentWorld->GetSubsystem<USpaceSubsystem>();
		if (SpaceSubsystem)
		{
			return SpaceSubsystem->RegisterSpace(this);
		}
		UE_LOG(LogTemp, Warning, TEXT("SpaceSubsystem not found."));
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("CurrentWorld not found."));
	return false;
}

void ASpace::GatherConstructionData()
{
	// 默认使用自身命名作为编码
	this->Code = this->GetActorNameOrLabel();
}

void ASpace::Destroyed()
{
	// 同时销毁自身附加的所有Actor
	for (AActor* Actor : this->AttachActors)
	{
		if (Actor)
			Actor->Destroy();
	}
	// 清空自身各种缓存
	this->ResetSpaceData();
	Super::Destroyed();
}

const TArray<ASpace*>& ASpace::GetSubspaces() const
{
	return this->Subspaces;
}

const TMap<FString, ASpace*>& ASpace::GetSubspaceMap() const
{
	return this->SubspaceMap;
}

const TArray<ASpaceStruct*>& ASpace::GetStructs() const
{
	return this->Structs;
}

ASpaceStruct* ASpace::GetStructByCode(const FString& TargetCode) const
{
	return this->StructMap.FindRef(TargetCode);
}

TArray<ASpaceStruct*> ASpace::GetStructByMark(const FString& Mark) const
{
	// 定义返回数据
	TArray<ASpaceStruct*> Result;
	// 寻找对应分区
	USpaceStructPartition* Partition = this->StructPartitionMap.FindRef(Mark);
	if (Partition)
	{
		Partition->StructMap.GenerateValueArray(Result);
		return Result;
	}
	return Result;
}

bool ASpace::HasMark(const FString& Mark) const
{
	return this->Marks.Contains(Mark);
}

TArray<ASpace*> ASpace::GetSubspacesByMark(const FString& Mark) const
{
	// 定义返回数据
	TArray<ASpace*> Result;
	// 寻找对应分区
	USpacePartition* Partition = this->SubspacePartitionMap.FindRef(Mark);
	if (Partition)
	{
		Partition->SpacesMap.GenerateValueArray(Result);
		return Result;
	}
	return Result;
}

void ASpace::SetOrdinaryActorsHidden(bool bNewHidden)
{
	for (AActor* OrdinaryActor : OrdinaryActors)
	{
		SetOrdinaryActorTreeHidden(OrdinaryActor, bNewHidden);
	}
}

bool ASpace::HasStructWithMark(const FString& Mark) const
{
	return this->StructMarksSet.Contains(Mark);
}

const TSet<FString>& ASpace::GetStructMarksSet() const
{
	return this->StructMarksSet;
}


// Called when the game starts or when spawned
void ASpace::BeginPlay()
{
	Super::BeginPlay();
	// 初始化自身
	this->Init();
}

// Called every frame
void ASpace::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}


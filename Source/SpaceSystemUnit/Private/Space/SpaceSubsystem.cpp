// Fill out your copyright notice in the Description page of Project Settings.


#include "Space/SpaceSubsystem.h"
#include "Space/Space.h"


bool USpaceSubsystem::RegisterSpace(ASpace* Space)
{
	// 校验空间对象是否有效
	if (!IsValid(Space))
    {
        return false;
    }

	// 校验编码
	if (!Space->Code.IsEmpty())
	{
		// 如果重复了，则注册失败
		if (this->SpaceMap.Contains(Space->Code))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("SpaceSubsystem: Space Code重复 Code=%s"),
				*Space->Code
			);
			return false;
		}
		
		// 添加进入数组、映射、标记集合
		this->Spaces.Add(Space);
		this->SpaceMap.Add(Space->Code,Space);
		this->SpaceMarkSet.Append(Space->Marks);

		// 添加进入分区方便查找
		// 遍历空间的Mark
		for (FString Mark : Space->Marks)
		{
			// 如果已经存在这个分区，往这个分区里面添加该空间
			if (this->SpacePartitionMap.Contains(Mark))
			{
				USpacePartition* Partition = this->SpacePartitionMap.FindRef(Mark);
				// 添加进入存储结构
				Partition->SpacesMap.Add(Space->Code,Space);
			}
			// 否则创建新分区并添加
			else
			{
				USpacePartition* Partition = NewObject<USpacePartition>(this);
				// 添加进入存储结构
				Partition->SpacesMap.Add(Space->Code,Space);
				this->SpacePartitionMap.Add(Mark,Partition);
			}
		}
		return true;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Space code not valid."));
    return false;
}

bool USpaceSubsystem::UnregisterSpace(FString SpaceCode)
{
	if (SpaceMap.Contains(SpaceCode))
	{
		// 从映射表中移除并且设置他为无效
		ASpace* Space = SpaceMap[SpaceCode];
		SpaceMap.Remove(SpaceCode);
		Space->bIsValid = false;
		return true;
	}
	return false;
}

ASpace* USpaceSubsystem::GetSpace(FString SpaceCode)
{
	if (SpaceMap.Contains(SpaceCode))
	{
		return SpaceMap[SpaceCode];
	}
	return nullptr;
}

bool USpaceSubsystem::DestroySpace(FString SpaceCode, bool bReassignChildrenToParent)
{
	// 尝试销毁空间
	// 空间是否存在
	if (SpaceMap.Contains(SpaceCode))
	{
		// 获取该空间
		ASpace* Space = SpaceMap[SpaceCode];
		if (bReassignChildrenToParent)
		{
			// 重新设置上下级关系
			switch (Space->SpaceType)
			{
			case ESpaceType::UnKnown:
				{
					// 如果是未知类型，则删除失败
					return false;
				}
			case ESpaceType::Branch:
				{
					// 重新附加子节点到父节点上，随后初始化
					ASpace* ParentSpace = Space->GetParentSpace();
					TArray<ASpace*> Subspaces = Space->GetSubspaces();
					for (ASpace* Subspace : Subspaces)
					{
						Subspace->AttachToActor(ParentSpace,FAttachmentTransformRules::KeepWorldTransform);
						// 重新根据附加关系设置层级
						Subspace->InitializeSpaceData();
					}
					// 父类也设置层级
					ParentSpace->InitializeSpaceData();
					break;
				}
			default:
				{
					// 如果是孤立空间或者叶空间，根空间则不用做任何处理，删除后没有子节点或子节点成为孤立空间和根空间
					break;					
				}
			}
		}
		// 移除空间的注册
		SpaceMap.Remove(SpaceCode);
		Space->bIsValid = false;
		// 销毁他
		Space->Destroy();
		return true;
	}
	return false;
}

const TArray<ASpace*>& USpaceSubsystem::GetSpaces() const
{
	return this->Spaces;
}

const TMap<FString, ASpace*>& USpaceSubsystem::GetSpaceMap() const
{
	return SpaceMap;
}

TArray<ASpace*> USpaceSubsystem::GetSpacesByMark(const FString& Mark) const
{
	// 构筑结果数组
	TArray<ASpace*> Result;
	// 寻找对应分区
	if (this->SpacePartitionMap.Contains(Mark))
	{
		USpacePartition* Partition = this->SpacePartitionMap.FindRef(Mark);
		Partition->SpacesMap.GenerateValueArray(Result);
	}
	return Result;
}





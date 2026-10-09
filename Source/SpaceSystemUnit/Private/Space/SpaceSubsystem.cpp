// Fill out your copyright notice in the Description page of Project Settings.

#include "Space/SpaceSubsystem.h"
#include "Space/Space.h"
#include "Space/Interface/SpaceSelectable.h"
#include "SpaceSystemUnit.h"

namespace
{
  // 通知函数
  void NotifySpaceSelection(ASpace *Space, bool bSelected)
  {
    if (!IsValid(Space))
    {
      return;
    }
    if (bSelected)
    {
      // C++ 实现的接口，直接调虚函数
      if (ISpaceSelectable *Selectable = Cast<ISpaceSelectable>(Space))
      {
        Selectable->OnSpaceSelected();
      }
      // 蓝图实现的接口，用 Execute 通知蓝图事件
      else
      {
        ISpaceSelectable::Execute_ReceiveOnSpaceSelected(Space);
      }
    }
    else
    {
      if (ISpaceSelectable *Selectable = Cast<ISpaceSelectable>(Space))
      {
        Selectable->OnSpaceDeselected();
      }
      // 蓝图实现的接口，用 Execute 通知蓝图事件
      else
      {
        ISpaceSelectable::Execute_ReceiveOnSpaceDeselected(Space);
      }
    }
  }
}

#if WITH_EDITOR
#include "DataTableEditorUtils.h"

void FWorldSpaceStructureRow::OnDataTableChanged(const UDataTable *InDataTable, const FName InRowName)
{
  Super::OnDataTableChanged(InDataTable, InRowName);

  // 整表(多行)变化时引擎内部可能正在遍历 RowMap，此时不要重命名行，避免迭代器失效
  if (InRowName == NAME_None || Code.IsEmpty())
  {
    return;
  }

  const FName DesiredName(*Code);
  if (DesiredName == InRowName)
  {
    return;
  }

  if (UDataTable *DataTable = const_cast<UDataTable *>(InDataTable))
  {
    // 若 DesiredName 已存在(重复Code)，RenameRow 会失败并返回 false，不会覆盖已有行
    if (FDataTableEditorUtils::RenameRow(DataTable, InRowName, DesiredName))
    {
      // 重新选中改名后的行，保持网格高亮跟随
      FDataTableEditorUtils::SelectRow(DataTable, DesiredName);
    }
  }
}
#endif

bool USpaceSubsystem::RegisterSpace(ASpace *Space)
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
          LogSpaceSystem,
          Warning,
          TEXT("SpaceSubsystem: Space Code重复 Code=%s"),
          *Space->Code);
      return false;
    }

    // 添加进入数组、映射、标记集合
    this->Spaces.Add(Space);
    this->SpaceMap.Add(Space->Code, Space);
    this->SpaceMarkSet.Append(Space->Marks);

    // 添加进入分区方便查找
    // 遍历空间的Mark
    for (FString Mark : Space->Marks)
    {
      // 如果已经存在这个分区，往这个分区里面添加该空间
      if (this->SpacePartitionMap.Contains(Mark))
      {
        USpacePartition *Partition = this->SpacePartitionMap.FindRef(Mark);
        // 添加进入存储结构
        Partition->SpacesMap.Add(Space->Code, Space);
      }
      // 否则创建新分区并添加
      else
      {
        USpacePartition *Partition = NewObject<USpacePartition>(this);
        // 添加进入存储结构
        Partition->SpacesMap.Add(Space->Code, Space);
        this->SpacePartitionMap.Add(Mark, Partition);
      }
    }
    return true;
  }

  UE_LOG(LogSpaceSystem, Warning, TEXT("Space code not valid."));
  return false;
}

bool USpaceSubsystem::UnregisterSpace(FString SpaceCode)
{
  if (SpaceMap.Contains(SpaceCode))
  {
    // 从映射表中移除并且设置他为无效
    ASpace *Space = SpaceMap[SpaceCode];
    SpaceMap.Remove(SpaceCode);
    Space->bIsValid = false;
    return true;
  }
  return false;
}

ASpace *USpaceSubsystem::GetSpace(FString SpaceCode)
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
    ASpace *Space = SpaceMap[SpaceCode];
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
        ASpace *ParentSpace = Space->GetParentSpace();
        TArray<ASpace *> Subspaces = Space->GetSubspaces();
        for (ASpace *Subspace : Subspaces)
        {
          Subspace->AttachToActor(ParentSpace, FAttachmentTransformRules::KeepWorldTransform);
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

const TArray<ASpace *> &USpaceSubsystem::GetSpaces() const
{
  return this->Spaces;
}

const TMap<FString, ASpace *> &USpaceSubsystem::GetSpaceMap() const
{
  return SpaceMap;
}

TArray<ASpace *> USpaceSubsystem::GetSpacesByMark(const FString &Mark) const
{
  // 构筑结果数组
  TArray<ASpace *> Result;
  // 寻找对应分区
  if (this->SpacePartitionMap.Contains(Mark))
  {
    USpacePartition *Partition = this->SpacePartitionMap.FindRef(Mark);
    Partition->SpacesMap.GenerateValueArray(Result);
  }
  return Result;
}

void USpaceSubsystem::ClearSelection()
{
  // 先清空再通知
  TArray<ASpace *> AllSpaces = SelectedSpaces;
  SelectedSpaces.Empty();

  // 向所有已选择空间发送取消选择通知
  for (ASpace *Space : AllSpaces)
  {
    if (!IsValid(Space))
      continue;

    NotifySpaceSelection(Space, false);
  }
}

void USpaceSubsystem::SetSelectMode(ESpaceSelectMode NewMode)
{
  // 先清空当前选择，再存储新的选择模式
  ClearSelection();
  SelectMode = NewMode;

  UE_LOG(LogSpaceSystem, Log, TEXT("SpaceSubsystem: SetSelectMode=%d"), (int32)SelectMode);
}

void USpaceSubsystem::ToggleSingleSelect(ASpace *NewSpace)
{
  if (!IsValid(NewSpace))
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("SpaceSubsystem: ToggleSingleSelect failed, Space invalid."));
    return;
  }

  // 不可选择模式下拒绝选择
  if (SelectMode == ESpaceSelectMode::None)
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("SpaceSubsystem: ToggleSingleSelect failed, select mode is None. Code=%s"), *NewSpace->Code);
    return;
  }

  // 清空旧选择，再加入新空间
  ClearSelection();
  SelectedSpaces.Add(NewSpace);

  NotifySpaceSelection(NewSpace, true);

  UE_LOG(LogSpaceSystem, Log, TEXT("SpaceSubsystem: ToggleSingleSelect Code=%s"), *NewSpace->Code);
}

void USpaceSubsystem::ToggleMultipleSelect(const TArray<ASpace *> &NewSpaces)
{
  // 只有多选模式允许互斥多选
  if (SelectMode == ESpaceSelectMode::None || SelectMode == ESpaceSelectMode::Single)
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("SpaceSubsystem: ToggleMultipleSelect failed, select mode is %d."), (int32)SelectMode);
    return;
  }

  // 清空旧选择，再加入全部目标空间
  ClearSelection();
  // 先进行全部添加再进行全部通知，避免通知时添加未完成
  for (ASpace *Space : NewSpaces)
  {
    if (IsValid(Space))
    {
      SelectedSpaces.Add(Space);
    }
  }
  for (ASpace *Space : SelectedSpaces)
  {
    NotifySpaceSelection(Space, true);
  }

  UE_LOG(LogSpaceSystem, Log, TEXT("SpaceSubsystem: ToggleMultipleSelect count=%d"), SelectedSpaces.Num());
}

void USpaceSubsystem::AddMultipleSelect(const TArray<ASpace *> &NewSpaces)
{
  // 只有多选模式允许追加选择
  if (SelectMode == ESpaceSelectMode::None || SelectMode == ESpaceSelectMode::Single)
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("SpaceSubsystem: AddToMultiSelect failed, select mode is %d."), (int32)SelectMode);
    return;
  }

  // 过滤无效空间与重复空间，仅向本次新增的空间发送选择通知
  TArray<ASpace *> AddedSpaces;
  for (ASpace *Space : NewSpaces)
  {
    // 无效或已选择的空间跳过，避免重复选择
    if (!IsValid(Space) || SelectedSpaces.Contains(Space))
    {
      continue;
    }

    SelectedSpaces.Add(Space);
    AddedSpaces.Add(Space);
  }
  for (ASpace *Space : AddedSpaces)
  {
    NotifySpaceSelection(Space, true);
  }

  UE_LOG(LogSpaceSystem, Log, TEXT("SpaceSubsystem: AddToMultiSelect count=%d"), AddedSpaces.Num());
}

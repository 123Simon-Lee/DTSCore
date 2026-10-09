// Fill out your copyright notice in the Description page of Project Settings.

#include "Scripts/Space/SpaceScript.h"

#include "Components/SceneComponent.h"
#include "EditorUtilityLibrary.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Space/Space.h"
#include "Space/SpaceSubsystem.h"
#include "SpaceSystemUnit.h"

void USpaceScript::InitializeSpaceActorsFromDataTable(UDataTable *DataTable)
{
  // 获取世界对象
  TArray<AActor *> Selection = UEditorUtilityLibrary::GetSelectionSet();
  if (Selection.Num() == 0)
    return;
  UWorld *World = Selection[0]->GetWorld();
  if (!World || !DataTable || !DataTable->RowStruct->IsChildOf(FWorldSpaceStructureRow::StaticStruct()))
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("InitializeSpaceActorsFromDataTable: World or DataTable is invalid."));
    return;
  }

  // 遍历世界中所有 SpaceActor
  for (TActorIterator<ASpace> It(World); It; ++It)
  {
    ASpace *Space = *It;
    if (!IsValid(Space))
    {
      continue;
    }

    // 首先根据其 Label 初始化 Code
    // 修改前标记为脏数据，否则地图保存时不会记录该变更
    Space->Modify();
    Space->Code = Space->GetActorLabel();
    if (Space->Code.IsEmpty())
    {
      continue;
    }

    // 根据 Code 在数据表中查找对应行
    const FWorldSpaceStructureRow *Row =
        DataTable->FindRow<FWorldSpaceStructureRow>(
            FName(*Space->Code),
            TEXT("InitializeSpaceActorsFromDataTable"));
    if (!Row)
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("InitializeSpaceActorsFromDataTable: No row found for Code \"%s\"."), *Space->Code);
      continue;
    }

    // 根据数据表设置其 DisplayName
    Space->DisplayName = Row->DisplayName;
  }

  UE_LOG(LogSpaceSystem, Log, TEXT("InitializeSpaceActorsFromDataTable completed."));
}

void USpaceScript::AttachSpacesFromDataTable(UDataTable *DataTable)
{
  // 获取世界对象
  TArray<AActor *> Selection = UEditorUtilityLibrary::GetSelectionSet();
  if (Selection.Num() == 0)
    return;
  UWorld *World = Selection[0]->GetWorld();
  if (!World || !DataTable || !DataTable->RowStruct->IsChildOf(FWorldSpaceStructureRow::StaticStruct()))
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpacesFromDataTable: World or DataTable is invalid."));
    return;
  }

  // 遍历世界中所有 SpaceActor，按 Code 建立索引
  TMap<FString, ASpace *> SpaceMap;
  TArray<ASpace *> AllSpaces;
  for (TActorIterator<ASpace> It(World); It; ++It)
  {
    ASpace *Space = *It;
    if (!IsValid(Space) || Space->Code.IsEmpty())
    {
      continue;
    }

    if (SpaceMap.Contains(Space->Code))
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpacesFromDataTable: duplicate Code \"%s\"."), *Space->Code);
      continue;
    }

    SpaceMap.Add(Space->Code, Space);
    AllSpaces.Add(Space);
  }

  // 根据数据表中的 ParentCode 建立附加关系
  for (const auto &Pair : SpaceMap)
  {
    ASpace *Space = Pair.Value;

    const FWorldSpaceStructureRow *Row =
        DataTable->FindRow<FWorldSpaceStructureRow>(
            FName(*Space->Code),
            TEXT("AttachSpacesFromDataTable"));
    if (!Row)
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpacesFromDataTable: No row found for Code \"%s\"."), *Space->Code);
      continue;
    }

    // ParentCode 为空视为根空间，解除附加
    if (Row->ParentCode.IsEmpty())
    {
      if (Space->GetAttachParentActor())
      {
        Space->Modify();
        Space->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
      }
      continue;
    }

    ASpace *ParentSpace = SpaceMap.FindRef(Row->ParentCode);
    if (!ParentSpace)
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpacesFromDataTable: Parent \"%s\" not found for Code \"%s\"."), *Row->ParentCode, *Space->Code);
      continue;
    }
    if (ParentSpace == Space)
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpacesFromDataTable: Code \"%s\" cannot attach to itself."), *Space->Code);
      continue;
    }

    // 附加并标记为脏数据
    Space->Modify();
    Space->AttachToActor(ParentSpace, FAttachmentTransformRules::KeepWorldTransform);
  }

  // 重新梳理所有空间的附加关系与空间类型
  for (ASpace *Space : AllSpaces)
  {
    Space->InitializeSpaceData();
  }

  UE_LOG(LogSpaceSystem, Log, TEXT("AttachSpacesFromDataTable completed."));
}

void USpaceScript::AttachSelectedFromDataTable(UDataTable *DataTable, FString Separator, int32 ValueIndex)
{
  // 检查数据表类型、分隔符和索引，参数无效时停止执行。
  if (!IsValid(DataTable) || !DataTable->GetRowStruct() ||
      !DataTable->GetRowStruct()->IsChildOf(FWorldSpaceStructureRow::StaticStruct()) ||
      Separator.IsEmpty() || ValueIndex < 0)
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Invalid table, empty separator or negative index."));
    return;
  }

  // 获取当前选中的 Actor，没有选中对象时直接返回。
  const TArray<AActor *> Selection = UEditorUtilityLibrary::GetSelectionSet();
  if (Selection.IsEmpty())
  {
    return;
  }

  // 从有效的选中 Actor 获取世界，用于查找可附加的 Space。
  UWorld *World = nullptr;
  for (AActor *Actor : Selection)
  {
    if (IsValid(Actor) && IsValid(Actor->GetWorld()))
    {
      World = Actor->GetWorld();
      break;
    }
  }
  if (!World)
  {
    UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: No valid world in selection."));
    return;
  }

  // 遍历世界中的 Space，按 Code 保存，目标 Space 无需选中。
  TMap<FString, ASpace *> SpaceMap;
  for (TActorIterator<ASpace> It(World); It; ++It)
  {
    ASpace *Space = *It;
    // 跳过无效对象和没有 Code 的 Space。
    if (!IsValid(Space) || Space->Code.IsEmpty())
    {
      continue;
    }
    // Code 重复时保留先找到的 Space，并输出提示。
    if (SpaceMap.Contains(Space->Code))
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Duplicate space Code \"%s\"."), *Space->Code);
      continue;
    }
    SpaceMap.Add(Space->Code, Space);
  }

  // 将本次修改记录为一次编辑器撤销操作。
  const FScopedTransaction Transaction(NSLOCTEXT("SpaceScript", "AttachSelectedFromDataTable", "Attach Selected Actors From Data Table"));
  TArray<ASpace *> ChangedSpaces;
  // 只修改 Selection 中 Actor 的附加关系。
  for (AActor *Actor : Selection)
  {
    // 跳过无效对象、其他世界的对象，以及没有根组件的对象。
    if (!IsValid(Actor) || Actor->GetWorld() != World || !IsValid(Actor->GetRootComponent()))
    {
      continue;
    }

    // 按分隔符拆分 Label，保留空段，避免后面的段落索引前移。
    const FString Label = Actor->GetActorLabel();
    TArray<FString> Parts;
    Label.ParseIntoArray(Parts, *Separator, false);
    // 指定索引越界或对应内容为空时，跳过这个 Actor。
    if (!Parts.IsValidIndex(ValueIndex) || Parts[ValueIndex].IsEmpty())
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Label \"%s\" has no non-empty value at index %d."), *Label, ValueIndex);
      continue;
    }

    // 取指定段作为表行名查表，例如取出 ColorBlock 查找同名行；找不到时跳过。
    const FWorldSpaceStructureRow *Row = DataTable->FindRow<FWorldSpaceStructureRow>(
        FName(*Parts[ValueIndex]), TEXT("AttachSelectedFromDataTable"), false);
    if (!Row)
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: No row \"%s\" for Actor \"%s\"."), *Parts[ValueIndex], *Label);
      continue;
    }

    // 用表行自身的 Code 匹配目标 Space，找不到时保留原附加关系。
    ASpace *TargetSpace = SpaceMap.FindRef(Row->Code);
    if (!IsValid(TargetSpace) || TargetSpace->GetWorld() != Actor->GetWorld())
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Space \"%s\" not found in world for Actor \"%s\"."), *Row->Code, *Label);
      continue;
    }
    // 禁止附加到自身或自己的下级 Actor，避免形成循环附加。
    if (TargetSpace == Actor || TargetSpace->IsAttachedTo(Actor))
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Attachment would create a cycle for Actor \"%s\"."), *Label);
      continue;
    }
    // 已经附加到目标 Space 时，跳过重复操作。
    if (Actor->GetAttachParentActor() == TargetSpace)
    {
      continue;
    }

    // 记录修改，将选中 Actor 直接附加到目标 Space，保持世界位置、旋转和缩放。
    Actor->Modify();
    Actor->GetRootComponent()->Modify();
    if (!Actor->AttachToActor(TargetSpace, FAttachmentTransformRules::KeepWorldTransform))
    {
      UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSelectedFromDataTable: Failed to attach Actor \"%s\" to \"%s\"."), *Label, *Row->Code);
      continue;
    }

    // 选中 Actor 本身是 Space 时，记录下来，稍后刷新它的空间数据。
    if (ASpace *Space = Cast<ASpace>(Actor))
    {
      ChangedSpaces.Add(Space);
    }
  }

  // 根据修改后的附加关系，刷新这些选中 Space 的父子空间记录和空间类型。
  for (ASpace *Space : ChangedSpaces)
  {
    Space->InitializeSpaceData();
  }
}

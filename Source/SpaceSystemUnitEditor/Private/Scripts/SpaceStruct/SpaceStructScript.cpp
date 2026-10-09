// Fill out your copyright notice in the Description page of Project Settings.


#include "Scripts/SpaceStruct/SpaceStructScript.h"

#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EditorUtilityLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Space/Space.h"
#include "Space/SpaceStruct.h"
#include "Space/SpaceSubsystem.h"
#include "SpaceSystemUnit.h"

void USpaceStructScript::AttachSpaceStructsFromDataTable(UDataTable* DataTable, bool bTraverseWholeWorld)
{
	// 直接取编辑器世界，全世界模式不依赖选中物体。
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!IsValid(World) || !IsValid(DataTable) || !DataTable->GetRowStruct() ||
		!DataTable->GetRowStruct()->IsChildOf(FWorldSpaceStructureRow::StaticStruct()))
	{
		UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpaceStructsFromDataTable: World or DataTable is invalid."));
		return;
	}

	// 按开关收集待处理对象，选中模式忽略其他类型的 Actor。
	TArray<ASpaceStruct*> Targets;
	if (bTraverseWholeWorld)
	{
		for (TActorIterator<ASpaceStruct> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				Targets.Add(*It);
			}
		}
	}
	else
	{
		for (AActor* Actor : UEditorUtilityLibrary::GetSelectionSet())
		{
			ASpaceStruct* SpaceStruct = Cast<ASpaceStruct>(Actor);
			if (IsValid(SpaceStruct) && SpaceStruct->GetWorld() == World)
			{
				Targets.Add(SpaceStruct);
			}
		}
	}

	if (Targets.IsEmpty())
	{
		UE_LOG(LogSpaceSystem, Log, TEXT("AttachSpaceStructsFromDataTable: No space structs to process."));
		return;
	}

	// 父空间始终从整个世界查找，无需同时选中。
	TMap<FString, ASpace*> SpaceMap;
	for (TActorIterator<ASpace> It(World); It; ++It)
	{
		ASpace* Space = *It;
		if (IsValid(Space) && !Space->Code.IsEmpty())
		{
			SpaceMap.FindOrAdd(Space->Code) = Space;
		}
	}

	// 将批量附加记录为一次可撤销的编辑操作。
	const FScopedTransaction Transaction(NSLOCTEXT("SpaceStructScript", "AttachSpaceStructs", "Attach Space Structs"));
	int32 AttachedCount = 0;
	for (ASpaceStruct* SpaceStruct : Targets)
	{
		FString AttachSpaceCode;
		if (!SpaceStruct->GetAttachParentCode(World, DataTable, AttachSpaceCode) || AttachSpaceCode.IsEmpty())
		{
			UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpaceStructsFromDataTable: Struct \"%s\" has no valid attach space."),
				*SpaceStruct->GetActorLabel());
			continue;
		}

		ASpace* TargetSpace = SpaceMap.FindRef(AttachSpaceCode);
		if (!IsValid(TargetSpace))
		{
			UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpaceStructsFromDataTable: Space \"%s\" not found for struct \"%s\"."),
				*AttachSpaceCode, *SpaceStruct->GetActorLabel());
			continue;
		}

		// 保持世界位置、旋转和缩放，只修改附加关系。
		SpaceStruct->Modify();
		if (USceneComponent* Root = SpaceStruct->GetRootComponent())
		{
			Root->Modify();
		}
		if (SpaceStruct->AttachToActor(TargetSpace, FAttachmentTransformRules::KeepWorldTransform))
		{
			++AttachedCount;
		}
		else
		{
			UE_LOG(LogSpaceSystem, Warning, TEXT("AttachSpaceStructsFromDataTable: Failed to attach struct \"%s\" to space \"%s\"."),
				*SpaceStruct->GetActorLabel(), *AttachSpaceCode);
		}
	}

	UE_LOG(LogSpaceSystem, Log, TEXT("AttachSpaceStructsFromDataTable completed, attached %d of %d structs."), AttachedCount, Targets.Num());
}

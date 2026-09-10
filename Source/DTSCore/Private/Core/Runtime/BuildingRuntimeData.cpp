#include "Core/Runtime/BuildingRuntimeData.h"

// =========================================================
// UBuildingRuntimeNode
// =========================================================

UBuildingRuntimeNode* UBuildingRuntimeNode::FindNode(AActor* InActor)
{
    if (!InActor)
    {
        return nullptr;
    }

    if (RuntimeActor == InActor)
    {
        return this;
    }

    for (TObjectPtr<UBuildingRuntimeNode>& Child : Children)
    {
        if (!Child)
        {
            continue;
        }

        if (UBuildingRuntimeNode* Result = Child->FindNode(InActor))
        {
            return Result;
        }
    }

    return nullptr;
}

void UBuildingRuntimeNode::CollectFloorNodes(
    TArray<UBuildingRuntimeNode*>& OutNodes)
{
    if (FloorIndex != BUILDING_INVALID_FLOOR_INDEX)
    {
        OutNodes.Add(this);
    }

    for (TObjectPtr<UBuildingRuntimeNode>& Child : Children)
    {
        if (Child)
        {
            Child->CollectFloorNodes(OutNodes);
        }
    }
}

void UBuildingRuntimeNode::CollectNodesByMark(
    const FString& Mark,
    TArray<UBuildingRuntimeNode*>& OutNodes)
{
    if (Tags.Contains(Mark))
    {
        OutNodes.Add(this);
    }

    for (TObjectPtr<UBuildingRuntimeNode>& Child : Children)
    {
        if (Child)
        {
            Child->CollectNodesByMark(Mark, OutNodes);
        }
    }
}

void UBuildingRuntimeNode::ForEachNode(TFunctionRef<void(UBuildingRuntimeNode*)> Func)
{
    Func(this);

    for (TObjectPtr<UBuildingRuntimeNode>& Child : Children)
    {
        if (Child)
        {
            Child->ForEachNode(Func);
        }
    }
}
//重置运行时数据和状态
void UBuildingRuntimeNode::ResetRuntime()
{
    CurrentOffset = FVector::ZeroVector;
    TargetOffset = FVector::ZeroVector;
    CurrentTransform = OriginalTransform;

    bExpanded = false;
    bHidden = false;

    if (RuntimeActor)
    {
        RuntimeActor->SetActorTransform(OriginalTransform);
        RuntimeActor->SetActorHiddenInGame(false);
    }

    for (TObjectPtr<UBuildingRuntimeNode>& Child : Children)
    {
        if (Child)
        {
            Child->ResetRuntime();
        }
    }
}

// =========================================================
// FBuildingRuntimeData
// =========================================================

UBuildingRuntimeNode* FBuildingRuntimeData::FindNode(AActor* Actor)
{
    return Root ? Root->FindNode(Actor) : nullptr;
}

void FBuildingRuntimeData::CollectFloorNodes(TArray<UBuildingRuntimeNode*>& OutNodes)
{
    OutNodes.Reset();

    if (Root)
    {
        Root->CollectFloorNodes(OutNodes);
    }
}

void FBuildingRuntimeData::CollectNodesByMark(const FString& Mark,TArray<UBuildingRuntimeNode*>& OutNodes)
{
    OutNodes.Reset();

    if (Root)
    {
        Root->CollectNodesByMark(Mark, OutNodes);
    }
}

void FBuildingRuntimeData::ResetRuntime()
{
    if (Root)
    {
        Root->ResetRuntime();
    }
}

// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Manager/SceneGroupVisibilityManager.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ASceneGroupVisibilityManager::ASceneGroupVisibilityManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

}

void ASceneGroupVisibilityManager::InitializeSceneGroupCache()
{
	SceneGroupCache.Empty();
	VisibleGroupKeys.Empty();
	bSceneGroupCacheInitialized = true;

	for (const FSceneVisibilityGroupConfig& Config : SceneGroups)
	{
		if (Config.GroupKey.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("SceneVisibilityManager: skipped a config with empty GroupKey"));
			continue;
		}

		// 相同 GroupKey 的多条配置会合并，不再丢弃后续配置。
		FSceneVisibilityRuntimeGroup& RuntimeGroup = SceneGroupCache.FindOrAdd(Config.GroupKey);

		CollectActorTree(Config.RootActor, RuntimeGroup.Actors);

		for (AActor* AdditionalRootActor : Config.AdditionalRootActors)
		{
			CollectActorTree(AdditionalRootActor, RuntimeGroup.Actors);
		}

		if (!Config.ActorTag.IsNone())
		{
			TArray<AActor*> TaggedActors;
			UGameplayStatics::GetAllActorsWithTag(this, Config.ActorTag, TaggedActors);

			for (AActor* TaggedActor : TaggedActors)
			{
				CollectActorTree(TaggedActor, RuntimeGroup.Actors);
			}
		}
	}

	for (TPair<FString, FSceneVisibilityRuntimeGroup>& Pair : SceneGroupCache)
	{
		const FString& GroupKey = Pair.Key;
		FSceneVisibilityRuntimeGroup& RuntimeGroup = Pair.Value;

		if (RuntimeGroup.Actors.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("SceneVisibilityManager: GroupKey=%s cached 0 actors; check RootActor, AdditionalRootActors or ActorTag"), *GroupKey);
			continue;
		}

		const bool bAnyVisible = RuntimeGroup.Actors.ContainsByPredicate(
			[](const TObjectPtr<AActor>& Actor)
			{
				return IsValid(Actor) && !Actor->IsHidden();
			});

		if (bAnyVisible)
		{
			VisibleGroupKeys.Add(GroupKey);
		}

		UE_LOG(LogTemp, Log, TEXT("SceneGroup Cached: Key=%s ActorCount=%d"), *GroupKey, RuntimeGroup.Actors.Num());
	}
}

void ASceneGroupVisibilityManager::ApplySelectedGroups(const TArray<FString>& SelectedKeys)
{
	EnsureSceneGroupCacheInitialized();

	TSet<FString> SelectedSet;


	for (const FString& Key : SelectedKeys)
	{
		SelectedSet.Add(Key);
	}


	/*
	 * 遍历缓存中的所有场景类别
	 */
	for (TPair<FString, FSceneVisibilityRuntimeGroup>& Pair : SceneGroupCache)
	{
		const FString& GroupKey = Pair.Key;


		const bool bShouldShow = SelectedSet.Contains(GroupKey);


		SetGroupVisibleInternal(GroupKey, bShouldShow);
	}

}

void ASceneGroupVisibilityManager::SetGroupVisible(const FString& GroupKey, bool bVisible)
{
	EnsureSceneGroupCacheInitialized();
	SetGroupVisibleInternal(GroupKey, bVisible);
}

void ASceneGroupVisibilityManager::ShowAllGroups()
{
	EnsureSceneGroupCacheInitialized();
	for (TPair<FString, FSceneVisibilityRuntimeGroup>& Pair : SceneGroupCache)
	{
		SetGroupVisibleInternal(Pair.Key, true);
	}
}

void ASceneGroupVisibilityManager::HideAllGroups()
{
	EnsureSceneGroupCacheInitialized();
	for (TPair<FString, FSceneVisibilityRuntimeGroup>& Pair : SceneGroupCache)
	{
		SetGroupVisibleInternal(Pair.Key, false);
	}
}

// Called when the game starts or when spawned
void ASceneGroupVisibilityManager::BeginPlay()
{
	Super::BeginPlay();
	InitializeSceneGroupCache();
}

// Called every frame
void ASceneGroupVisibilityManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void ASceneGroupVisibilityManager::CollectActorTree(AActor* RootActor, TArray<TObjectPtr<AActor>>& OutActors)
{
	if (!IsValid(RootActor)) return;
	/*
	 * 缓存自己
	 */
	OutActors.AddUnique(RootActor);
	/*
	 * 获取直接子 Actor
	 */
	TArray<AActor*> ChildActors;

	RootActor->GetAttachedActors(ChildActors);

	/*
	 * 递归
	 */
	for (AActor* ChildActor : ChildActors)
	{
		if (!IsValid(ChildActor))continue;

		CollectActorTree(ChildActor, OutActors);
	}
}

void ASceneGroupVisibilityManager::EnsureSceneGroupCacheInitialized()
{
	if (!bSceneGroupCacheInitialized)
	{
		InitializeSceneGroupCache();
	}
}

void ASceneGroupVisibilityManager::SetGroupVisibleInternal(const FString& GroupKey, bool bVisible)
{
	FSceneVisibilityRuntimeGroup* Group = SceneGroupCache.Find(GroupKey);


	if (!Group)
	{
		UE_LOG(LogTemp,Warning,TEXT("SceneVisibilityManager: Unknown GroupKey = %s"),*GroupKey);
		return;
	}


	int32 ValidActorCount = 0;
	int32 SceneComponentCount = 0;

	for (AActor* Actor : Group->Actors)
	{
		if (!IsValid(Actor))continue;

		++ValidActorCount;
		Actor->SetActorHiddenInGame(!bVisible);

		/*
		 * 部分蓝图（例如 BP_SplineMesh_2）会在 Construction Script 中动态
		 * Add SplineMeshComponent。除了设置 Actor 隐藏状态，再显式同步其全部
		 * SceneComponent，避免动态组件保留旧的渲染状态。
		 */
		TInlineComponentArray<USceneComponent*> SceneComponents;
		Actor->GetComponents(SceneComponents);

		for (USceneComponent* SceneComponent : SceneComponents)
		{
			if (!IsValid(SceneComponent))continue;

			++SceneComponentCount;
			SceneComponent->SetHiddenInGame(!bVisible, true);
		}
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("SceneGroup Visibility: Key=%s Visible=%s ActorCount=%d SceneComponentCount=%d"),
		*GroupKey,
		bVisible ? TEXT("true") : TEXT("false"),
		ValidActorCount,
		SceneComponentCount);


	if (bVisible)
	{
		VisibleGroupKeys.Add(GroupKey);
	}
	else
	{
		VisibleGroupKeys.Remove(GroupKey);
	}
}


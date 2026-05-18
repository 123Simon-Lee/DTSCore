// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingFloorComponent.h"
#include "EngineUtils.h"
#include "BuildingFloorConfig.h"
#include "BuildingManager.generated.h"

// 楼层点击事件
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMouseClicked, AActor*, BuildingActor, int32, Floor);

USTRUCT(BlueprintType)
struct FSceneScope
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    AActor* NowBuilding = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    AActor* NowFloor = nullptr;
};

UCLASS()
class DTSCORE_API ABuildingManager : public AActor
{
	GENERATED_BODY()
    ABuildingManager();
public:
    // 在 UPROPERTY 里加：
/** 楼栋差异化配置表，Row Name = Actor 显示名称（如"B1"），
 *  只需要填需要特殊配置的楼栋，其余走默认值 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building")
    UDataTable* BuildingFloorConfigTable;

    UPROPERTY(BlueprintAssignable, Category = "Event")
    FOnMouseClicked OnMouseClicked;

    UPROPERTY(EditAnywhere,BlueprintReadWrite)
    bool bEnableClick = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Disassemble")
    EDisassembleMode DisassembleMode = EDisassembleMode::Drawer;

    UPROPERTY(EditAnywhere)
    float TraceLength = 100000.f;

    // 楼层需要使用的拆楼组件类映射
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Building")
    TMap<AActor*,TSubclassOf<UBuildingFloorComponent>> BuildingComponentsClassMap; 

    UPROPERTY(BlueprintReadWrite, Category = "Building")
    TMap<AActor*, UBuildingFloorComponent*> BuildingMap;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Building")
    TMap<AActor*, EPullDirection> BuildingMapwithPullDir;
    
    UPROPERTY(BlueprintReadWrite, Category = "Building")
    UBuildingFloorComponent* ActiveBuildingComponent = nullptr;
    
    UPROPERTY(BlueprintReadWrite, Category = "Building")
    AActor* ActiveBuildingActor = nullptr;

    //Hover
    UPROPERTY(BlueprintReadOnly)
    int32 HoverFloor = 0;
    UPROPERTY(BlueprintReadOnly)
    AActor* HoverActor = nullptr;
    //性能优化
    UPROPERTY(BlueprintReadWrite, Category = "MouseSettings")
    float HoverCheckInterval = 0.05f; // 20次/秒
    UPROPERTY(BlueprintReadWrite, Category = "MouseSettings")
    float HoverTimer = 0.f;
	//记录上一次鼠标位置，只有当鼠标位置发生变化时才进行Hover检测
    UPROPERTY(BlueprintReadOnly)
    FVector2D LastMousePos;
    // 缓存命中结果（避免重复Trace）
    UPROPERTY(BlueprintReadOnly)
    FHitResult CachedHit;
    // 是否有有效命中
    UPROPERTY(BlueprintReadOnly)
    bool bHasValidHit = false;


public:
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    UBuildingFloorComponent* FindBuildingFloorComponent(AActor* BuildingActor);

    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    UBuildingFloorComponent* GetFloorCompByName(const FString& ActorName) const;
    //辅助函数 方便TS取用
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    TArray<AActor*> GetBuildingMapKeys() const;

    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    FSceneScope GetNowSceneScope();
protected:

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    bool DoTrace(FHitResult& OutHit);
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    void HandleClick(const FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    void HandleHover(const FHitResult& Hit);
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    void ClearHover();
    //高光显示
    UFUNCTION(BlueprintCallable, Category = "BuildManagerEvent")
    void SetActorHighlight(AActor* Actor, bool bEnable);
    
};

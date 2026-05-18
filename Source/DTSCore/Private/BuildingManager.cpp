// Fill out your copyright notice in the Description page of Project Settings.


#include "BuildingManager.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values
ABuildingManager::ABuildingManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
    
}

UBuildingFloorComponent* ABuildingManager::FindBuildingFloorComponent(AActor* BuildingActor)
{
    UBuildingFloorComponent* Comp = BuildingMap.FindRef(BuildingActor);
   return Comp ;
}

UBuildingFloorComponent* ABuildingManager::GetFloorCompByName(const FString& ActorName) const
{
    for (const auto& Pair : BuildingMap)
    {
        if (Pair.Key && UKismetSystemLibrary::GetDisplayName(Pair.Key) == ActorName)
            return Pair.Value;
    }
    return nullptr;
}

TArray<AActor*> ABuildingManager::GetBuildingMapKeys() const
{
    TArray<AActor*> Keys;
    BuildingMap.GetKeys(Keys);
    return Keys;
}

FSceneScope ABuildingManager::GetNowSceneScope()
{
    FSceneScope Result;
    Result.NowBuilding = this->ActiveBuildingActor;

    if (this->ActiveBuildingComponent)
    {
        if (this->ActiveBuildingComponent->ActiveFloor != 0)
        {
            // 有激活楼层 → 用楼层 Actor
            Result.NowFloor = this->ActiveBuildingComponent->FloorMap[
                this->ActiveBuildingComponent->ActiveFloor];
        }
        else
        {
            // ActiveFloor == 0 → 用 DevicePoint Actor（DataSmithActor 的直接子节点中名含 DevicePoint 的）
            if (this->ActiveBuildingComponent->DataSmithActor)
            {
                TArray<AActor*> TempChildrens;
                this->ActiveBuildingComponent->DataSmithActor->GetAttachedActors(TempChildrens, false);
                for (AActor* Child : TempChildrens)
                {
                    if (!Child) continue;
                    FString Name = UKismetSystemLibrary::GetDisplayName(Child);
                    if (Name.Contains(TEXT("DevicePoint")))
                    {
                        Result.NowFloor = Child;
                        break;
                    }
                }
            }
        }
    }

    return Result;
}

// Called when the game starts or when spawned
void ABuildingManager::BeginPlay()
{
    Super::BeginPlay();
    BuildingMap.Empty();

    for (auto BuildingActorPair : BuildingComponentsClassMap)
    {
        if (!BuildingActorPair.Key) continue;

        AActor* BuildingActor = BuildingActorPair.Key;
        TSubclassOf<UBuildingFloorComponent> ComponentClass = BuildingActorPair.Value;

        // 创建组件
        UBuildingFloorComponent* Comp =
            NewObject<UBuildingFloorComponent>(BuildingActor, ComponentClass);
        if (!Comp) continue;

        Comp->RegisterComponent();
        Comp->DisassembleMode = DisassembleMode;
        Comp->DataSmithActor = BuildingActor;

        // =========================================================
        // 查 DataTable，有配置行则用配置覆盖，没有则保持默认值
        // =========================================================
        if (BuildingFloorConfigTable)
        {
            FString ActorName = UKismetSystemLibrary::GetDisplayName(BuildingActor);
            FBuildingFloorConfig* Config = BuildingFloorConfigTable->FindRow<FBuildingFloorConfig>(
                FName(*ActorName), TEXT(""),false);

            if (Config)
            {
                Comp->InitFromConfig(*Config);
            }
        }

        // 解析楼层
        Comp->ParseFloors();

        // 建立映射
        BuildingMap.Add(BuildingActor, Comp);

        if (BuildingMapwithPullDir.IsEmpty()) continue;

        // 设置方向（BuildingMapwithPullDir 优先级高于 DataTable，后设置会覆盖）
        if (BuildingMapwithPullDir.Contains(BuildingActor))
        {
            Comp->PullDirection = BuildingMapwithPullDir[BuildingActor];
        }
    }
}

// Called every frame
void ABuildingManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;

    // ===== 鼠标移动检测 =====
    float X, Y;
    PC->GetMousePosition(X, Y);

    FVector2D CurrentMousePos(X, Y);
    bool bMouseMoved = !CurrentMousePos.Equals(LastMousePos, 0.1f);

    // ===== 是否需要Trace =====
    bool bNeedTrace = bMouseMoved || PC->WasInputKeyJustPressed(EKeys::LeftMouseButton);

    if (bNeedTrace)
    {
        LastMousePos = CurrentMousePos;
        bHasValidHit = DoTrace(CachedHit);
    }

    // ===== Hover（低频 + 移动才触发）=====
    if (bMouseMoved)
    {
        HoverTimer += DeltaTime;

        if (HoverTimer >= HoverCheckInterval)
        {
            HoverTimer = 0.f;
            HandleHover(CachedHit);
        }
    }

    // ===== Click（即时）=====
    if (bEnableClick && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        HandleClick(CachedHit);
    }
}
bool ABuildingManager::DoTrace(FHitResult& OutHit)
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return false;

    FVector Start, Dir;
    PC->DeprojectMousePositionToWorld(Start, Dir);

    FVector End = Start + Dir * TraceLength;

    return GetWorld()->LineTraceSingleByChannel(
        OutHit,
        Start,
        End,
        ECC_Visibility
    );
}
//点击
void ABuildingManager::HandleClick(const FHitResult& Hit)
{
    if (!ActiveBuildingComponent) return;
    if (!Hit.bBlockingHit) return;

    AActor* HitActor = Hit.GetActor();
    if (!HitActor) return;

    AActor* Current = HitActor;

    while (Current)
    {
        int32* FloorPtr = ActiveBuildingComponent->ActorToFloorMap.Find(Current);

        if (FloorPtr)
        {
            int32 Floor = *FloorPtr;

            ClearHover();

            ActiveBuildingComponent->ToggleFloor(Floor);

            OnMouseClicked.Broadcast(ActiveBuildingActor, Floor);

            return;
        }

        Current = Current->GetAttachParentActor();
    }
}
//悬停
void ABuildingManager::HandleHover(const FHitResult& Hit)
{
    if (!ActiveBuildingComponent)
    {
        ClearHover();
        return;
    }

    if (ActiveBuildingComponent->bLayeringRunning || ActiveBuildingComponent->bLayeringRunning)
        return;

    if (!Hit.bBlockingHit)
    {
        ClearHover();
        return;
    }

    AActor* HitActor = Hit.GetActor();
    if (!HitActor)
    {
        ClearHover();
        return;
    }

    AActor* Current = HitActor;

    while (Current)
    {
        int32* FloorPtr = ActiveBuildingComponent->ActorToFloorMap.Find(Current);

        if (FloorPtr)
        {
            int32 NewFloor = *FloorPtr;

            if (HoverFloor == NewFloor)
                return;

            ClearHover();

            HoverFloor = NewFloor;
            HoverActor = Current;

            SetActorHighlight(HoverActor, true);
            return;
        }

        Current = Current->GetAttachParentActor();
    }

    ClearHover();
}

void ABuildingManager::ClearHover()
{
    if (HoverActor)
    {
        SetActorHighlight(HoverActor, false);
        HoverActor = nullptr;
        HoverFloor = 0;
    }
}

void ABuildingManager::SetActorHighlight(AActor* Actor, bool bEnable)
{
    if (!Actor) return;

    TArray<UPrimitiveComponent*> PrimComps;
    Actor->GetComponents<UPrimitiveComponent>(PrimComps);

    for (UPrimitiveComponent* Prim : PrimComps)
    {
        if (!Prim) continue;

        if (Prim->bRenderCustomDepth != bEnable)
        {
            Prim->SetRenderCustomDepth(bEnable);
        }
    }
}
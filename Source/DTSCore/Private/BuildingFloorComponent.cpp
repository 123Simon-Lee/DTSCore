#include "BuildingFloorComponent.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

UBuildingFloorComponent::UBuildingFloorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    ConstructorHelpers::FObjectFinder<UCurveFloat> CurveObj(
        TEXT("/Script/Engine.CurveFloat'/DTSCore/CameraTimeline.CameraTimeline'")
    );

    if (CurveObj.Succeeded())
    {
        Curve = CurveObj.Object;
    }
}

void UBuildingFloorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (Curve)
    {
        FOnTimelineFloat LayeringUpdate;
        LayeringUpdate.BindDynamic(this, &UBuildingFloorComponent::OnLayeringUpdate);

        FOnTimelineEvent LayeringFinish;
        LayeringFinish.BindDynamic(this, &UBuildingFloorComponent::OnLayeringFinish);

        LayeringTimeline.AddInterpFloat(Curve, LayeringUpdate);
        LayeringTimeline.SetTimelineFinishedFunc(LayeringFinish);
        LayeringTimeline.SetLooping(false);

        FOnTimelineFloat FloorExpandUpdate;
        FloorExpandUpdate.BindDynamic(this, &UBuildingFloorComponent::OnFloorExpandUpdate);

        FOnTimelineEvent FloorExpandFinish;
        FloorExpandFinish.BindDynamic(this, &UBuildingFloorComponent::OnFloorExpandFinish);

        FloorExpandTimeline.AddInterpFloat(Curve, FloorExpandUpdate);
        FloorExpandTimeline.SetTimelineFinishedFunc(FloorExpandFinish);
        FloorExpandTimeline.SetLooping(false);
    }
}

void UBuildingFloorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    LayeringTimeline.TickTimeline(DeltaTime);
    FloorExpandTimeline.TickTimeline(DeltaTime);
}

// =========================================================
//  解析
// =========================================================

int32 UBuildingFloorComponent::ParseFloorFromName(const FString& Name)
{
    FRegexPattern Pattern(TEXT("^(B?\\d+|R)F$"));

    FRegexMatcher Matcher(Pattern, Name);

    if (Matcher.FindNext())
    {
        FString FloorStr = Matcher.GetCaptureGroup(1);
        if (FloorStr.StartsWith(TEXT("B")))
            return -FCString::Atoi(*FloorStr.RightChop(1));
        // TODO:临时处理
        else if (FloorStr.StartsWith(TEXT("R")))
            return 4;
        else
            return FCString::Atoi(*FloorStr);
    }
    return 0;
}

int32 UBuildingFloorComponent::ParseFloorString(const FString& FloorStr)
{
    if (FloorStr.IsEmpty()) return 0;
    FString Str = FloorStr.ToUpper();
    if (Str.EndsWith(TEXT("F"))) Str = Str.LeftChop(1);
    if (Str.StartsWith(TEXT("B"))) return -FCString::Atoi(*Str.RightChop(1));
    return FCString::Atoi(*Str);
}

int32 UBuildingFloorComponent::ParseVisibilityFloorName(const FString& Name)
{
    FString Str = Name.TrimStartAndEnd().ToUpper();
    if (Str.Equals(TEXT("RF"))) return 9999;
    if (Str.StartsWith(TEXT("B")))
    {
        FString Num = Str.RightChop(1);
        int32 Val = FCString::Atoi(*Num);
        return (Val > 0) ? -Val : 0;
    }
    if (Str.EndsWith(TEXT("F")))
    {
        FString Num = Str.LeftChop(1);
        int32 Val = FCString::Atoi(*Num);
        return (Val > 0) ? Val : 0;
    }
    if (Str.IsNumeric()) return FCString::Atoi(*Str);
    return 0;
}

void UBuildingFloorComponent::CalculateFloorHeight()
{
    FloorZMap.Empty();
    for (auto& Elem : FloorMap)
    {
        int32 Floor = Elem.Key;
        AActor* FloorActor = Elem.Value;
        if (!FloorActor) continue;

        float Z = 0.f;
        TArray<AActor*> FloorChildren;
        FloorActor->GetAttachedActors(FloorChildren, true);
        for (AActor* Child : FloorChildren)
        {
            if (!Child) continue;
            FString Name = UKismetSystemLibrary::GetDisplayName(Child);
            if (Name.EndsWith(TEXT("WQ")))
            {
                FVector Origin, Extent;
                Child->GetActorBounds(true, Origin, Extent);
                Z = Origin.Z;
                break;
            }
        }
        FloorZMap.Add(Floor, Z);
    }
}

FVector UBuildingFloorComponent::GetFloorTHCenter(int32 Floor) const
{
    if (FloorTHCenterMap.Contains(Floor))
        return FloorTHCenterMap[Floor];

    int32 Closest = -1;
    int32 MinDiff = TNumericLimits<int32>::Max();
    for (auto& Elem : FloorTHCenterMap)
    {
        int32 Diff = FMath::Abs(Elem.Key - Floor);
        if (Diff < MinDiff) { MinDiff = Diff; Closest = Elem.Key; }
    }
    return (Closest != -1) ? FloorTHCenterMap[Closest] : FVector::ZeroVector;
}

float UBuildingFloorComponent::GetArmLengthForFloorByFOV(int32 Floor, float FOV, float ZoomScale) const
{
    if (!FloorMap.Contains(Floor)) return BuildingTotalHeight;
    AActor* FloorActor = FloorMap[Floor];
    if (!FloorActor) return BuildingTotalHeight;

    FBox THBox(ForceInit);
    bool bFound = false;
    for (AActor* TH : THActors)
    {
        if (!TH) continue;
        AActor* Parent = TH->GetAttachParentActor();
        if (Parent != FloorActor) continue;
        FVector Origin, Extent;
        TH->GetActorBounds(true, Origin, Extent);
        THBox += FBox(Origin - Extent, Origin + Extent);
        bFound = true;
    }
    if (!bFound)
    {
        FVector Origin, Extent;
        FloorActor->GetActorBounds(true, Origin, Extent);
        THBox = FBox(Origin - Extent, Origin + Extent);
    }

    float Radius = THBox.GetExtent().Size();
    float HalfFOV = FMath::DegreesToRadians(FOV * 0.5f);
    float Distance = Radius / FMath::Tan(HalfFOV);
    return Distance * ZoomScale;
}

void UBuildingFloorComponent::InitFromConfig(const FBuildingFloorConfig& Config)
{
    DisassembleMode = Config.DisassembleMode;
    PullDirection = Config.PullDirection;
    PullDistanceScale = Config.PullDistanceScale;
    SpreadScale = Config.SpreadScale;
    CampusMaxBuildingHeight = Config.CampusMaxBuildingHeight;
    FloorPullScaleOverride = Config.FloorPullScaleOverride;
    if (Config.MaxFloorSizeX > 0.f) MaxFloorSizeX = Config.MaxFloorSizeX;
    if (Config.MaxFloorSizeY > 0.f) MaxFloorSizeY = Config.MaxFloorSizeY;
}


void UBuildingFloorComponent::ParseFloors()
{
    FloorMap.Empty();
    OriginLoc.Empty();
    TargetOffsetMap.Empty();
    HiddenFloors.Empty();
    MaxFloorSizeX = 0.f;
    MaxFloorSizeY = 0.f;
    TotalBuildingHeight = 0.f;
    FloorZMap.Empty();
    StandaloneActors.Empty();
    StandaloneOriginLocMap.Empty();
    FloorTHMap.Empty();
    StandaloneTHActors.Empty();

    if (!DataSmithActor) return;

    TArray<AActor*> Children;
    DataSmithActor->GetAttachedActors(Children);

    TArray<TPair<int32, AActor*>> TempArray;

    // =========================================================
    // Drawer 模式
    // =========================================================
    if (DisassembleMode == EDisassembleMode::Drawer)
    {
        TArray<AActor*> FloorCandidates;

        for (AActor* Child : Children)
        {
            if (!Child) continue;
            FString Name = UKismetSystemLibrary::GetDisplayName(Child);

            if (Name.EndsWith(TEXT("TH")))
            {
                Child->SetActorHiddenInGame(true);
                continue;
            }

            int32 TestFloor = ParseFloorFromName(Name);
            if (TestFloor != 0)
            {
                FloorCandidates.Add(Child);
                continue;
            }

            TArray<AActor*> AllDescendants;
            Child->GetAttachedActors(AllDescendants, true);

            bool bHasFloorDescendant = false;
            for (AActor* Desc : AllDescendants)
            {
                if (!Desc) continue;
                FString DescName = UKismetSystemLibrary::GetDisplayName(Desc);
                if (ParseFloorFromName(DescName) != 0)
                {
                    bHasFloorDescendant = true;
                    FloorCandidates.Add(Desc);
                }
            }

            if (!bHasFloorDescendant)
            {
                // ⭐ Child 本身加进散装
                StandaloneActors.Add(Child);
                StandaloneOriginLocMap.Add(Child, Child->GetActorLocation());

                // ⭐ Child 的所有后代也加进散装（处理有子节点的情况）
                for (AActor* Desc : AllDescendants)
                {
                    if (!Desc) continue;
                    FString DescName = UKismetSystemLibrary::GetDisplayName(Desc);

                    if (!StandaloneOriginLocMap.Contains(Desc))
                        StandaloneOriginLocMap.Add(Desc, Desc->GetActorLocation());

                    if (DescName.Contains(TEXT("_TH_")))
                        StandaloneTHActors.Add(Desc);
                }

                // ⭐ Child 自身名字也检查 TH
                if (Name.Contains(TEXT("_TH_")))
                    StandaloneTHActors.Add(Child);
            }
        }

        for (AActor* Candidate : FloorCandidates)
        {
            if (!Candidate) continue;
            FString Name = UKismetSystemLibrary::GetDisplayName(Candidate);
            if (Name.EndsWith(TEXT("TH")))
            {
                Candidate->SetActorHiddenInGame(true);
                continue;
            }
            int32 Floor = ParseFloorFromName(Name);
            if (Floor == 0) continue;
            TempArray.Add(TPair<int32, AActor*>(Floor, Candidate));
        }
    }
    else
    {
        // =========================================================
        // Visibility 模式
        // =========================================================
        for (AActor* Child : Children)
        {
            FString Name = UKismetSystemLibrary::GetDisplayName(Child);
            if (Name.EndsWith(TEXT("TH")))
            {
                Child->SetActorHiddenInGame(true);
                continue;
            }
            int32 Floor = ParseVisibilityFloorName(Name);
            if (Floor == 0) continue;
            TempArray.Add(TPair<int32, AActor*>(Floor, Child));
        }
    }

    // 排序
    TempArray.Sort([](const TPair<int32, AActor*>& A, const TPair<int32, AActor*>& B)
        {
            int32 FA = A.Key, FB = B.Key;
            if (FA < 0 && FB < 0) return FA < FB;
            if (FA < 0 && FB > 0) return true;
            if (FA > 0 && FB < 0) return false;
            return FA < FB;
        });

    // RF 修正
    int32 MaxPositiveFloor = 0;
    for (auto& Elem : TempArray)
        if (Elem.Key != 9999 && Elem.Key > 0)
            MaxPositiveFloor = FMath::Max(MaxPositiveFloor, Elem.Key);

    int32 RFFloor = MaxPositiveFloor + 1;
    for (auto& Elem : TempArray)
        if (Elem.Key == 9999)
            Elem.Key = RFFloor;

    // =========================================================
    // 主循环
    // =========================================================
    for (auto& Elem : TempArray)
    {
        int32   Floor = Elem.Key;
        AActor* Actor = Elem.Value;
        if (!Actor) continue;

        FloorMap.Add(Floor, Actor);
        ActorToFloorMap.Add(Actor, Floor);
        OriginLoc.Add(Floor, Actor->GetActorLocation());

        if (DisassembleMode == EDisassembleMode::Drawer)
        {
            float RealZ = Actor->GetActorLocation().Z;
            bool bFoundTH = false;
            float MinTHZ = FLT_MAX;

            TArray<AActor*> ChildrenActors;
            Actor->GetAttachedActors(ChildrenActors, true);

            FFloorTHList THList;

            for (AActor* SubChild : ChildrenActors)
            {
                if (!SubChild) continue;
                FString SubName = UKismetSystemLibrary::GetDisplayName(SubChild);
                if (SubName.Contains(TEXT("_TH_")))
                {
                    float Z = SubChild->GetActorLocation().Z;
                    MinTHZ = FMath::Min(MinTHZ, Z);
                    bFoundTH = true;
                    THList.Actors.Add(SubChild);
                }
            }

            if (THList.Actors.Num() > 0)
                FloorTHMap.Add(Floor, THList);

            if (bFoundTH)
            {
                RealZ = MinTHZ;
            }
            else
            {
                FBox FloorBox(ForceInit);
                for (AActor* Child : ChildrenActors)
                {
                    if (!Child) continue;
                    FVector O, E;
                    Child->GetActorBounds(true, O, E);
                    FloorBox += FBox(O - E, O + E);
                }
                {
                    FVector O, E;
                    Actor->GetActorBounds(true, O, E);
                    FloorBox += FBox(O - E, O + E);
                }
                RealZ = FloorBox.IsValid ? FloorBox.Min.Z : Actor->GetActorLocation().Z;
            }

            FloorZMap.Add(Floor, RealZ);
        }
    }

    // =========================================================
    // Drawer：把绝对 Z 转成相对高度，计算 TotalBuildingHeight
    // =========================================================
    if (DisassembleMode == EDisassembleMode::Drawer && FloorZMap.Num() > 0)
    {
        float MinZ = FLT_MAX;
        float MaxZ = -FLT_MAX;
        for (auto& Elem : FloorZMap) { MinZ = FMath::Min(MinZ, Elem.Value); MaxZ = FMath::Max(MaxZ, Elem.Value); }
        for (auto& Elem : FloorZMap) Elem.Value -= MinZ;
        TotalBuildingHeight = MaxZ - MinZ;

        FloorHeightMap.Empty();
        TArray<int32> SortedFloors;
        FloorZMap.GetKeys(SortedFloors);
        SortedFloors.Sort();

        for (int32 i = 0; i < SortedFloors.Num(); i++)
        {
            int32 F = SortedFloors[i];
            AActor* tempActor = FloorMap.Contains(F) ? FloorMap[F] : nullptr;
            float FloorH = 0.f;

            if (tempActor)
            {
                FBox FloorBox(ForceInit);
                TArray<AActor*> AllChildren;
                tempActor->GetAttachedActors(AllChildren, true);
                for (AActor* Child : AllChildren)
                {
                    if (!Child) continue;
                    FVector O, E;
                    Child->GetActorBounds(true, O, E);
                    FloorBox += FBox(O - E, O + E);
                }
                { FVector O, E; tempActor->GetActorBounds(true, O, E); FloorBox += FBox(O - E, O + E); }
                if (FloorBox.IsValid) FloorH = FloorBox.GetSize().Z;
            }

            if (FloorH <= 0.f)
            {
                if (i + 1 < SortedFloors.Num())
                    FloorH = FloorZMap[SortedFloors[i + 1]] - FloorZMap[F];
                else
                    FloorH = TotalBuildingHeight / FMath::Max(SortedFloors.Num(), 1);
            }
            FloorHeightMap.Add(F, FloorH);
        }

        if (MaxFloorSizeX <= 0.f || MaxFloorSizeY <= 0.f)
        {
            TArray<float> AllSizesX, AllSizesY;
            for (auto& Elem : FloorMap)
            {
                AActor* FA = Elem.Value;
                if (!FA) continue;
                FBox FloorBox(ForceInit);
                TArray<AActor*> FAChildren;
                FA->GetAttachedActors(FAChildren, true);
                for (AActor* Child : FAChildren)
                {
                    if (!Child) continue;
                    FVector O, E;
                    Child->GetActorBounds(true, O, E);
                    FloorBox += FBox(O - E, O + E);
                }
                { FVector O, E; FA->GetActorBounds(true, O, E); FloorBox += FBox(O - E, O + E); }
                if (!FloorBox.IsValid) continue;
                FVector Ext = FloorBox.GetExtent();
                AllSizesX.Add(Ext.X * 2.f);
                AllSizesY.Add(Ext.Y * 2.f);
            }

            if (AllSizesX.Num() > 0)
            {
                TArray<float> SortedX = AllSizesX, SortedY = AllSizesY;
                SortedX.Sort(); SortedY.Sort();
                float MedianX = SortedX[SortedX.Num() / 2];
                float MedianY = SortedY[SortedY.Num() / 2];
                const float OutlierScale = 3.f;
                float BestSizeX = 0.f, BestSizeY = 0.f;
                for (int32 i = 0; i < AllSizesX.Num(); i++)
                {
                    if (AllSizesX[i] <= MedianX * OutlierScale) BestSizeX = FMath::Max(BestSizeX, AllSizesX[i]);
                    if (AllSizesY[i] <= MedianY * OutlierScale) BestSizeY = FMath::Max(BestSizeY, AllSizesY[i]);
                }
                if (MaxFloorSizeX <= 0.f) MaxFloorSizeX = BestSizeX;
                if (MaxFloorSizeY <= 0.f) MaxFloorSizeY = BestSizeY;
            }
        }
    }

    // =========================================================
    // Visibility 模式
    // =========================================================
    if (DisassembleMode == EDisassembleMode::Visibility)
    {
        THActors.Empty();
        THCenterMap.Empty();
        FloorTHCenterMap.Empty();

        for (auto& Elem : FloorMap)
        {
            int32   FloorNum = Elem.Key;
            AActor* FloorActor = Elem.Value;
            if (!FloorActor) continue;

            TArray<AActor*> FloorChildren;
            FloorActor->GetAttachedActors(FloorChildren, true);

            for (AActor* FloorChild : FloorChildren)
            {
                if (!FloorChild) continue;
                FString ChildName = UKismetSystemLibrary::GetDisplayName(FloorChild);
                bool bIsTH = ChildName.EndsWith(TEXT("TH"));
                bool bIsWQ = ChildName.EndsWith(TEXT("WQ"));
                if (!bIsTH && !bIsWQ) continue;

                if (bIsTH)
                {
                    FloorChild->SetActorHiddenInGame(true);
                    THActors.Add(FloorChild);
                    FVector Origin, Extent;
                    FloorChild->GetActorBounds(true, Origin, Extent);
                    THCenterMap.Add(FloorChild, Origin);
                    if (FloorTHCenterMap.Contains(FloorNum))
                        FloorTHCenterMap[FloorNum] = (FloorTHCenterMap[FloorNum] + Origin) * 0.5f;
                    else
                        FloorTHCenterMap.Add(FloorNum, Origin);
                }
                if (bIsWQ) WQActors.Add(FloorChild);
            }
        }

        if (THActors.Num() > 0)
        {
            FBox THBox(ForceInit);
            for (AActor* TH : THActors)
            {
                if (!TH) continue;
                FVector Origin, Extent;
                TH->GetActorBounds(true, Origin, Extent);
                THBox += FBox(Origin - Extent, Origin + Extent);
            }
            BuildingTotalHeight = THBox.GetSize().Z;
        }
        CalculateFloorHeight();
    }
}


// =========================================================
//  分层 / 归位
// =========================================================

void UBuildingFloorComponent::LayeringDisplay()
{
    if (bLayeringRunning && LayeringTimeline.IsReversing())
    {
        LayeringTimeline.Play();
        return;
    }
    if (bIsLayering && !bLayeringRunning) return;
    bLayeringRunning = true;
    LayeringTimeline.PlayFromStart();
}

void UBuildingFloorComponent::BackToNormal()
{
    if (ActiveFloor != 0) ToggleFloor(ActiveFloor);

    if (bLayeringRunning && !LayeringTimeline.IsReversing())
    {
        LayeringTimeline.Reverse();
        return;
    }
    if (!bIsLayering && !bLayeringRunning) return;
    bLayeringRunning = true;
    LayeringTimeline.ReverseFromEnd();
}

// =========================================================
//  单层抽屉模式拆楼
// =========================================================

void UBuildingFloorComponent::ToggleFloor(int32 Floor)
{
    if (!bIsLayering || !FloorMap.Contains(Floor)) return;

    for (auto& Elem : FloorMap)
    {
        TargetOffsetMap.Add(Elem.Key, FVector::Zero());
        FVector NowLocation = Elem.Value->GetActorLocation();
        NowLocation.Z = 0.f;
        NowOffsetMap.Add(Elem.Key, NowLocation);
    }

    if (ActiveFloor == Floor)
    {
        ActiveFloor = 0;
        // 归位时恢复 TH：只显示最高层 TH，散装 TH 恢复显示
        UpdateTHVisibilityForDrawer(true);
    }
    else
    {
        // 恢复上一个抽出楼层的 TH
        if (ActiveFloor != 0 && FloorTHMap.Contains(ActiveFloor))
        {
            for (AActor* TH : FloorTHMap[ActiveFloor].Actors)
                if (TH) TH->SetActorHiddenInGame(false);
        }

        ActiveFloor = Floor;
        AActor* Actor = FloorMap[Floor];
        if (!Actor) return;

        // 只隐藏当前抽出楼层的 TH
        if (FloorTHMap.Contains(Floor))
        {
            for (AActor* TH : FloorTHMap[Floor].Actors)
                if (TH) TH->SetActorHiddenInGame(true);
        }

        // =========================================================
        // 第一步：确定拉出方向
        // =========================================================
        FVector Dir = FVector::ZeroVector;
        switch (PullDirection)
        {
        case EPullDirection::Auto:     Dir = (MaxFloorSizeX >= MaxFloorSizeY) ? FVector(1, 0, 0) : FVector(0, 1, 0); break;
        case EPullDirection::Right:    Dir = FVector(1, 0, 0);  break;
        case EPullDirection::Left:     Dir = FVector(-1, 0, 0); break;
        case EPullDirection::Forward:  Dir = FVector(0, 1, 0);  break;
        case EPullDirection::Backward: Dir = FVector(0, -1, 0); break;
        default:                       Dir = FVector(1, 0, 0);  break;
        }

        FVector WorldDir = Dir;
        if (DataSmithActor) WorldDir = DataSmithActor->GetActorTransform().TransformVector(Dir);
        WorldDir.Normalize();

        // =========================================================
        // 第二步：计算基础拉出距离
        // =========================================================
        float SizeX = MaxFloorSizeX, SizeY = MaxFloorSizeY;
        if (SizeX <= 0.f || SizeY <= 0.f)
        {
            FBox FloorBox(ForceInit);
            TArray<AActor*> AllChildren;
            Actor->GetAttachedActors(AllChildren, true);
            for (AActor* Child : AllChildren)
            {
                if (!Child) continue;
                FVector O, E; Child->GetActorBounds(true, O, E);
                FloorBox += FBox(O - E, O + E);
            }
            { FVector O, E; Actor->GetActorBounds(true, O, E); FloorBox += FBox(O - E, O + E); }
            FVector BoxExtent = FloorBox.IsValid ? FloorBox.GetExtent() : FVector(1000.f);
            if (SizeX <= 0.f) SizeX = BoxExtent.X * 2.f;
            if (SizeY <= 0.f) SizeY = BoxExtent.Y * 2.f;
        }

        float BaseDistance = 0.f;
        switch (PullDirection)
        {
        case EPullDirection::Auto:     BaseDistance = (MaxFloorSizeX >= MaxFloorSizeY) ? SizeX : SizeY; break;
        case EPullDirection::Right:
        case EPullDirection::Left:     BaseDistance = SizeX; break;
        case EPullDirection::Forward:
        case EPullDirection::Backward: BaseDistance = SizeY; break;
        default:                       BaseDistance = SizeX; break;
        }

        // =========================================================
        // 第三步：用 Bounds 边缘计算真实拉出距离
        // =========================================================
        float MedianSize = (MaxFloorSizeX >= MaxFloorSizeY) ? MaxFloorSizeX : MaxFloorSizeY;

        FBox AllFloorsBox(ForceInit);
        for (auto& Elem : FloorMap)
        {
            if (!Elem.Value) continue;
            FBox FB(ForceInit);
            TArray<AActor*> FC;
            Elem.Value->GetAttachedActors(FC, true);
            for (AActor* C : FC)
            {
                if (!C) continue;
                FVector O, E; C->GetActorBounds(true, O, E);
                FB += FBox(O - E, O + E);
            }
            { FVector O, E; Elem.Value->GetActorBounds(true, O, E); FB += FBox(O - E, O + E); }
            if (!FB.IsValid) continue;
            float LayerSize = FVector::DotProduct(FB.GetExtent(), WorldDir.GetAbs()) * 2.f;
            if (LayerSize > MedianSize * 3.f) continue;
            AllFloorsBox += FB;
        }

        FBox ThisFloorBox(ForceInit);
        {
            TArray<AActor*> AllChildren;
            Actor->GetAttachedActors(AllChildren, true);
            for (AActor* Child : AllChildren)
            {
                if (!Child) continue;
                FVector O, E; Child->GetActorBounds(true, O, E);
                ThisFloorBox += FBox(O - E, O + E);
            }
            { FVector O, E; Actor->GetActorBounds(true, O, E); ThisFloorBox += FBox(O - E, O + E); }
        }

        float Distance = BaseDistance;
        if (AllFloorsBox.IsValid && ThisFloorBox.IsValid)
        {
            float ThisSize = FVector::DotProduct(ThisFloorBox.GetExtent(), WorldDir.GetAbs()) * 2.f;
            if (ThisSize <= MedianSize * 3.f)
            {
                float BuildingFrontEdge =
                    (WorldDir.X > 0.f) ? AllFloorsBox.Max.X :
                    (WorldDir.X < 0.f) ? -AllFloorsBox.Min.X :
                    (WorldDir.Y > 0.f) ? AllFloorsBox.Max.Y : -AllFloorsBox.Min.Y;

                float FloorBackEdge =
                    (WorldDir.X > 0.f) ? ThisFloorBox.Min.X :
                    (WorldDir.X < 0.f) ? -ThisFloorBox.Max.X :
                    (WorldDir.Y > 0.f) ? ThisFloorBox.Min.Y : -ThisFloorBox.Max.Y;

                Distance = FMath::Max(BuildingFrontEdge - FloorBackEdge, BaseDistance);
            }
        }

        float FinalScale = FloorPullScaleOverride.Contains(Floor)
            ? FloorPullScaleOverride[Floor] : PullDistanceScale;
        Distance *= FinalScale;

        TargetOffsetMap[ActiveFloor] = WorldDir * Distance;
    }
    bExpandRunning = true;
    FloorExpandTimeline.PlayFromStart();
}

// =========================================================
//  通用显隐 API
// =========================================================

void UBuildingFloorComponent::HideFloors(const TArray<int32>& Floors)
{
    for (int32 Floor : Floors) ApplyFloorVisibility(Floor, false, -1);
}

void UBuildingFloorComponent::ShowFloors(const TArray<int32>& Floors)
{
    int32 MaxFloor = -1;
    for (int32 Floor : Floors) MaxFloor = FMath::Max(MaxFloor, Floor);
    for (int32 Floor : Floors) ApplyFloorVisibility(Floor, true, MaxFloor);
}

void UBuildingFloorComponent::HideAbove(int32 Floor)
{
    for (auto& Elem : FloorMap)
        if (Elem.Key > Floor) ApplyFloorVisibility(Elem.Key, false, -1);
}

void UBuildingFloorComponent::ShowAll()
{
    int32 MaxFloor = -1;
    for (auto& Elem : FloorMap) MaxFloor = FMath::Max(MaxFloor, Elem.Key);
    for (auto& Elem : FloorMap) ApplyFloorVisibility(Elem.Key, true, MaxFloor);

    if (DataSmithActor)
    {
        TArray<AActor*> Ch;
        DataSmithActor->GetAttachedActors(Ch);
        for (AActor* Child : Ch)
            if (Child->GetName().EndsWith(TEXT("TH")))
                Child->SetActorHiddenInGame(true);
    }
}

void UBuildingFloorComponent::SetAllFloorsVisibility(bool bVisible)
{
    int32 MaxFloor = -1;
    if (bVisible) for (auto& Elem : FloorMap) MaxFloor = FMath::Max(MaxFloor, Elem.Key);
    for (auto& Elem : FloorMap) ApplyFloorVisibility(Elem.Key, bVisible, MaxFloor);
}

// =========================================================
//  显隐模式专属 API
// =========================================================

void UBuildingFloorComponent::ToggleFloorVisibility(int32 Floor)
{
    if (!FloorMap.Contains(Floor)) return;
    TargetBuilding = FloorMap[Floor];
    for (auto& Elem : FloorMap)
        ApplyFloorVisibility(Elem.Key, Elem.Key <= Floor, Floor);
}

void UBuildingFloorComponent::ShowOnlyFloor(int32 Floor)
{
    for (auto& Elem : FloorMap) ApplyFloorVisibility(Elem.Key, Elem.Key == Floor, -1);
}

void UBuildingFloorComponent::ShowFloorRange(int32 MinFloor, int32 MaxFloor)
{
    for (auto& Elem : FloorMap)
        ApplyFloorVisibility(Elem.Key, Elem.Key >= MinFloor && Elem.Key <= MaxFloor, MaxFloor);
}

bool UBuildingFloorComponent::IsFloorVisible(int32 Floor) const
{
    return !HiddenFloors.Contains(Floor);
}

// =========================================================
//  内部：统一分发
// =========================================================

void UBuildingFloorComponent::ApplyFloorVisibility(int32 Floor, bool bVisible, int32 CurrentToggleFloor)
{
    if (!FloorMap.Contains(Floor)) return;
    AActor* Actor = FloorMap[Floor];
    if (!Actor) return;

    Actor->SetActorHiddenInGame(!bVisible);

    TArray<AActor*> AttachedActors;
    Actor->GetAttachedActors(AttachedActors, true);
    for (AActor* Child : AttachedActors)
    {
        if (!Child) continue;
        FString ChildName = UKismetSystemLibrary::GetDisplayName(Child);
        if (ChildName.EndsWith(TEXT("TH")))
        {
            const bool bTHVisible = bVisible && (Floor < CurrentToggleFloor);
            Child->SetActorHiddenInGame(!bTHVisible);
            continue;
        }
        Child->SetActorHiddenInGame(!bVisible);
    }

    if (DisassembleMode == EDisassembleMode::Visibility)
    {
        if (bVisible) HiddenFloors.Remove(Floor);
        else          HiddenFloors.Add(Floor);
        OnFloorVisibilityChanged.Broadcast(Floor, bVisible);
    }
}

// =========================================================
//  Timeline 回调
// =========================================================

void UBuildingFloorComponent::OnLayeringUpdate(float Value)
{
    if (DisassembleMode == EDisassembleMode::Drawer)
    {
        TArray<int32> SortedFloors;
        FloorMap.GetKeys(SortedFloors);
        SortedFloors.Sort();

        int32 FloorCount = SortedFloors.Num();
        float BaseHeight = (CampusMaxBuildingHeight > 0.f) ? CampusMaxBuildingHeight : TotalBuildingHeight;
        float SpreadStep = (FloorCount > 1) ? (TotalBuildingHeight / (float)(FloorCount - 1)) * SpreadScale : 0.f;

        for (int32 i = 0; i < SortedFloors.Num(); i++)
        {
            int32   Floor = SortedFloors[i];
            AActor* Actor = FloorMap[Floor];
            if (!Actor || !OriginLoc.Contains(Floor)) continue;

            FVector Start = OriginLoc[Floor];
            FVector Target = Start;
            Target.Z = Start.Z + BaseHeight + (float)i * SpreadStep;
            Actor->SetActorLocation(FMath::Lerp(Start, Target, Value));
        }

        for (AActor* SA : StandaloneActors)
        {
            if (!SA || !StandaloneOriginLocMap.Contains(SA)) continue;
            FVector Origin = StandaloneOriginLocMap[SA];
            FVector Target = Origin;
            Target.Z = Origin.Z + BaseHeight;
            SA->SetActorLocation(FMath::Lerp(Origin, Target, Value));

            TArray<AActor*> Descendants;
            SA->GetAttachedActors(Descendants, true);
            for (AActor* Desc : Descendants)
            {
                if (!Desc || !StandaloneOriginLocMap.Contains(Desc)) continue;
                FVector DescOrigin = StandaloneOriginLocMap[Desc];
                FVector DescTarget = DescOrigin;
                DescTarget.Z = DescOrigin.Z + BaseHeight;
                Desc->SetActorLocation(FMath::Lerp(DescOrigin, DescTarget, Value));
            }
        }
    }
    else
    {
        if (FloorMap.Num() == 0) return;
        float GroundZ = GetGroundZ();
        float BaseOffsetZ = GroundZ + TotalBuildingHeight;

        for (auto& Elem : FloorMap)
        {
            int32   Floor = Elem.Key;
            AActor* Actor = Elem.Value;
            if (!OriginLoc.Contains(Floor)) continue;
            FVector Start = OriginLoc[Floor];
            float OffsetZ = FloorZMap.Contains(Floor) ? FloorZMap[Floor] * 1.5f : 0.f;
            FVector Target = Start;
            Target.Z = BaseOffsetZ + OffsetZ;
            Actor->SetActorLocation(FMath::Lerp(Start, Target, Value));
        }
    }
}

void UBuildingFloorComponent::OnLayeringFinish()
{
    bLayeringRunning = false;

    float Pos = LayeringTimeline.GetPlaybackPosition();
    float Len = LayeringTimeline.GetTimelineLength();

    if (Pos <= KINDA_SMALL_NUMBER)
    {
        bIsLayering = false;
        ActiveFloor = 0;
        UpdateTHVisibilityForDrawer(false);
    }
    else if (Pos >= Len - KINDA_SMALL_NUMBER)
    {
        bIsLayering = true;
        LayeredLoc.Empty();
        for (auto& Elem : FloorMap)
        {
            LayeredLoc.Add(Elem.Key, Elem.Value->GetActorLocation());
            TargetOffsetMap.Add(Elem.Key, FVector::Zero());
        }
        UpdateTHVisibilityForDrawer(true);
    }

    OnLayeringTimeLineFinished.Broadcast();
}

void UBuildingFloorComponent::OnFloorExpandUpdate(float Value)
{
    if (FloorMap.Num() == 0) return;
    for (auto& Elem : FloorMap)
    {
        int32   Floor = Elem.Key;
        AActor* Actor = Elem.Value;
        if (!LayeredLoc.Contains(Floor)) continue;

        FVector Base = LayeredLoc[Floor];
        FVector Final = Base;
        Final += FMath::Lerp(NowOffsetMap[Floor], TargetOffsetMap[Floor], Value);
        Final.Z = Actor->GetActorLocation().Z;
        Actor->SetActorLocation(Final);
    }
}

void UBuildingFloorComponent::OnFloorExpandFinish()
{
    bExpandRunning = false;
    OnFloorExpandTimelineFinished.Broadcast();
}

void UBuildingFloorComponent::UpdateTHVisibilityForDrawer(bool bLayering)
{
    if (DisassembleMode != EDisassembleMode::Drawer) return;

    int32 MaxFloor = TNumericLimits<int32>::Min();
    for (auto& Elem : FloorMap) MaxFloor = FMath::Max(MaxFloor, Elem.Key);

    // 楼层 TH：分层时只显示最高层，归位时全部显示
    for (auto& Elem : FloorTHMap)
    {
        int32 Floor = Elem.Key;
        for (AActor* TH : Elem.Value.Actors)
        {
            if (!TH) continue;
            if (bLayering)
                TH->SetActorHiddenInGame(Floor != MaxFloor);
            else
                TH->SetActorHiddenInGame(false);
        }
    }

    // 散装 TH：分层时隐藏，归位时显示
    for (AActor* TH : StandaloneTHActors)
        if (TH) TH->SetActorHiddenInGame(bLayering);
}

float UBuildingFloorComponent::GetGroundZ()
{
    if (!DataSmithActor) return 0.f;
    FVector Start = DataSmithActor->GetActorLocation() + FVector(0, 0, 10000);
    FVector End = Start - FVector(0, 0, 20000);
    FHitResult Hit;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(DataSmithActor);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
        return Hit.Location.Z;
    return 0.f;
}

void UBuildingFloorComponent::ForceReset()
{
    LayeringTimeline.Stop();
    FloorExpandTimeline.Stop();
    bLayeringRunning = false;
    bExpandRunning = false;
    ActiveFloor = 0;

    if (bIsLayering)
    {
        for (auto& Elem : FloorMap)
        {
            AActor* Actor = Elem.Value;
            if (!Actor) continue;
            if (LayeredLoc.Contains(Elem.Key))
                Actor->SetActorLocation(LayeredLoc[Elem.Key]);
        }
    }
    else
    {
        for (auto& Elem : FloorMap)
        {
            AActor* Actor = Elem.Value;
            if (!Actor) continue;
            if (OriginLoc.Contains(Elem.Key))
                Actor->SetActorLocation(OriginLoc[Elem.Key]);
        }
        for (auto& Pair : StandaloneOriginLocMap)
            if (Pair.Key) Pair.Key->SetActorLocation(Pair.Value);
    }
}
// Fill out your copyright notice in the Description page of Project Settings.


#include "UDSTCoreFunctionLibrary.h"
#include <Kismet/KismetSystemLibrary.h>


FVector UUDSTCoreFunctionLibrary::GetActorTopWorldLocation(AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}

	FVector Origin;
	FVector BoxExtent;

	// 获取世界空间包围盒
	Actor->GetActorBounds(true, Origin, BoxExtent);

	// 顶部位置（关键！）
	return Origin + FVector(0.f, 0.f, BoxExtent.Z);
}

FBox UUDSTCoreFunctionLibrary::GetActorsBoundingBox(const TArray<AActor*>& Actors)
{
    if (Actors.Num() == 0)
    {
        return FBox();
    }

    FBox TotalBox(EForceInit::ForceInit);

    for (AActor* Actor : Actors)
    {
        if (!Actor) continue;

        FVector Origin;
        FVector Extent;

        Actor->GetActorBounds(true, Origin, Extent);

        // 转成 FBox（最关键一步）
        FBox Box = FBox::BuildAABB(Origin, Extent);

        // 合并包围盒
        TotalBox += Box;
    }
	return TotalBox;
}

FVector UUDSTCoreFunctionLibrary::GetActorsCenter(const TArray<AActor*>& Actors)
{
    if (Actors.Num() == 0)
    {
        return FVector::ZeroVector;
    }

    FBox TotalBox= GetActorsBoundingBox(Actors);

   
    // 返回整体中心
    return TotalBox.GetCenter();
}

float UUDSTCoreFunctionLibrary::GetActorsHeight(const TArray<AActor*>& Actors)
{
    FBox TotalBox= GetActorsBoundingBox(Actors);

    return TotalBox.GetExtent().Z; // 半高
}


bool UUDSTCoreFunctionLibrary::ReadConfigString(const FString& Section, const FString& Key, const FString& ConfigFilePath, FString& OutValue)
{
    if (ConfigFilePath.IsEmpty() || Section.IsEmpty() || Key.IsEmpty())
    {
        return false;
    }

    // 每次读前 LoadFile 确保拿到磁盘最新值
    GConfig->LoadFile(ConfigFilePath);
    return GConfig->GetString(*Section, *Key, OutValue, ConfigFilePath);
}

FString UUDSTCoreFunctionLibrary::SetResolutionFromIni(const FString& IniFilePath)
{
    // 确保文件存在
    if (!FPaths::FileExists(IniFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("INI file not found: %s"), *IniFilePath);
        return FString();
    }

    // 读取INI文件
    FConfigFile ConfigFile;
    ConfigFile.Read(IniFilePath);

    // 获取分辨率设置（假设存储在[Resolution]段）
    FString ResolutionString;
    if (!ConfigFile.GetString(TEXT("Resolution"), TEXT("ScreenResolution"), ResolutionString))
    {
        UE_LOG(LogTemp, Error, TEXT("Resolution setting not found in INI file"));
        return FString();
    }

    // 解析分辨率字符串（格式示例："3840x1000f" 或 "1920x1080w"）
    int32 Width = 0;
    int32 Height = 0;
    bool bFullscreen = false;
    if (!ParseResolutionString(ResolutionString, Width, Height, bFullscreen))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid resolution format: %s"), *ResolutionString);
        return FString();
    }

    // 设置分辨率
    FString Command = FString::Printf(TEXT("r.setres %dx%d%s"),
        Width, Height,
        bFullscreen ? TEXT("f") : TEXT("w"));


    return Command;
}

bool UUDSTCoreFunctionLibrary::ParseResolutionString(const FString& ResString, int32& OutWidth, int32& OutHeight, bool& OutFullscreen)
{
    // 示例格式：3840x1000f 或 1920x1080w
    FString WidthStr, HeightStr;
    FString ModeStr;

    // 分离模式和分辨率
    if (ResString.Len() < 3) return false;

    ModeStr = ResString.Right(1).ToLower();
    FString ResolutionPart = ResString.LeftChop(1);

    // 分离宽高
    if (!ResolutionPart.Split(TEXT("x"), &WidthStr, &HeightStr))
        return false;

    // 转换数值
    OutWidth = FCString::Atoi(*WidthStr);
    OutHeight = FCString::Atoi(*HeightStr);

    // 确定模式
    if (ModeStr == TEXT("f"))
        OutFullscreen = true;
    else if (ModeStr == TEXT("w"))
        OutFullscreen = false;
    else
        return false;

    return (OutWidth > 0 && OutHeight > 0);
}


FString UUDSTCoreFunctionLibrary::RemoveLastSuffix(const FString& InputString)
{
    if (InputString.IsEmpty()) return FString();

    FString CleanString = InputString.Replace(TEXT(" "), TEXT(""));

    // 收集所有 '-' 的位置
    TArray<int32> DashIndices;
    for (int32 i = 0; i < CleanString.Len(); i++)
    {
        if (CleanString[i] == TEXT('-'))
            DashIndices.Add(i);
    }

    // 至少要有4个'-'才需要裁剪，保留前3个'-'之内的内容
    if (DashIndices.Num() < 4)
        return CleanString;

    // 截到第4个'-'之前（index从0起，第4个是DashIndices[3]）
    return CleanString.Left(DashIndices[3]);
}


static int32 ClampQualityLevel(int32 Val)
{
    return FMath::Clamp(Val, 0, 3);
}

void UUDSTCoreFunctionLibrary::SetRenderQuality(float ResolutionQuality, int32 ViewDistanceQuality, int32 AntiAliasingQuality, int32 ShadowQuality, int32 GlobalIlluminationQuality, int32 ReflectionQuality, int32 PostProcessQuality, int32 TextureQuality, int32 EffectsQuality, int32 FoliageQuality, int32 ShadingQuality)
{
    ResolutionQuality = FMath::Clamp(ResolutionQuality, 10.f, 100.f);

    Scalability::FQualityLevels Quality;
    Quality.ResolutionQuality = ResolutionQuality;
    Quality.ViewDistanceQuality = ClampQualityLevel(ViewDistanceQuality);
    Quality.AntiAliasingQuality = ClampQualityLevel(AntiAliasingQuality);
    Quality.ShadowQuality = ClampQualityLevel(ShadowQuality);
    Quality.GlobalIlluminationQuality = ClampQualityLevel(GlobalIlluminationQuality);
    Quality.ReflectionQuality = ClampQualityLevel(ReflectionQuality);
    Quality.PostProcessQuality = ClampQualityLevel(PostProcessQuality);
    Quality.TextureQuality = ClampQualityLevel(TextureQuality);
    Quality.EffectsQuality = ClampQualityLevel(EffectsQuality);
    Quality.FoliageQuality = ClampQualityLevel(FoliageQuality);
    Quality.ShadingQuality = ClampQualityLevel(ShadingQuality);

    Scalability::SetQualityLevels(Quality);
    Scalability::SaveState(GGameUserSettingsIni);

    UE_LOG(LogTemp, Log, TEXT("[RenderQuality] Res=%.0f ViewDist=%d Shadow=%d GI=%d"),
        ResolutionQuality, ViewDistanceQuality, ShadowQuality, GlobalIlluminationQuality);
}

void UUDSTCoreFunctionLibrary::SetRenderQualityPreset(int32 QualityLevel)
{
    QualityLevel = FMath::Clamp(QualityLevel, 0, 3);

    switch (QualityLevel)
    {
    case 0:
        SetRenderQuality(50.f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        break;
    case 1:
        SetRenderQuality(65.f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1);
        break;
    case 2:
        SetRenderQuality(70.f, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2);
        break;
    case 3:
        SetRenderQuality(100.f, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3);
        break;
    }
}

float UUDSTCoreFunctionLibrary::GetResolutionQuality()
{
    return Scalability::GetQualityLevels().ResolutionQuality;
}

void UUDSTCoreFunctionLibrary::ApplyPerformanceSettings()
{
    static const TArray<FString> Commands = {
        TEXT("r.Shadow.MaxResolution 1024"),
        TEXT("r.Shadow.DistanceScale 0.5"),
        TEXT("r.Shadow.RadiusThreshold 0.05"),
        TEXT("r.DynamicRes.OperationMode 1"),
        TEXT("r.DynamicRes.FrameTimeBudget 33"),
    };

    UWorld* World = GEngine ? GEngine->GetCurrentPlayWorld() : nullptr;
    if (!World) return;

    APlayerController* PC = World->GetFirstPlayerController();

    for (const FString& Cmd : Commands)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(World, Cmd, PC);
    }
}

bool UUDSTCoreFunctionLibrary::WriteConfigString(const FString& Section, const FString& Key, const FString& Value, const FString& ConfigFilePath)
{
    if (ConfigFilePath.IsEmpty() || Section.IsEmpty() || Key.IsEmpty())
    {
        return false;
    }

    // 移除旧缓存，确保下次读取从磁盘重新加载
    GConfig->RemoveKey(*Section, *Key, ConfigFilePath);

    GConfig->SetString(*Section, *Key, *Value, ConfigFilePath);
    GConfig->Flush(false, ConfigFilePath);
    return true;
}

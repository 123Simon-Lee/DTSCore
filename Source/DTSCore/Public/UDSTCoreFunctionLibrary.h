// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Actor.h"
#include "InstancedStruct.h" 
#include "Engine/DataTable.h"
#include "UDSTCoreFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class DTSCORE_API UUDSTCoreFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	//拿到碰撞盒子 且算出坐标和中心点 或者高度的方法
	UFUNCTION(BlueprintCallable, Category = "UDST|Mesh")
	static FVector GetActorTopWorldLocation(AActor* Actor);
	UFUNCTION(BlueprintCallable, Category = "UDST|Mesh")
	static FBox GetActorsBoundingBox(const TArray<AActor*>& Actors);
	UFUNCTION(BlueprintCallable, Category = "UDST|Mesh")
	static FVector GetActorsCenter(const TArray<AActor*>& Actors);
	UFUNCTION(BlueprintCallable, Category = "UDST|Mesh")
	static float GetActorsHeight(const TArray<AActor*>& Actors);

	//config 配置方法 Ini文件读取 
	UFUNCTION(BlueprintCallable, Category = "UDST|Config")
	static bool ReadConfigString(const FString& Section, const FString& Key, const FString& ConfigFilePath, FString& OutValue);
	UFUNCTION(BlueprintCallable, Category = "UDST|Config")
	static FString SetResolutionFromIni(const FString& IniFilePath);
	UFUNCTION(BlueprintCallable, Category = "UDST|Config")
	static bool ParseResolutionString(const FString& ResString, int32& OutWidth, int32& OutHeight, bool& OutFullscreen);
	//
	UFUNCTION(BlueprintCallable, Category = "UDST|String Process")
	static FString RemoveLastSuffix(const FString& InputString);

	//DataTable
	UFUNCTION(BlueprintCallable, Category = "UDST|DataTable")
	static bool GetDataTableRowAsInstancedStruct(
		UDataTable* Table,
		FName RowName,
		FInstancedStruct& OutRow);   // ← 标准 UFUNCTION，无需 CustomThunk

	UFUNCTION(BlueprintCallable, Category = "UDST|DataTable")
	static int32 GetDataTableAllRowsAsInstancedStruct(
		UDataTable* Table,
		TArray<FInstancedStruct>& OutRows);

	//控制渲染质量
	UFUNCTION(BlueprintCallable, Category = "Rendering|Quality")
	static void SetRenderQuality(
		float ResolutionQuality = 70.f,
		int32 ViewDistanceQuality = 2,
		int32 AntiAliasingQuality = 2,
		int32 ShadowQuality = 2,
		int32 GlobalIlluminationQuality = 2,
		int32 ReflectionQuality = 2,
		int32 PostProcessQuality = 3,
		int32 TextureQuality = 3,
		int32 EffectsQuality = 3,
		int32 FoliageQuality = 3,
		int32 ShadingQuality = 3
	);
	UFUNCTION(BlueprintCallable, Category = "Rendering|Quality")
	static void SetRenderQualityPreset(int32 QualityLevel = 2);

	UFUNCTION(BlueprintPure, Category = "Rendering|Quality")
	static float GetResolutionQuality();

	//控制阴影系数 使用控制台命令行
	UFUNCTION(BlueprintCallable, Category = "Rendering|Performance")
	static void ApplyPerformanceSettings();
};

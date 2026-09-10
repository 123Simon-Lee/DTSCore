// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UDSTCoreTimeLibrary.generated.h"

/**
 * 
 */
UCLASS()
class DTSCORE_API UUDSTCoreTimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	//XX:XX:XX
	UFUNCTION(BlueprintPure, Category = "DSTCore|Time")
	static FString GetTimeString();
	//yyyy-MM-dd
	UFUNCTION(BlueprintPure, Category = "DSTCore|Time")
	static FString GetDateString();
	//获取星期几
	UFUNCTION(BlueprintPure, Category = "DSTCore|Time")
	static FString GetWeekdayString();
};

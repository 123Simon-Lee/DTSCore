// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include"JsEnv.h"
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TwinScriptGameInstance.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCall, FString, FunctionName, UObject*, uobj);
/**
 * 
 */
UCLASS()
class DTSCORE_API UTwinScriptGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	virtual void Init() override;
	virtual void  OnStart() override;
	virtual void Shutdown() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bDebugMode;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bWaitForDebugger;

	
	UPROPERTY(BlueprintAssignable, Category = "Event")
	FCall call;
	UFUNCTION(BlueprintCallable, Category = "TsEvent")
	void CallTs(FString FunctionName, UObject* uobj);

	UFUNCTION(BlueprintCallable, Category = "HostSetting")
	FString GetBackendHost();

private:
	TSharedPtr<puerts::FJsEnv>GameScript;

	// 后端地址存储,默认为本机
	UPROPERTY()
	FString BackendHost = TEXT("127.0.0.1");
};

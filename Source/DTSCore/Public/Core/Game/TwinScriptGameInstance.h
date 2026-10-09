// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "JsEnv.h"
#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "TwinScriptGameInstance.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCall, FString, FunctionName, UObject *, uobj);
/**
 *
 */
UCLASS()
class DTSCORE_API UTwinScriptGameInstance : public UGameInstance
{
  GENERATED_BODY()
public:
  virtual void Init() override;
  virtual void OnStart() override;
  virtual void Shutdown() override;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
  bool bDebugMode;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
  bool bWaitForDebugger;

  UPROPERTY(BlueprintAssignable, Category = "Event")
  FCall call;
  UFUNCTION(BlueprintCallable, Category = "TsEvent")
  void CallTs(FString FunctionName, UObject *uobj);

  /** 获取后端服务器地址。 */
  UFUNCTION(BlueprintPure, Category = "HostSetting")
  FString GetBackendHost() const;

  /** 获取 WebUI 页面地址。 */
  UFUNCTION(BlueprintPure, Category = "HostSetting")
  FString GetWebURL() const;

protected:
  TSharedPtr<puerts::FJsEnv> GameScript;

  /** 后端服务器 IP 或域名。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HostSetting")
  FString BackendHost = TEXT("127.0.0.1");

  /** WebUI 完整访问地址。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HostSetting")
  FString WebURL = TEXT("http://127.0.0.1:8087");
};

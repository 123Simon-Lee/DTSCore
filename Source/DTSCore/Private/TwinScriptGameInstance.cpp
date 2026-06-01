// Fill out your copyright notice in the Description page of Project Settings.


#include "TwinScriptGameInstance.h"

void UTwinScriptGameInstance::Init()
{
	Super::Init();
	if (bDebugMode)
	{
		GameScript = MakeShared<puerts::FJsEnv>(
			std::make_unique<puerts::DefaultJSModuleLoader>(TEXT("JavaScript")),
			std::make_shared<puerts::FDefaultLogger>(),
			9000);
		if (bWaitForDebugger)
		{
			GameScript->WaitDebugger();
		}
	}
	else
	{
		GameScript = MakeShared<puerts::FJsEnv>();
	}

	TArray<TPair<FString, UObject*>> Arguments;
	Arguments.Add(TPair<FString, UObject*>(TEXT("GameInstance"), this)); 
	GameScript->Start("MainGame", Arguments);

	// 尝试获取Content/Config/HostSetting.json
	FString HostSettingPath = FPaths::ProjectContentDir() + TEXT("Config/HostSetting.json");
	if (FPaths::FileExists(HostSettingPath))
	{
		// 读取内容Json
		FString Content;
		FFileHelper::LoadFileToString(Content,*HostSettingPath);
		
		// 创建一个Json对象
		TSharedPtr<FJsonObject> JsonObject;
		// 创建一个Json 阅读器
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Content);
		// 尝试解析Json
		if (FJsonSerializer::Deserialize(JsonReader,JsonObject))
		{
			// 获取BackendHost字段
			if (JsonObject->HasField(TEXT("BackendHost")))
			{
				this->BackendHost = JsonObject->GetStringField(TEXT("BackendHost"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("BackendHost not found"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("HostSetting.json not found"));
	}

}

void UTwinScriptGameInstance::OnStart()
{
	Super::OnStart();
}

void UTwinScriptGameInstance::Shutdown()
{
	Super::Shutdown();
	GameScript.Reset();
}

void UTwinScriptGameInstance::CallTs(FString FunctionName, UObject* uobj)
{

}

FString UTwinScriptGameInstance::GetBackendHost()
{
	return this->BackendHost;
}

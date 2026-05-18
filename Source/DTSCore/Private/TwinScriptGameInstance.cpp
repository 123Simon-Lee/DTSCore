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

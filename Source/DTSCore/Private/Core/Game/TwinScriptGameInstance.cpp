// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/Game/TwinScriptGameInstance.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
  FString GetSoftwareConfigFilePath()
  {
    return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Softwareconfig.ini"));
  }

  bool TryReadBackendHostFromSoftwareConfig(FString& OutBackendHost)
  {
    const FString ConfigPath = GetSoftwareConfigFilePath();
    if (!FPaths::FileExists(ConfigPath))
    {
      return false;
    }

    FConfigFile ConfigFile;
    ConfigFile.Read(ConfigPath);
    return ConfigFile.GetString(TEXT("HostSetting"), TEXT("BackendHost"), OutBackendHost);
  }

  void EnsureBackendHostEntryInSoftwareConfig(const FString& BackendHost)
  {
    const FString ConfigPath = GetSoftwareConfigFilePath();
    if (ConfigPath.IsEmpty())
    {
      return;
    }

    FString ExistingContent;
    if (FPaths::FileExists(ConfigPath))
    {
      FFileHelper::LoadFileToString(ExistingContent, *ConfigPath);
    }

    if (ExistingContent.IsEmpty())
    {
      FString NewContent = TEXT("; 后端地址配置\r\n[HostSetting]\r\nBackendHost=");
      NewContent += BackendHost;
      NewContent += TEXT("\r\n");
      FFileHelper::SaveStringToFile(NewContent, *ConfigPath);
      return;
    }

    const FString SectionHeader = TEXT("[HostSetting]");
    const FString KeyLine = FString::Printf(TEXT("BackendHost=%s"), *BackendHost);
    TArray<FString> Lines;
    ExistingContent.ParseIntoArrayLines(Lines);

    bool bSeenHostSettingSection = false;
    bool bFoundKey = false;

    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
      FString& Line = Lines[Index];
      const FString TrimmedLine = Line.TrimStartAndEnd();

      if (TrimmedLine.StartsWith(TEXT("[")) && TrimmedLine.EndsWith(TEXT("]")))
      {
        if (bSeenHostSettingSection)
        {
          if (!bFoundKey)
          {
            Lines.Insert(KeyLine, Index);
            bFoundKey = true;
          }
          break;
        }

        bSeenHostSettingSection = TrimmedLine.Equals(SectionHeader, ESearchCase::IgnoreCase);
        continue;
      }

      if (!bSeenHostSettingSection)
      {
        continue;
      }

      if (TrimmedLine.StartsWith(TEXT("BackendHost"), ESearchCase::IgnoreCase))
      {
        Line = KeyLine;
        bFoundKey = true;
        break;
      }
    }

    if (!bFoundKey)
    {
      if (!bSeenHostSettingSection)
      {
        Lines.Add(TEXT(""));
        Lines.Add(TEXT("; 后端地址配置"));
        Lines.Add(TEXT("[HostSetting]"));
      }
      Lines.Add(KeyLine);
    }

    FString UpdatedContent;
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
      if (Index > 0)
      {
        UpdatedContent += TEXT("\r\n");
      }
      UpdatedContent += Lines[Index];
    }

    FFileHelper::SaveStringToFile(UpdatedContent, *ConfigPath);
  }
}

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

  FString LoadedBackendHost = this->BackendHost;
  if (TryReadBackendHostFromSoftwareConfig(LoadedBackendHost))
  {
    this->BackendHost = LoadedBackendHost.TrimStartAndEnd();
  }
  else
  {
    // 兼容旧的 HostSetting.json 配置，若 ini 里没有值则回退读取。
    FString HostSettingPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Config/HostSetting.json"));
    if (FPaths::FileExists(HostSettingPath))
    {
      FString Content;
      FFileHelper::LoadFileToString(Content, *HostSettingPath);

      TSharedPtr<FJsonObject> JsonObject;
      TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Content);
      if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
      {
        if (JsonObject->HasField(TEXT("BackendHost")))
        {
          this->BackendHost = JsonObject->GetStringField(TEXT("BackendHost")).TrimStartAndEnd();
        }
        else
        {
          UE_LOG(LogTemp, Error, TEXT("BackendHost not found"));
        }
      }
    }
    else
    {
      UE_LOG(LogTemp, Warning, TEXT("HostSetting.json not found, will use default backend host"));
    }

    EnsureBackendHostEntryInSoftwareConfig(this->BackendHost);
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

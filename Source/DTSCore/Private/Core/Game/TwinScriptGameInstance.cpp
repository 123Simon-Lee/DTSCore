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

  /** 从 Softwareconfig.ini 的 HostSetting 节读取字符串。 */
  bool TryReadHostSetting(
      const TCHAR *Key,
      FString &OutValue)
  {
    const FString ConfigPath = GetSoftwareConfigFilePath();

    if (!FPaths::FileExists(ConfigPath))
    {
      return false;
    }

    FConfigFile ConfigFile;
    ConfigFile.Read(ConfigPath);

    FString Value;

    if (!ConfigFile.GetString(
            TEXT("HostSetting"),
            Key,
            Value))
    {
      return false;
    }

    Value.TrimStartAndEndInline();

    if (Value.IsEmpty())
    {
      return false;
    }

    OutValue = MoveTemp(Value);
    return true;
  }

  void EnsureBackendHostEntryInSoftwareConfig(const FString &BackendHost)
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
      FString &Line = Lines[Index];
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

  /*
   * 先读取配置，再启动 Puerts。
   * 这样 TS 和 Widget 第一次访问时就能拿到正确地址。
   */
  FString LoadedBackendHost;

  if (TryReadHostSetting(
          TEXT("BackendHost"),
          LoadedBackendHost))
  {
    BackendHost = LoadedBackendHost;
  }
  else
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("BackendHost not found, use default: %s"),
        *BackendHost);

    EnsureBackendHostEntryInSoftwareConfig(BackendHost);
  }

  FString LoadedWebURL;

  if (TryReadHostSetting(TEXT("WebURL"), LoadedWebURL))
  {
    WebURL = LoadedWebURL;
  }
  else
  {
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("WebURL not found, use default: %s"),
        *WebURL);
  }

  UE_LOG(
      LogTemp,
      Log,
      TEXT("BackendHost: %s"),
      *BackendHost);

  UE_LOG(
      LogTemp,
      Log,
      TEXT("WebURL: %s"),
      *WebURL);

  if (bDebugMode)
  {
    GameScript = MakeShared<puerts::FJsEnv>(
        std::make_unique<puerts::DefaultJSModuleLoader>(
            TEXT("JavaScript")),
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

  TArray<TPair<FString, UObject *>> Arguments;
  Arguments.Add(
      TPair<FString, UObject *>(TEXT("GameInstance"), this));

  GameScript->Start(TEXT("MainGame"), Arguments);
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

void UTwinScriptGameInstance::CallTs(FString FunctionName, UObject *uobj)
{
}

FString UTwinScriptGameInstance::GetWebURL() const
{
  return WebURL;
}

FString UTwinScriptGameInstance::GetBackendHost() const
{
  return BackendHost;
}

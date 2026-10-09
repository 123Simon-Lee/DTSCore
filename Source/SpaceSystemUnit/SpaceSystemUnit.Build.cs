using UnrealBuildTool;

public class SpaceSystemUnit : ModuleRules
{
  public SpaceSystemUnit(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

    PublicDependencyModuleNames.AddRange(
        new string[]
        {
                "Core", "Engine",
        }
    );

    PrivateDependencyModuleNames.AddRange(
        new string[]
        {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore"
        }
    );
    if (Target.Type == TargetType.Editor)
    {
      // 行结构体 OnDataTableChanged 中自动同步行名(FDataTableEditorUtils)需要 UnrealEd，仅编辑器目标链接
      PrivateDependencyModuleNames.Add("UnrealEd");
    }
  }
}
using UnrealBuildTool;

public class PassageSystem : ModuleRules
{
  public PassageSystem(ReadOnlyTargetRules Target)
      : base(Target)
  {
    PCHUsage =
        PCHUsageMode.UseExplicitOrSharedPCHs;

    PublicDependencyModuleNames.AddRange(
        new[]
        {
                "Core",
                "CoreUObject",
                "Engine"
        });
  }
}
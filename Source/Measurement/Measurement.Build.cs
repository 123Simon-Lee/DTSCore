// Measurement 模块构建规则：测距/测面积/剖切工具
using UnrealBuildTool;

public class Measurement : ModuleRules
{
  public Measurement(ReadOnlyTargetRules Target) : base(Target)
  {
    PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

     PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "SpaceSystemUnit",
            "ProceduralMeshComponent"
        });

    
  }
}

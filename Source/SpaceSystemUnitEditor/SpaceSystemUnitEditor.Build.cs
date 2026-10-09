using UnrealBuildTool;

public class SpaceSystemUnitEditor : ModuleRules
{
    public SpaceSystemUnitEditor(ReadOnlyTargetRules Target)
        : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",

                // 使用 ASpace 和空间数据表结构。
                "SpaceSystemUnit",

                // UActorActionUtility 的模块。
                // 因为父类出现在公共头文件中，所以放 Public。
                "Blutility"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "UnrealEd",
                "Slate",
                "SlateCore"
            }
        );


    }
}
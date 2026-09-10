// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DTSCore : ModuleRules
{
	public DTSCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// 相机运行时配置必须以原始文本方式随包发布，才能在程序运行期间修改并保留文件里的注释。
		RuntimeDependencies.Add(
            "$(ProjectDir)/Content/Softwareconfig.ini",
			StagedFileType.NonUFS);
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "JsEnv",
				"Puerts",
                "EnhancedInput",
				"InputCore",
                "UMG",
                "Json",          
                "JsonUtilities"
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"SpaceSystemUnit",
				"Slate",
				"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}

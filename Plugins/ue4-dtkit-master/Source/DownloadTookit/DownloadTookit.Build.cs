// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
public class DownloadTookit : ModuleRules
{
	public DownloadTookit(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
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
				"HTTP",
				// UE 5.7: SSL module no longer needed — MD5 uses engine's built-in FMD5 (Misc/SecureHash.h)
				// ... add other public dependencies that you statically link with here ...
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

		PublicDefinitions.AddRange(new string[]{
			"WITH_LOG=0",
			// UE 5.7: GetContent() already returns const TArray<uint8>&, no hack needed
			"HACK_HTTP_LOG_GETCONTENT_WARNING=0"
		});
	
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
	}
}

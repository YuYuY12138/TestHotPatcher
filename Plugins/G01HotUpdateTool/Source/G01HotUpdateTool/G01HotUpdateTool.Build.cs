using UnrealBuildTool;

public class G01HotUpdateTool : ModuleRules
{
    public G01HotUpdateTool(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // HotPatcher
            "HotPatcherRuntime",
            "HotPatcherCore",

            // JSON
            "Json",
            "JsonUtilities",

            // Slate UI (Editor panel)
            "Slate",
            "SlateCore",
            "InputCore",
            "EditorStyle",
            "UnrealEd",
            "WorkspaceMenuStructure",
            "ApplicationCore",
        });
    }
}

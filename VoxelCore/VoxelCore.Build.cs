using System.IO;
using UnrealBuildTool;


public class VoxelCore : ModuleRules
{
    public VoxelCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] {Path.Combine(ModuleDirectory, "Public")});

        PrivateIncludePaths.AddRange(
        new string[] {Path.Combine(ModuleDirectory, "Private"),Path.Combine(ModuleDirectory, "ThirdParty", "FastNoiseLite", "include")});

        PublicDependencyModuleNames.AddRange(new string[]{"Core", "CoreUObject", "Engine", "InputCore"});

        PrivateDependencyModuleNames.AddRange(new string[] { });

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });

        PublicDependencyModuleNames.AddRange(new string[]
{
        "Core", "CoreUObject", "Engine", "InputCore", "NetCore", "ProceduralMeshComponent",
        "Json", "JsonUtilities"
        });

        // FastNoiseLite is header-only; no additional linking required.
    }
}
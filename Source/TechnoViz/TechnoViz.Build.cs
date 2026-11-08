using System.IO;
using UnrealBuildTool;

public class TechnoViz : ModuleRules
{
	public TechnoViz(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "Audio"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "Niagara", "Slate", "SlateCore"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// WASAPI capture (loopback + input) and MMCSS "Pro Audio" thread priority.
			PublicSystemLibraries.AddRange(new string[] { "ole32.lib", "avrt.lib", "propsys.lib" });
		}
	}
}

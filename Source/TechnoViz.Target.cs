using UnrealBuildTool;

public class TechnoVizTarget : TargetRules
{
	public TechnoVizTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TechnoViz");
	}
}

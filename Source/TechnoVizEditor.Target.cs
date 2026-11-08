using UnrealBuildTool;

public class TechnoVizEditorTarget : TargetRules
{
	public TechnoVizEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("TechnoViz");
	}
}

using UnrealBuildTool;

public class HitregLabEditorTarget : TargetRules
{
	public HitregLabEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("HitregLab");
		ExtraModuleNames.Add("HitregLabEditor");
	}
}

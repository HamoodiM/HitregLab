using UnrealBuildTool;

public class HitregLabEditor : ModuleRules
{
	public HitregLabEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] {
			"HitregLab", "Slate", "SlateCore", "InputCore", "UnrealEd", "ToolMenus", "PropertyEditor", "WorkspaceMenuStructure"
		});
	}
}

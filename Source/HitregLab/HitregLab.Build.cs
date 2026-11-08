using UnrealBuildTool;

public class HitregLab : ModuleRules
{
	public HitregLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NetCore", "AIModule", "Slate", "SlateCore", "UMG"
		});
	}
}

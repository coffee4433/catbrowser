using UnrealBuildTool;

public class FortnitePortingRuntime : ModuleRules
{
	public FortnitePortingRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			[
				"Core", "CoreUObject", "Engine", "InputCore", "AssetRegistry", "Slate", "SlateCore"
			]
		);
	}
}

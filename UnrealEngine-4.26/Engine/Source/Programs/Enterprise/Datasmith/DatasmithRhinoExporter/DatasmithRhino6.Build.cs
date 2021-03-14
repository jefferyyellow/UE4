// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public abstract class DatasmithRhinoBase : ModuleRules
	{
		public DatasmithRhinoBase(ReadOnlyTargetRules Target)
			: base(Target)
		{
			bUseRTTI = true;
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"DatasmithFacadeCSharp",
				}
			);

			PrivateIncludePaths.AddRange(
				new string[]
				{
					ModuleDirectory
				}
			);
		}

		public abstract string GetRhinoVersion();
	}

	[SupportedPlatforms("Win64")]
	public class DatasmithRhino6 : DatasmithRhinoBase
	{
		public DatasmithRhino6(ReadOnlyTargetRules Target)
			: base(Target)
		{
		}

		public override string GetRhinoVersion()
		{
			return "6";
		}
	}
}

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginSequencerTarget : TargetRules
{
    public EnginePluginSequencerTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEnginePluginSequencer";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        TargetDependencies.Add("SequencerPlugin");

        ExtraModules.Add("EnginePluginSequencerModule");
    }
}

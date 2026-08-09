using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class SequencerPluginTarget : TargetRules
{
    public SequencerPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plSequencerPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        ExtraModules.Add("SequencerPluginModule");
    }
}

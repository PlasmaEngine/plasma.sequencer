using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginSequencerTarget : TargetRules
{
    public EditorPluginSequencerTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plEditorPluginSequencer";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);

        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;

        TargetDependencies.Add("SequencerPlugin");

        ExtraModules.Add("EditorPluginSequencerModule");
    }
}

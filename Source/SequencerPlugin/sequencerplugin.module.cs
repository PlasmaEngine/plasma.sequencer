using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class SequencerPluginModule : ModuleRules
{
    public SequencerPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/SequencerPlugin", "SequencerPluginPCH.h",
            "BUILDSYSTEM_BUILDING_SEQUENCERPLUGIN_LIB");

        // Media Foundation is the default video encoder for rendering a sequence to disk. These are
        // OS components, so nothing is redistributed - which is exactly why it is the default over
        // a vendored encoder (Documentation/Sequencer/Plan.html §14).
        ConfigureForPlatform(TargetPlatform.Windows, () =>
        {
            PublicSystemLibraries.Add("mfplat");
            PublicSystemLibraries.Add("mfreadwrite");
            PublicSystemLibraries.Add("mfuuid");
        });
    }
}

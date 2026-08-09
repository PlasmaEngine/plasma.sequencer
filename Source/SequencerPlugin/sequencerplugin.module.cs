using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class SequencerPluginModule : ModuleRules
{
    public SequencerPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/SequencerPlugin", "SequencerPluginPCH.h",
            "BUILDSYSTEM_BUILDING_SEQUENCERPLUGIN_LIB");
    }
}

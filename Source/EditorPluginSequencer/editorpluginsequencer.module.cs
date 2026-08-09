using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginSequencerModule : ModuleRules
{
    public EditorPluginSequencerModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginSequencer",
            "EditorPluginSequencerPCH.h", "BUILDSYSTEM_BUILDING_EDITORPLUGINSEQUENCER_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/SequencerPlugin", "plSequencerPlugin");

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginSequencerModule",
            "Source/EditorPluginSequencer", "Core", "Gui", "Widgets");
    }
}

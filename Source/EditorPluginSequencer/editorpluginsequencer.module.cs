using System.IO;
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

        // plColorGradientAssetData (the color-track payload the gradient editor binds to) lives in
        // plEditorPluginAssets, whose headers sit under EditorPlugins/Assets - not on the default
        // SDK include path, same situation as EnginePluginScene for the engine plugin.
        PrivateIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Code", "EditorPlugins", "Assets"));
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEditorPluginAssets");

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginSequencerModule",
            "Source/EditorPluginSequencer", "Core", "Gui", "Widgets");
    }
}

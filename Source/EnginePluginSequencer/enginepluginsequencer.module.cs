using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EnginePluginSequencerModule : ModuleRules
{
    public EnginePluginSequencerModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EnginePluginSequencer",
            "EnginePluginSequencerPCH.h", "BUILDSYSTEM_BUILDING_ENGINEPLUGINSEQUENCER_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);
        PlasmaPackageSdk.AddPackagePluginImport(this, context, "Source/SequencerPlugin", "plSequencerPlugin");

        // plSequenceContext derives from plSceneContext: Sequence documents are game-object context
        // documents, and the scene context is what serves those in the engine process (it registers
        // for "Scene;Prefab;PropertyAnim" itself). Its headers live under EditorPlugins/Scene, which
        // is not on the default SDK include path, and its messages come from SharedPluginScene.
        PrivateIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Code", "EditorPlugins", "Scene"));
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEnginePluginScene");
        PlasmaPackageSdk.AddImportLibrary(this, context, "plSharedPluginScene");
    }
}

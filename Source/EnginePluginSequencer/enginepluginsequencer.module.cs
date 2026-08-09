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

        // plSequenceContext derives directly from plEngineProcessDocumentContext (like plLayerContext
        // and water's profile context). It must NOT derive from plSceneContext: the context factory
        // resolves "DocumentType" via FindPropertyByName, which walks the hierarchy, so a derived
        // redefinition trips the RTTI correctness assert.
    }
}

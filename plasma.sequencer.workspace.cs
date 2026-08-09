// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaSequencerWorkspace : WorkspaceRules
{
    public PlasmaSequencerWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("SequencerPlugin");
        TargetNames.Add("EnginePluginSequencer");
        TargetNames.Add("EditorPluginSequencer");
    }
}

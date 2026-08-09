#include <SequencerPlugin/SequencerPluginPCH.h>

#include <Core/ResourceManager/ResourceManager.h>
#include <Foundation/Configuration/Startup.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

// clang-format off
PL_BEGIN_SUBSYSTEM_DECLARATION(GameEngine, SequencerPlugin)

  BEGIN_SUBSYSTEM_DEPENDENCIES
    "Foundation",
    "Core"
  END_SUBSYSTEM_DEPENDENCIES

  ON_CORESYSTEMS_STARTUP
  {
    plResourceManager::RegisterResourceForAssetType("Sequence", plGetStaticRTTI<plSequenceResource>());

    plSequenceResourceDescriptor descriptor;
    plSequenceResourceHandle hFallback = plResourceManager::CreateResource<plSequenceResource>(
      "SequenceMissing", std::move(descriptor), "Fallback for missing sequences");
    plResourceManager::SetResourceTypeMissingFallback<plSequenceResource>(hFallback);
  }

  ON_CORESYSTEMS_SHUTDOWN
  {
    plResourceManager::SetResourceTypeMissingFallback<plSequenceResource>(plSequenceResourceHandle());
    plSequenceResource::CleanupDynamicPluginReferences();
  }

  ON_HIGHLEVELSYSTEMS_STARTUP
  {
  }

  ON_HIGHLEVELSYSTEMS_SHUTDOWN
  {
  }

PL_END_SUBSYSTEM_DECLARATION;
// clang-format on

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Startup);

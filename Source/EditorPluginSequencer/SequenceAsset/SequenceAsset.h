#pragma once

#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorFramework/Document/GameObjectContextDocument.h>
#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

/// \brief The reflected, undo-mirrored data a Sequence asset stores.
///
/// P0 carries only the timeline framing; bindings and typed tracks land with P1
/// (Documentation/Sequencer/Plan.html §4). Storage is ticks at plSequenceTicksPerSecond;
/// the FPS is display/snapping only.
class plSequenceData : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceData, plReflectedClass);

public:
  plUInt16 m_uiFramesPerSecond = 30;
  plUInt64 m_uiDurationTicks = 5 * plSequenceTicksPerSecond;
};

/// \brief Document for the Sequence asset type.
///
/// A game-object context document, like PropertyAnim: the sequence attaches to a scene document as
/// its editing context, so its bindings resolve against and preview into your actual level.
class plSequenceAssetDocument : public plSimpleAssetDocument<plSequenceData, plGameObjectContextDocument>
{
  using BaseClass = plSimpleAssetDocument<plSequenceData, plGameObjectContextDocument>;
  PL_ADD_DYNAMIC_REFLECTION(plSequenceAssetDocument, BaseClass);

public:
  plSequenceAssetDocument(plStringView sDocumentPath);

  plTime GetDuration() const;

  virtual plManipulatorSearchStrategy GetManipulatorSearchStrategy() const override
  {
    return plManipulatorSearchStrategy::ChildrenOfSelectedObject;
  }

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
};

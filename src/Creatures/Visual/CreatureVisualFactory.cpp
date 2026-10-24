#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Creatures/Visual/CreatureVisualBoneSkeleton.h"
#include "Creatures/Visual/CreatureVisualGltf.h"
#include "Creatures/Visual/CreatureVisualRigid.h"
#include "Creatures/Visual/CreatureVisualSprite.h"
#include <memory>

namespace cutum
{

namespace
{

std::unique_ptr<IUCreatureVisual>
CreateForBackend(CreatureVisualBackend backend)
{
  switch (backend)
  {
  case CreatureVisualBackend::BoneSkeleton:
    return CreateCreatureVisualBoneSkeleton();
  case CreatureVisualBackend::GltfSkeleton:
    return CreateCreatureVisualGltf();
  default:
    return std::make_unique<UCreatureVisualRigid>();
  }
}

} // namespace

std::unique_ptr<IUCreatureVisual>
CreateCreatureVisual(const CreatureDefinition &def)
{
  if (CreatureUsesBillboardSprite(def))
  {
    return CreateCreatureVisualSprite();
  }
  return CreateForBackend(ResolveCreatureVisualBackend(def));
}

} // namespace cutum

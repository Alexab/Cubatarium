#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualGltf.h"
#include "Creatures/Visual/CreatureVisualRigid.h"
#include "Creatures/Visual/CreatureVisualSkeletalGeo.h"
#include <memory>

namespace cutum
{

std::unique_ptr<ICreatureVisual>
CreateCreatureVisual(const CreatureDefinition &def)
{
  switch (ParseCreatureVisualBackend(def.visual.backend))
  {
  case CreatureVisualBackend::SkeletalGeo:
    return CreateCreatureVisualSkeletalGeo();
  case CreatureVisualBackend::GltfSkeleton:
    return CreateCreatureVisualGltf();
  default:
    return std::make_unique<UCreatureVisualRigid>();
  }
}

} // namespace cutum

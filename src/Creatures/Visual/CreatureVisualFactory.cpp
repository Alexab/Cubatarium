#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualSkeletalGeo.h"
#include "Creatures/Visual/CreatureVisualGltf.h"
#include "Creatures/Visual/CreatureVisualRigid.h"
#include <iostream>
#include <memory>
#include <unordered_set>

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
  {
    static std::unordered_set<std::string> logged;
    if (logged.insert(def.Id).second)
    {
      std::cerr << "UCreatureVisual: gltf_skeleton stub for " << def.Id
                << std::endl;
    }
    return std::make_unique<UCreatureVisualGltf>();
  }
  default:
    return std::make_unique<UCreatureVisualRigid>();
  }
}

} // namespace cutum

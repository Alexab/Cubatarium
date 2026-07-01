#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Definition/CreatureDefinition.h"
#include "Creatures/Visual/CreatureVisual.h"
#include "Creatures/Visual/CreatureVisualGltf.h"
#include "Creatures/Visual/CreatureVisualRigid.h"
#include "Creatures/Visual/CreatureVisualBoneSkeleton.h"
#include <memory>

namespace cutum
{

namespace
{

bool BackendAssetsAvailable(const CreatureDefinition &def,
                            CreatureVisualBackend backend)
{
  switch (backend)
  {
  case CreatureVisualBackend::GltfSkeleton:
    return !def.visual.gltf.modelPath.empty();
  case CreatureVisualBackend::BoneSkeleton:
    return !def.visual.boneSkeleton.geometryId.empty();
  default:
    return true;
  }
}

std::unique_ptr<IUCreatureVisual> CreateForBackend(CreatureVisualBackend backend)
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
  CreatureVisualBackend backend =
      ParseCreatureVisualBackend(def.visual.backend);
  if (!BackendAssetsAvailable(def, backend) &&
      !def.visual.fallbackBackend.empty())
  {
    backend = ParseCreatureVisualBackend(def.visual.fallbackBackend);
  }
  return CreateForBackend(backend);
}

} // namespace cutum

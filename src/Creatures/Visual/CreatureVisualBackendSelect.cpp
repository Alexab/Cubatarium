#include "Creatures/Visual/CreatureVisualBackendSelect.h"
#include "Creatures/Definition/CreatureDefinition.h"

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

} // namespace

bool CreatureUsesBillboardSprite(const CreatureDefinition &def)
{
  return def.visual.sprite.billboard;
}

CreatureVisualBackend
ResolveCreatureVisualBackend(const CreatureDefinition &def)
{
  CreatureVisualBackend backend =
      ParseCreatureVisualBackend(def.visual.backend);
  if (!BackendAssetsAvailable(def, backend) &&
      !def.visual.fallbackBackend.empty())
  {
    backend = ParseCreatureVisualBackend(def.visual.fallbackBackend);
  }
  return backend;
}

} // namespace cutum

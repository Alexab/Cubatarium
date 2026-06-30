#ifndef CREATUREGLTFASSET_H
#define CREATUREGLTFASSET_H

#include "Creatures/Core/CreatureCatalogTypes.h"
#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <memory>
#include <string>

namespace cutum
{

struct CreatureGltfAsset
{
  CreatureGltfSpec Spec;
  std::shared_ptr<CreatureGltfMeshAsset> Mesh;
  std::string SpeciesId;
};

} // namespace cutum

#endif

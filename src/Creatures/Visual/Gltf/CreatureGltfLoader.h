#ifndef CREATUREGLTFLOADER_H
#define CREATUREGLTFLOADER_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <memory>
#include <string>

namespace cutum
{

class CreatureGltfLoader
{
public:
  static std::shared_ptr<CreatureGltfMeshAsset>
  LoadFromFile(const std::string &gltfPath);
};

} // namespace cutum

#endif

#ifndef CREATUREGLTFBONEPALETTE_H
#define CREATUREGLTFBONEPALETTE_H

#include "Creatures/Visual/Gltf/CreatureGltfTypes.h"
#include <glm/glm.hpp>
#include <vector>

namespace cutum
{

std::vector<glm::mat4>
ComputeGltfSkinMatrices(const CreatureGltfMeshAsset &asset,
                        const GltfAnimationCpu *animation, float timeSec,
                        bool loop);

} // namespace cutum

#endif

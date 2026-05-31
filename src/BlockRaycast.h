#ifndef BLOCKRAYCAST_H
#define BLOCKRAYCAST_H

#include <glm/glm.hpp>
#include <optional>

namespace cutum {

class BlockWorld;
class BlockRegistry;

struct BlockRayHit {
 glm::ivec3 blockPos;
 glm::ivec3 faceNormal;
 float distance;
};

std::optional<BlockRayHit> RaycastSolidBlocks(
    const BlockWorld& world,
    const BlockRegistry& registry,
    glm::vec3 origin,
    glm::vec3 direction,
    float maxDistance = 128.0f);

}

#endif

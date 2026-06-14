#ifndef BLOCKRAYCAST_H
#define BLOCKRAYCAST_H

#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;

struct BlockRayHit
{
  glm::ivec3 blockPos;
  glm::ivec3 faceNormal;
  float distance;
};

std::optional<BlockRayHit> RaycastSolidBlocks(const UBlockWorld &world,
                                              const UBlockRegistry &registry,
                                              glm::vec3 origin,
                                              glm::vec3 direction,
                                              float maxDistance = 128.0f);

} // namespace cutum

#endif

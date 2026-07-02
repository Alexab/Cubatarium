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

struct FluidPlacementHit
{
  glm::ivec3 block_pos;
  glm::ivec3 face_normal;
  bool via_fluid_volume{false};
};

std::optional<BlockRayHit> RaycastSolidBlocks(const UBlockWorld &world,
                                              const UBlockRegistry &registry,
                                              glm::vec3 origin,
                                              glm::vec3 direction,
                                              float maxDistance = 128.0f);

std::optional<FluidPlacementHit> RaycastFluidPlacementTarget(
    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,
    glm::vec3 front, float max_distance = 128.0f);

} // namespace cutum

#endif

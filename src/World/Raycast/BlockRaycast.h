#ifndef BLOCKRAYCAST_H
#define BLOCKRAYCAST_H

#include <cstdint>
#include "World/Math/BlockTypes.h"
#include <glm/glm.hpp>
#include <optional>

namespace cutum
{

class UBlockWorld;
class UBlockRegistry;
class UWorld;

struct BlockRayHit
{
  glm::ivec3 blockPos;
  glm::ivec3 faceNormal;
  float distance;
};

struct OpaqueVoxelRayWitness
{
  // 1=opaque cube, 2=unloaded chunk before an opaque cube, 3=none in range.
  uint8_t state{0};
  uint8_t entry_face{0xffu};
  glm::ivec3 block{0};
  glm::ivec3 unloaded_cell{0};
  glm::ivec3 previous_block{0};
  BlockId block_id{BLOCK_AIR};
  BlockId previous_block_id{BLOCK_AIR};
  float distance{-1.0f};
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

/// Trace the first opaque cube using the renderer's voxel visibility rules.
OpaqueVoxelRayWitness TraceOpaqueVoxelRay(const UWorld &world,
                                         const glm::vec3 &origin,
                                         const glm::vec3 &direction,
                                         float max_distance);

glm::ivec3 InferPlacementNormal(const BlockRayHit &hit, glm::vec3 eye_pos);

// Future: bucket pour / fluid tool placement — NOT used by hotbar AddObjectByView.
std::optional<glm::ivec3> RaycastAirPocketAlongRay(
    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,
    glm::vec3 front, const BlockRayHit &hit, float max_distance = 128.0f);

// Future: bucket pour / fluid tool placement — NOT used by hotbar AddObjectByView.
std::optional<FluidPlacementHit> RaycastFluidPlacementTarget(
    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,
    glm::vec3 front, float max_distance = 128.0f);

} // namespace cutum

#endif

#include "World/Raycast/BlockRaycast.h"
#include <array>
#include <cmath>
#include <limits>

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/BlockQuery.h"
#include "World/Collision/VoxelDdaTraversal.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

namespace cutum
{

namespace
{

constexpr float kHalfBlock = 0.5f;

float NextBoundaryT(const glm::vec3 &origin, const glm::vec3 &direction,
                    int blockCoord, int axis)
{
  if (direction[axis] > 0.0f)
  {
    return (static_cast<float>(blockCoord) + kHalfBlock - origin[axis]) /
           direction[axis];
  }
  if (direction[axis] < 0.0f)
  {
    return (static_cast<float>(blockCoord) - kHalfBlock - origin[axis]) /
           direction[axis];
  }
  return std::numeric_limits<float>::max();
}

bool IsRaycastTarget(const UBlockWorld &world, const UBlockRegistry &registry,
                     glm::ivec3 pos)
{
  // Phase 4: Unloaded is not a place/break target (and not AIR).
  const BlockQueryResult q = world.QueryBlock(pos);
  if (q.IsUnloaded() || q.IsAir())
  {
    return false;
  }
  return registry.BlocksMovement(q.id);
}

bool IsAirPocketCell(const UBlockWorld &world, const UBlockRegistry &registry,
                     glm::ivec3 cell)
{
  if (!world.IsAir(cell))
  {
    return false;
  }
  static constexpr std::array<glm::ivec3, 6> kNeighbors = {
      glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
      glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};
  for (const glm::ivec3 &offset : kNeighbors)
  {
    if (IsRaycastTarget(world, registry, cell + offset))
    {
      return true;
    }
  }
  return false;
}

glm::ivec3 InferFaceNormal(const UBlockWorld &world,
                           const UBlockRegistry &registry, glm::ivec3 air_cell,
                           glm::ivec3 solid_hint)
{
  const glm::ivec3 delta = air_cell - solid_hint;
  if (delta != glm::ivec3(0))
  {
    return delta;
  }
  static constexpr std::array<glm::ivec3, 6> kNeighbors = {
      glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
      glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};
  for (const glm::ivec3 &offset : kNeighbors)
  {
    if (IsRaycastTarget(world, registry, air_cell + offset))
    {
      return -offset;
    }
  }
  return glm::ivec3(0, -1, 0);
}

} // namespace

std::optional<BlockRayHit>
RaycastSolidBlocks(const UBlockWorld &world, const UBlockRegistry &registry,
                   glm::vec3 origin, glm::vec3 direction, float maxDistance)
{
  const float len = glm::length(direction);
  if (len < 1e-6f)
  {
    return std::nullopt;
  }
  direction /= len;

  const float eps = 1e-4f;
  glm::ivec3 current = WorldPosToBlock(origin);

  if (IsRaycastTarget(world, registry, current))
  {
    BlockRayHit hit;
    hit.blockPos = current;
    hit.faceNormal =
        glm::ivec3(direction.x > 0.0f ? -1 : (direction.x < 0.0f ? 1 : 0),
                   direction.y > 0.0f ? -1 : (direction.y < 0.0f ? 1 : 0),
                   direction.z > 0.0f ? -1 : (direction.z < 0.0f ? 1 : 0));
    hit.distance = 0.0f;
    return hit;
  }

  float t = 0.0f;
  while (t < maxDistance)
  {
    float tNext = maxDistance;
    int stepAxis = -1;
    const float tx = NextBoundaryT(origin, direction, current.x, 0);
    const float ty = NextBoundaryT(origin, direction, current.y, 1);
    const float tz = NextBoundaryT(origin, direction, current.z, 2);

    if (tx < tNext)
    {
      tNext = tx;
      stepAxis = 0;
    }
    if (ty < tNext)
    {
      tNext = ty;
      stepAxis = 1;
    }
    if (tz < tNext)
    {
      tNext = tz;
      stepAxis = 2;
    }

    if (tNext >= maxDistance)
    {
      break;
    }

    t = tNext + eps;
    glm::ivec3 next = current;
    if (stepAxis == 0)
    {
      next.x += (direction.x > 0.0f) ? 1 : -1;
    }
    else if (stepAxis == 1)
    {
      next.y += (direction.y > 0.0f) ? 1 : -1;
    }
    else if (stepAxis == 2)
    {
      next.z += (direction.z > 0.0f) ? 1 : -1;
    }

    if (IsRaycastTarget(world, registry, next))
    {
      BlockRayHit hit;
      hit.blockPos = next;
      hit.distance = t;
      hit.faceNormal = current - next;
      return hit;
    }

    current = next;
  }

  return std::nullopt;
}

glm::ivec3 InferPlacementNormal(const BlockRayHit &hit, glm::vec3 eye_pos)
{
  if (hit.faceNormal != glm::ivec3(0))
  {
    return hit.faceNormal;
  }
  const glm::vec3 toCamera = eye_pos - BlockCenter(hit.blockPos);
  glm::ivec3 normal(0);
  if (std::abs(toCamera.x) >= std::abs(toCamera.y) &&
      std::abs(toCamera.x) >= std::abs(toCamera.z))
  {
    normal.x = toCamera.x > 0.0f ? 1 : -1;
  }
  else if (std::abs(toCamera.y) >= std::abs(toCamera.z))
  {
    normal.y = toCamera.y > 0.0f ? 1 : -1;
  }
  else
  {
    normal.z = toCamera.z > 0.0f ? 1 : -1;
  }
  return normal;
}

// Future: bucket pour / fluid tool placement — NOT used by hotbar AddObjectByView.
std::optional<glm::ivec3> RaycastAirPocketAlongRay(
    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,
    glm::vec3 front, const BlockRayHit &hit, float max_distance)
{
  const float len = glm::length(front);
  if (len < 1e-6f)
  {
    return std::nullopt;
  }
  const glm::vec3 direction = front / len;

  std::optional<glm::ivec3> best;
  float best_t = std::numeric_limits<float>::max();
  const float traverse_dist = std::max(0.0f, std::min(hit.distance - 0.05f, max_distance));

  TraverseVoxelRay(eye_pos, direction, traverse_dist,
                   [&](glm::ivec3 cell)
                   {
                     if (!IsAirPocketCell(world, registry, cell) ||
                         !world.IsAir(cell))
                     {
                       return false;
                     }
                     const float t = glm::dot(BlockCenter(cell) - eye_pos, direction);
                     if (t >= 0.0f && t < best_t)
                     {
                       best_t = t;
                       best = cell;
                     }
                     return false;
                   });

  return best;
}

// Future: bucket pour / fluid tool placement — NOT used by hotbar AddObjectByView.
std::optional<FluidPlacementHit> RaycastFluidPlacementTarget(
    const UBlockWorld &world, const UBlockRegistry &registry, glm::vec3 eye_pos,
    glm::vec3 front, float max_distance)
{
  const auto hit =
      RaycastSolidBlocks(world, registry, eye_pos, front, max_distance);
  if (!hit)
  {
    return std::nullopt;
  }

  const glm::ivec3 normal = InferPlacementNormal(*hit, eye_pos);
  const glm::ivec3 place_pos = hit->blockPos + normal;
  if (world.IsAir(place_pos))
  {
    FluidPlacementHit placement;
    placement.block_pos = place_pos;
    placement.face_normal = normal;
    placement.via_fluid_volume = false;
    return placement;
  }

  const auto pocket =
      RaycastAirPocketAlongRay(world, registry, eye_pos, front, *hit, max_distance);
  if (!pocket)
  {
    return std::nullopt;
  }

  FluidPlacementHit placement;
  placement.block_pos = *pocket;
  placement.face_normal =
      InferFaceNormal(world, registry, *pocket, hit->blockPos);
  placement.via_fluid_volume = true;
  return placement;
}

} // namespace cutum

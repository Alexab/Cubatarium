#include "World/Raycast/BlockRaycast.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/BlockQuery.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Collision/VoxelDdaTraversal.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/Math/GridMath.h"

namespace cutum
{

namespace
{

constexpr float kHalfBlock = 0.5f;

bool IsKnownAirTerrainSlice(const UWorld &world, glm::ivec3 chunk_coord)
{
  const int max_world_y = world.GetProceduralSettings().MaxHeight;
  const int max_cy = (max_world_y + CHUNK_SIZE - 1) / CHUNK_SIZE;
  if (chunk_coord.y > max_cy)
  {
    return true;
  }

  // A missing slice is air only when terrain generation has completed the
  // whole column and the slice is above its highest resident non-air slice.
  // Incomplete columns retain the conservative unloaded witness.
  const glm::ivec3 ground_coord(chunk_coord.x, 0, chunk_coord.z);
  if (!world.IsTerrainColumnCompleteFast(ground_coord))
  {
    return false;
  }
  const int highest_non_air_cy = GetHighestNonAirChunkSlice(
      world.GetBlockWorld(), ground_coord, max_world_y);
  return chunk_coord.y > highest_non_air_cy;
}

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

OpaqueVoxelRayWitness TraceOpaqueVoxelRay(const UWorld &world,
                                          const glm::vec3 &origin,
                                          const glm::vec3 &direction,
                                          float max_distance)
{
  OpaqueVoxelRayWitness result{};
  if (!std::isfinite(max_distance) || max_distance <= 0.0f ||
      glm::dot(direction, direction) < 0.99f)
  {
    return result;
  }

  const glm::vec3 ray_origin = origin + direction * 1e-4f;
  // World voxel coordinates identify block centers (cells span n±0.5),
  // matching WorldPosToBlock and the mesh vertex convention.
  glm::ivec3 cell = WorldPosToBlock(ray_origin);
  const glm::ivec3 step(direction.x > 0.0f ? 1 : direction.x < 0.0f ? -1 : 0,
                        direction.y > 0.0f ? 1 : direction.y < 0.0f ? -1 : 0,
                        direction.z > 0.0f ? 1 : direction.z < 0.0f ? -1 : 0);
  const float infinity = std::numeric_limits<float>::infinity();
  const auto first_boundary_t = [&](float position, float ray_dir, int voxel,
                                    int axis_step)
  {
    if (axis_step == 0)
    {
      return infinity;
    }
    const float boundary =
        static_cast<float>(voxel) + (axis_step > 0 ? 0.5f : -0.5f);
    return std::max(0.0f, (boundary - position) / ray_dir);
  };
  glm::vec3 next_t(first_boundary_t(ray_origin.x, direction.x, cell.x, step.x),
                   first_boundary_t(ray_origin.y, direction.y, cell.y, step.y),
                   first_boundary_t(ray_origin.z, direction.z, cell.z, step.z));
  const glm::vec3 delta_t(
      step.x == 0 ? infinity : std::abs(1.0f / direction.x),
      step.y == 0 ? infinity : std::abs(1.0f / direction.y),
      step.z == 0 ? infinity : std::abs(1.0f / direction.z));
  const auto &chunks = world.GetBlockWorld().GetChunkManager();
  const auto &registry = world.GetBlockRegistry();
  float entry_distance = 0.0f;
  int entered_axis = -1;
  int entered_step = 0;
  glm::ivec3 last_unloaded_chunk(std::numeric_limits<int>::min());
  bool last_unloaded_chunk_is_known_air = false;
  glm::ivec3 previous_cell = cell;
  BlockId previous_block_id = BLOCK_AIR;
  const int max_steps = std::max(32, static_cast<int>(max_distance * 2.0f) + 16);
  for (int i = 0; i < max_steps && entry_distance <= max_distance; ++i)
  {
    const BlockQueryResult query = chunks.QueryBlock(cell);
    if (query.IsUnloaded())
    {
      const glm::ivec3 unloaded_chunk = UChunkManager::WorldToChunk(cell);
      if (unloaded_chunk != last_unloaded_chunk)
      {
        last_unloaded_chunk = unloaded_chunk;
        last_unloaded_chunk_is_known_air =
            IsKnownAirTerrainSlice(world, unloaded_chunk);
      }
      if (!last_unloaded_chunk_is_known_air)
      {
        result.state = 2;
        result.unloaded_cell = cell;
        result.distance = entry_distance;
        return result;
      }
      ++result.known_air_unloaded_steps;
    }
    if (query.IsSolid() &&
        registry.GetRenderStyle(query.id) == BlockRenderStyle::UCube &&
        !registry.IsTransparent(query.id))
    {
      result.state = 1;
      result.block = cell;
      result.previous_block = previous_cell;
      result.previous_block_id = previous_block_id;
      if (entered_axis >= 0)
      {
        // Keep the witness in GreedyMesher's face order:
        // +Z, +X, -Z, -X, +Y, -Y. The ray enters opposite its travel step.
        result.entry_face = static_cast<uint8_t>(
            entered_axis == 0 ? (entered_step > 0 ? 3 : 1)
            : entered_axis == 1 ? (entered_step > 0 ? 5 : 4)
                                : (entered_step > 0 ? 2 : 0));
      }
      result.block_id = query.id;
      result.distance = entry_distance;
      return result;
    }

    const float next_distance =
        (std::min)(next_t.x, (std::min)(next_t.y, next_t.z));
    entry_distance = next_distance;
    if (!std::isfinite(entry_distance) || entry_distance > max_distance)
    {
      break;
    }
    previous_cell = cell;
    previous_block_id = query.id;
    entered_axis = -1;
    entered_step = 0;
    constexpr float kRayTieEpsilon = 1e-5f;
    int crossed_axes = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
      if (step[axis] == 0 ||
          std::abs(next_t[axis] - next_distance) > kRayTieEpsilon)
      {
        continue;
      }
      cell[axis] += step[axis];
      next_t[axis] += delta_t[axis];
      entered_axis = axis;
      entered_step = step[axis];
      ++crossed_axes;
    }
    if (crossed_axes != 1)
    {
      entered_axis = -1;
    }
  }
  result.state = 3;
  result.distance = max_distance;
  return result;
}

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

#include "World/Raycast/BlockRaycast.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "Blocks/BlockRegistry.h"
#include "World/Chunks/BlockQuery.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"

namespace cutum
{
namespace
{
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
  const std::string &world_folder = world.GetWorldFolderPath();
  if (world_folder.empty())
  {
    return false;
  }
  const UChunkStorageService &storage = world.GetChunkStorage();
  if (storage.IsColumnSavePending(ground_coord))
  {
    return false;
  }
  const int highest_resident_cy = GetHighestNonAirChunkSlice(
      world.GetBlockWorld(), ground_coord, max_world_y);
  const int highest_on_disk_cy =
      storage.GetHighestChunkSliceOnDisk(world_folder, ground_coord);
  return chunk_coord.y > std::max(highest_resident_cy, highest_on_disk_cy);
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

} // namespace cutum

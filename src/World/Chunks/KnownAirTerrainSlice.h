#pragma once
#include "World/Chunks/KnownAirTerrainSlicePolicy.h"
#include "World/Chunks/TerrainColumnUtil.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/World.h"
#include "World/IO/ChunkStorageService.h"
#include "World/Math/GridMath.h"
#include <limits>
#include <optional>

namespace cutum
{
// Shared with opaque ray tracing: missing resident data is not automatically
// air. Completed columns can prove empty slices above all resident/disk data.
inline std::optional<int> KnownAirTerrainColumnFloor(const UWorld &world,
                                                     glm::ivec3 ground)
{
  ground.y = 0;
  const int minY = world.GetProceduralSettings().MinHeight;
  const int maxY = world.GetProceduralSettings().MaxHeight;
  if (maxY < minY || !world.IsTerrainColumnCompleteFast(ground))
    return {};
  const auto &folder = world.GetWorldFolderPath();
  if (folder.empty())
    return {};
  const auto &storage = world.GetChunkStorage();
  if (storage.IsColumnSavePending(ground))
    return {};
  const int resident =
      GetHighestNonAirChunkSlice(world.GetBlockWorld(), ground, maxY, minY);
  const int disk = storage.GetHighestChunkSliceOnDisk(folder, ground);
  const int highest = std::max(resident, disk);
  if (highest == std::numeric_limits<int>::max())
    return {};
  return std::max(FloorDiv(minY, CHUNK_SIZE), highest + 1);
}
inline bool IsKnownAirTerrainSlice(const UWorld &world, glm::ivec3 chunk)
{
  const int minCy =
      FloorDiv(world.GetProceduralSettings().MinHeight, CHUNK_SIZE);
  const int maxCy =
      FloorDiv(world.GetProceduralSettings().MaxHeight, CHUNK_SIZE);
  if (chunk.y < minCy || chunk.y > maxCy)
    return true;
  return world.GetBlockWorld().IsKnownAbsentAirChunk(chunk);
}

/// A completed terrain producer and an exact disk membership index can also
/// prove a missing internal section. A column maximum cannot do so: adding an
/// upper island must not turn an independently empty lower section unknown.
inline bool IsKnownAbsentInternalTerrainSlice(const UWorld &world,
                                              glm::ivec3 chunk)
{
  const glm::ivec3 column(chunk.x, 0, chunk.z);
  const auto &folder = world.GetWorldFolderPath();
  const auto &storage = world.GetChunkStorage();
  return !folder.empty() && world.IsTerrainColumnCompleteFast(column) &&
         !storage.IsColumnSavePending(column) &&
         !world.GetBlockWorld().GetChunkManager().HasChunk(chunk) &&
         storage.IsChunkSliceAbsentOnDisk(folder, chunk);
}
}

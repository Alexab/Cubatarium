#include "World/Chunks/TerrainColumnUtil.h"
#include <algorithm>
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

namespace cutum
{
namespace
{
bool HasDecodedColumnData(const UBlockWorld &world, glm::ivec3 ground,
                          int max_y, int min_y)
{
  if (max_y < min_y)
    return false;
  for (int cy = FloorDiv(min_y, CHUNK_SIZE); cy <= FloorDiv(max_y, CHUNK_SIZE);
       ++cy)
    if (!world.GetChunkManager().HasDecodedTerrainSlice(
            {ground.x, cy, ground.z}))
      return false;
  return true;
}
} // namespace

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY, int minWorldY)
{
  if (maxWorldY < minWorldY)
  {
    return true;
  }
  if (HasDecodedColumnData(world,
                           UChunkManager::WorldToChunk({worldX, 0, worldZ}),
                           maxWorldY, minWorldY))
    return false;
  for (int y = minWorldY; y <= maxWorldY; ++y)
  {
    if (world.GetBlock(glm::ivec3(worldX, y, worldZ)) != BLOCK_AIR)
    {
      return false;
    }
  }
  return true;
}

bool IsTerrainColumnSaveCandidate(const UBlockWorld &world, glm::ivec3 column,
                                  int max_y, int min_y)
{
  return world.GetChunkManager().HasResidentGeometryColumn(column) ||
         HasDecodedColumnData(world, column, max_y, min_y);
}

int GetHighestNonAirChunkSlice(const UBlockWorld &world, glm::ivec3 groundCoord,
                               int maxWorldY, int minWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  int highest = kNoTerrainChunkSlice;
  for (int cy = minCy; cy <= maxCy; ++cy)
  {
    const UChunk *chunk = world.GetChunkManager().GetChunk(
        glm::ivec3(groundCoord.x, cy, groundCoord.z));
    if (!chunk || chunk->GetNonAirCount() == 0)
    {
      continue;
    }
    highest = std::max(highest, cy);
  }
  return highest;
}

int GetRequiredTerrainColumnTopCy(const UBlockWorld &world, glm::ivec3 groundCoord,
                                  int maxWorldY, int highestCyOnDisk,
                                  int minWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  int topCy = GetHighestNonAirChunkSlice(world, groundCoord, maxWorldY,
                                         minWorldY);
  if (highestCyOnDisk != kNoTerrainChunkSlice &&
      (topCy == kNoTerrainChunkSlice || highestCyOnDisk > topCy))
  {
    topCy = highestCyOnDisk;
  }
  if (topCy == kNoTerrainChunkSlice)
  {
    return kNoTerrainChunkSlice;
  }
  return std::min(topCy, maxCy);
}

int GetHighestTerrainSliceForSave(const UBlockWorld &world, glm::ivec3 ground,
                                  int max_y, int min_y)
{
  const int non_air = GetHighestNonAirChunkSlice(world, ground, max_y, min_y);
  int decoded = kNoTerrainChunkSlice;
  for (int cy = FloorDiv(min_y, CHUNK_SIZE); cy <= FloorDiv(max_y, CHUNK_SIZE);
       ++cy)
    if (world.GetChunkManager().HasDecodedTerrainSlice(
            {ground.x, cy, ground.z}))
      decoded = cy;
  return decoded > non_air &&
                 IsTerrainChunkComplete(world, ground, max_y, decoded, min_y)
             ? decoded
             : non_air;
}

void MaterializeTerrainColumnSliceRange(UBlockWorld &world, glm::ivec3 groundCoord,
                                      int fromCy, int toCy)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  if (toCy < fromCy)
  {
    return;
  }
  for (int cy = fromCy; cy <= toCy; ++cy)
  {
    const glm::ivec3 slice(groundCoord.x, cy, groundCoord.z);
    if (!world.GetChunkManager().HasLoadedTerrainSlice(slice))
      world.GetChunkManager().EnsureChunk(slice);
  }
}

void MaterializeRequiredTerrainColumnSlices(UBlockWorld &world,
                                            glm::ivec3 groundCoord,
                                            int maxWorldY, int highestCyOnDisk,
                                            int minWorldY)
{
  const int topCy = GetRequiredTerrainColumnTopCy(
      world, groundCoord, maxWorldY, highestCyOnDisk, minWorldY);
  if (topCy == kNoTerrainChunkSlice)
  {
    return;
  }
  MaterializeTerrainColumnSliceRange(world, groundCoord,
                                    FloorDiv(minWorldY, CHUNK_SIZE), topCy);
}

void MaterializeTerrainColumnAirSlices(UBlockWorld &world, glm::ivec3 groundCoord,
                                     int maxWorldY, int minWorldY)
{
  MaterializeRequiredTerrainColumnSlices(world, groundCoord, maxWorldY,
                                         kNoTerrainChunkSlice, minWorldY);
}

bool AreTerrainColumnSlicesLoaded(const UBlockWorld &world,
                                  glm::ivec3 groundCoord, int maxWorldY,
                                  int minimumSliceCy, int minWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  int highestLoadedCy = kNoTerrainChunkSlice;
  for (int cy = minCy; cy <= maxCy; ++cy)
  {
    if (world.GetChunkManager().HasLoadedTerrainSlice(
            glm::ivec3(groundCoord.x, cy, groundCoord.z)))
    {
      highestLoadedCy = cy;
    }
  }
  if (highestLoadedCy == kNoTerrainChunkSlice)
  {
    return false;
  }
  const int highestNonAirCy =
      GetHighestNonAirChunkSlice(world, groundCoord, maxWorldY, minWorldY);
  int requiredCy = highestLoadedCy;
  if (highestNonAirCy != kNoTerrainChunkSlice &&
      highestNonAirCy > requiredCy)
  {
    requiredCy = highestNonAirCy;
  }
  if (minimumSliceCy != kNoTerrainChunkSlice && minimumSliceCy > requiredCy)
  {
    requiredCy = minimumSliceCy;
  }
  requiredCy = std::min(requiredCy, maxCy);
  for (int cy = minCy; cy <= requiredCy; ++cy)
  {
    if (!world.GetChunkManager().HasLoadedTerrainSlice(
            glm::ivec3(groundCoord.x, cy, groundCoord.z)))
    {
      return false;
    }
  }
  return true;
}

bool IsTerrainChunkComplete(const UBlockWorld &world, glm::ivec3 groundCoord,
                            int maxWorldY, int minimumSliceCy,
                            int minWorldY)
{
  if (groundCoord.y != 0 ||
      world.GetChunkManager().HasUnresolvedTerrainColumnRead(groundCoord))
  {
    return false;
  }
  if (!AreTerrainColumnSlicesLoaded(world, groundCoord, maxWorldY,
                                    minimumSliceCy, minWorldY))
  {
    return false;
  }
  if (HasDecodedColumnData(world, groundCoord, maxWorldY, minWorldY))
    return true;
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      const int worldX = groundCoord.x * CHUNK_SIZE + lx;
      const int worldZ = groundCoord.z * CHUNK_SIZE + lz;
      if (TerrainColumnNeedsFill(world, worldX, worldZ, maxWorldY,
                                 minWorldY))
      {
        return false;
      }
    }
  }
  return true;
}

void ClearTerrainColumnChunks(UBlockWorld &world, glm::ivec3 groundCoord,
                              int maxWorldY, int minWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int minCy = FloorDiv(minWorldY, CHUNK_SIZE);
  const int maxCy = FloorDiv(maxWorldY, CHUNK_SIZE);
  for (int cy = minCy; cy <= maxCy; ++cy)
  {
    world.GetChunkManager().RemoveChunk(
        glm::ivec3(groundCoord.x, cy, groundCoord.z));
  }
}

} // namespace cutum

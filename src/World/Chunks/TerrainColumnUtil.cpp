#include "World/Chunks/TerrainColumnUtil.h"
#include <algorithm>
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"
#include "World/Math/GridMath.h"

namespace cutum
{

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY, int minWorldY)
{
  if (maxWorldY < minWorldY)
  {
    return true;
  }
  for (int y = minWorldY; y <= maxWorldY; ++y)
  {
    if (world.GetBlock(glm::ivec3(worldX, y, worldZ)) != BLOCK_AIR)
    {
      return false;
    }
  }
  return true;
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
    world.GetChunkManager().EnsureChunk(
        glm::ivec3(groundCoord.x, cy, groundCoord.z));
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
    if (world.GetChunkManager().HasChunk(
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
    if (!world.GetChunkManager().HasChunk(
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
  if (groundCoord.y != 0)
  {
    return false;
  }
  if (!AreTerrainColumnSlicesLoaded(world, groundCoord, maxWorldY,
                                    minimumSliceCy, minWorldY))
  {
    return false;
  }
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

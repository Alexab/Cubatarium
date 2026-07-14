#include "World/Chunks/TerrainColumnUtil.h"
#include <algorithm>
#include "World/Chunks/Chunk.h"
#include "World/Core/BlockWorld.h"

namespace cutum
{

bool TerrainColumnNeedsFill(const UBlockWorld &world, int worldX, int worldZ,
                            int maxWorldY)
{
  if (world.GetBlock(glm::ivec3(worldX, 0, worldZ)) == BLOCK_AIR)
  {
    return true;
  }
  const int topY = std::max(1, maxWorldY);
  for (int y = 1; y <= topY; ++y)
  {
    if (world.GetBlock(glm::ivec3(worldX, y, worldZ)) != BLOCK_AIR)
    {
      return false;
    }
  }
  return true;
}

int GetHighestNonAirChunkSlice(const UBlockWorld &world, glm::ivec3 groundCoord,
                               int maxWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  int highest = -1;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    const UChunk *chunk = world.GetChunkManager().GetChunk(
        glm::ivec3(groundCoord.x, cy, groundCoord.z));
    if (!chunk)
    {
      continue;
    }
    for (const BlockId block : chunk->GetData())
    {
      if (block != BLOCK_AIR)
      {
        highest = std::max(highest, cy);
        break;
      }
    }
  }
  return highest;
}

int GetRequiredTerrainColumnTopCy(const UBlockWorld &world, glm::ivec3 groundCoord,
                                  int maxWorldY, int highestCyOnDisk)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  int topCy = GetHighestNonAirChunkSlice(world, groundCoord, maxWorldY);
  if (highestCyOnDisk > topCy)
  {
    topCy = highestCyOnDisk;
  }
  if (topCy < 0)
  {
    return -1;
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
                                            int maxWorldY, int highestCyOnDisk)
{
  const int topCy =
      GetRequiredTerrainColumnTopCy(world, groundCoord, maxWorldY, highestCyOnDisk);
  if (topCy < 0)
  {
    return;
  }
  MaterializeTerrainColumnSliceRange(world, groundCoord, 0, topCy);
}

void MaterializeTerrainColumnAirSlices(UBlockWorld &world, glm::ivec3 groundCoord,
                                     int maxWorldY)
{
  MaterializeRequiredTerrainColumnSlices(world, groundCoord, maxWorldY, -1);
}

bool AreTerrainColumnSlicesLoaded(const UBlockWorld &world,
                                  glm::ivec3 groundCoord, int maxWorldY,
                                  int minimumSliceCy)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  int highestLoadedCy = -1;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    if (world.GetChunkManager().HasChunk(
            glm::ivec3(groundCoord.x, cy, groundCoord.z)))
    {
      highestLoadedCy = cy;
    }
  }
  if (highestLoadedCy < 0)
  {
    return false;
  }
  const int highestNonAirCy =
      GetHighestNonAirChunkSlice(world, groundCoord, maxWorldY);
  int requiredCy = highestLoadedCy;
  if (highestNonAirCy > requiredCy)
  {
    requiredCy = highestNonAirCy;
  }
  if (minimumSliceCy > requiredCy)
  {
    requiredCy = minimumSliceCy;
  }
  requiredCy = std::min(requiredCy, maxCy);
  for (int cy = 0; cy <= requiredCy; ++cy)
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
                            int maxWorldY, int minimumSliceCy)
{
  if (groundCoord.y != 0)
  {
    return false;
  }
  if (!AreTerrainColumnSlicesLoaded(world, groundCoord, maxWorldY,
                                    minimumSliceCy))
  {
    return false;
  }
  for (int lx = 0; lx < CHUNK_SIZE; ++lx)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      const int worldX = groundCoord.x * CHUNK_SIZE + lx;
      const int worldZ = groundCoord.z * CHUNK_SIZE + lz;
      if (TerrainColumnNeedsFill(world, worldX, worldZ, maxWorldY))
      {
        return false;
      }
    }
  }
  return true;
}

void ClearTerrainColumnChunks(UBlockWorld &world, glm::ivec3 groundCoord,
                              int maxWorldY)
{
  if (groundCoord.y != 0)
  {
    groundCoord.y = 0;
  }
  const int maxCy = (maxWorldY + CHUNK_SIZE - 1) / CHUNK_SIZE;
  for (int cy = 0; cy <= maxCy; ++cy)
  {
    world.GetChunkManager().RemoveChunk(
        glm::ivec3(groundCoord.x, cy, groundCoord.z));
  }
}

} // namespace cutum

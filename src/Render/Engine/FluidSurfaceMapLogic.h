#pragma once

#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Math/GridMath.h"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace cutum
{

class UBlockRegistry;

inline bool ShouldRefreshFluidSurfaceWindow(int old_origin_x, int old_origin_z,
                                            int new_origin_x, int new_origin_z)
{
  const int threshold =
      URuntimeTuning::Get().FluidSurfaceWindowMoveThreshold;
  return std::abs(new_origin_x - old_origin_x) >= threshold ||
         std::abs(new_origin_z - old_origin_z) >= threshold;
}

inline float FluidSurfaceStagingSentinel()
{
  return -1000.0f;
}

/// Scroll CPU staging when the fluid window origin moves. Returns true if
/// staging was rewritten (caller must patch newly exposed strips).
inline bool ScrollFluidSurfaceStagingWindow(
    std::vector<float> &surfaceStaging, std::vector<uint8_t> &fluidIndexStaging,
    std::vector<float> &fluidBottomStaging, int sizeBlocks,
    glm::ivec2 oldOriginBlock, glm::ivec2 newOriginBlock, float noSurfaceSentinel)
{
  const int shiftX = newOriginBlock.x - oldOriginBlock.x;
  const int shiftZ = newOriginBlock.y - oldOriginBlock.y;
  if (shiftX == 0 && shiftZ == 0)
  {
    return false;
  }
  const size_t texelCount =
      static_cast<size_t>(sizeBlocks) * static_cast<size_t>(sizeBlocks);
  if (std::abs(shiftX) >= sizeBlocks || std::abs(shiftZ) >= sizeBlocks)
  {
    std::fill(surfaceStaging.begin(), surfaceStaging.end(), noSurfaceSentinel);
    std::fill(fluidIndexStaging.begin(), fluidIndexStaging.end(),
              static_cast<uint8_t>(0));
    std::fill(fluidBottomStaging.begin(), fluidBottomStaging.end(),
              noSurfaceSentinel);
    return true;
  }
  std::vector<float> nextSurface(texelCount, noSurfaceSentinel);
  std::vector<uint8_t> nextIndex(texelCount, 0);
  std::vector<float> nextBottom(texelCount, noSurfaceSentinel);
  for (int z = 0; z < sizeBlocks; ++z)
  {
    for (int x = 0; x < sizeBlocks; ++x)
    {
      const int srcX = x + shiftX;
      const int srcZ = z + shiftZ;
      if (srcX < 0 || srcZ < 0 || srcX >= sizeBlocks || srcZ >= sizeBlocks)
      {
        continue;
      }
      const size_t dst =
          static_cast<size_t>(z) * static_cast<size_t>(sizeBlocks) +
          static_cast<size_t>(x);
      const size_t src =
          static_cast<size_t>(srcZ) * static_cast<size_t>(sizeBlocks) +
          static_cast<size_t>(srcX);
      nextSurface[dst] = surfaceStaging[src];
      nextIndex[dst] = fluidIndexStaging[src];
      nextBottom[dst] = fluidBottomStaging[src];
    }
  }
  surfaceStaging.swap(nextSurface);
  fluidIndexStaging.swap(nextIndex);
  fluidBottomStaging.swap(nextBottom);
  return true;
}

/// True when a ground chunk intersects new window but was fully outside the
/// previous window (needs strip patch after scroll).
inline bool FluidSurfaceChunkNeedsStripPatch(glm::ivec3 groundChunk,
                                             glm::ivec2 oldOriginBlock,
                                             glm::ivec2 newOriginBlock,
                                             int sizeBlocks)
{
  const int chunkMinX = groundChunk.x * CHUNK_SIZE;
  const int chunkMinZ = groundChunk.z * CHUNK_SIZE;
  const int chunkMaxX = chunkMinX + CHUNK_SIZE;
  const int chunkMaxZ = chunkMinZ + CHUNK_SIZE;
  const int newMaxX = newOriginBlock.x + sizeBlocks;
  const int newMaxZ = newOriginBlock.y + sizeBlocks;
  const bool inNew = chunkMaxX > newOriginBlock.x && chunkMinX < newMaxX &&
                     chunkMaxZ > newOriginBlock.y && chunkMinZ < newMaxZ;
  if (!inNew)
  {
    return false;
  }
  const int oldMaxX = oldOriginBlock.x + sizeBlocks;
  const int oldMaxZ = oldOriginBlock.y + sizeBlocks;
  const bool fullyInOld = chunkMinX >= oldOriginBlock.x &&
                          chunkMaxX <= oldMaxX &&
                          chunkMinZ >= oldOriginBlock.y &&
                          chunkMaxZ <= oldMaxZ;
  return !fullyInOld;
}

/// Fill one ground chunk into a fluid-surface staging window (CPU).
inline void PatchFluidSurfaceStagingChunk(
    std::vector<float> &surfaceStaging, std::vector<uint8_t> &fluidIndexStaging,
    std::vector<float> &fluidBottomStaging, int sizeBlocks,
    glm::ivec2 originBlock, glm::ivec3 groundChunk,
    const FluidSurfaceColumnSlice *slice, UBlockRegistry &registry,
    float noSurfaceSentinel)
{
  for (int localZ = 0; localZ < CHUNK_SIZE; ++localZ)
  {
    for (int localX = 0; localX < CHUNK_SIZE; ++localX)
    {
      const int bx = groundChunk.x * CHUNK_SIZE + localX;
      const int bz = groundChunk.z * CHUNK_SIZE + localZ;
      const int dx = bx - originBlock.x;
      const int dz = bz - originBlock.y;
      if (dx < 0 || dz < 0 || dx >= sizeBlocks || dz >= sizeBlocks)
      {
        continue;
      }
      const size_t idx = static_cast<size_t>(dz) * sizeBlocks + dx;
      if (!slice || !slice->HasSurface(localX, localZ))
      {
        surfaceStaging[idx] = noSurfaceSentinel;
        fluidIndexStaging[idx] = 0;
        fluidBottomStaging[idx] = noSurfaceSentinel;
        continue;
      }
      surfaceStaging[idx] =
          BlockTopY(static_cast<int>(slice->SurfaceBlockY[localZ][localX]));
      fluidBottomStaging[idx] =
          static_cast<float>(slice->BottomBlockY[localZ][localX]);
      fluidIndexStaging[idx] =
          FluidSurfaceIndexForBlock(slice->FluidId[localZ][localX], registry);
    }
  }
}

} // namespace cutum

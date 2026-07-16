#pragma once

#include "Render/Mesh/FluidSurfaceColumnSlice.h"
#include "World/Core/RuntimeTuning.h"
#include "World/Math/GridMath.h"
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

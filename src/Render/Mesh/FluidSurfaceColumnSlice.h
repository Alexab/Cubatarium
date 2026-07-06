#ifndef FLUIDSURFACECOLUMNSLICE_H
#define FLUIDSURFACECOLUMNSLICE_H

#include "World/Chunks/Chunk.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

class UBlockRegistry;
class UBlockWorld;

struct FluidSurfaceColumnSlice
{
  static constexpr int16_t kNoSurface = INT16_MIN;
  int16_t SurfaceBlockY[CHUNK_SIZE][CHUNK_SIZE];
  int16_t BottomBlockY[CHUNK_SIZE][CHUNK_SIZE];
  BlockId FluidId[CHUNK_SIZE][CHUNK_SIZE];

  FluidSurfaceColumnSlice()
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        SurfaceBlockY[lz][lx] = kNoSurface;
        BottomBlockY[lz][lx] = kNoSurface;
        FluidId[lz][lx] = BLOCK_AIR;
      }
    }
  }

  bool HasSurface(int localX, int localZ) const
  {
    return SurfaceBlockY[localZ][localX] != kNoSurface;
  }
};

uint8_t FluidSurfaceIndexForBlock(BlockId id, const UBlockRegistry &registry);

FluidSurfaceColumnSlice
BuildFluidSurfaceColumnSlice(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 groundChunkCoord, int scanHintY);

} // namespace cutum

#endif

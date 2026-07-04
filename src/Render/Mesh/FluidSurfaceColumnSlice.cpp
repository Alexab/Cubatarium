#include "Render/Mesh/FluidSurfaceColumnSlice.h"

#include "Blocks/BlockRegistry.h"
#include "World/Core/BlockWorld.h"
#include "World/Core/FluidColumnSurfaceQuery.h"

namespace cutum
{

uint8_t FluidSurfaceIndexForBlock(BlockId id, const UBlockRegistry &registry)
{
  if (id == BLOCK_AIR)
  {
    return 0;
  }
  const BlockId water_id = registry.GetIdByTypeName("water");
  if (water_id != BLOCK_AIR && id == water_id)
  {
    return 1;
  }
  const BlockId lava_id = registry.GetIdByTypeName("lava");
  if (lava_id != BLOCK_AIR && id == lava_id)
  {
    return 2;
  }
  if (registry.GetRenderStyle(id) == BlockRenderStyle::Fluid)
  {
    return 1;
  }
  return 0;
}

FluidSurfaceColumnSlice
BuildFluidSurfaceColumnSlice(const UBlockWorld &world, UBlockRegistry &registry,
                             glm::ivec3 groundChunkCoord, int scanHintY)
{
  FluidSurfaceColumnSlice slice;
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      slice.surfaceBlockY[lz][lx] = FluidSurfaceColumnSlice::kNoSurface;
      slice.fluidId[lz][lx] = BLOCK_AIR;
    }
  }

  const glm::ivec3 origin(groundChunkCoord.x * CHUNK_SIZE, 0,
                          groundChunkCoord.z * CHUNK_SIZE);
  for (int lz = 0; lz < CHUNK_SIZE; ++lz)
  {
    for (int lx = 0; lx < CHUNK_SIZE; ++lx)
    {
      const int bx = origin.x + lx;
      const int bz = origin.z + lz;
      const FluidColumnSurface column =
          FindFluidColumnSurfaceAt(world, registry, bx, bz, scanHintY);
      if (!column.valid)
      {
        continue;
      }
      slice.surfaceBlockY[lz][lx] = static_cast<int16_t>(column.surfaceBlockY);
      slice.fluidId[lz][lx] = column.fluidId;
    }
  }
  return slice;
}

} // namespace cutum

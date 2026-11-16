#include "Blocks/BlockRegistry.h"
#include "World/Lighting/GpuSkylightColumnSeed.h"

namespace cutum
{

bool gTestSkylightSeedAvailable = false;
void SetTestSkylightSeedAvailable(bool available)
{
  gTestSkylightSeedAvailable = available;
}

// Host/unit-test stub: no GL. RelightChunkCoords falls through to CPU
// PropagateSkylightColumn without linking GpuSkylightColumnSeed.cpp.

bool TryGpuSeedSkylightColumns(const std::array<uint8_t, CHUNK_VOLUME> & /*occ*/,
                               std::array<uint8_t, CHUNK_VOLUME> & /*sky_out*/)
{
  return false;
}

bool ApplyGpuSkylightSeedToChunk(UChunk &chunk, UBlockRegistry &registry)
{
  if (!gTestSkylightSeedAvailable)
    return false;
  std::array<uint8_t, CHUNK_VOLUME> occupancy{};
  for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
      {
        const BlockId id = chunk.GetBlockLocal({x, y, z});
        occupancy[(y * CHUNK_SIZE + z) * CHUNK_SIZE + x] =
            id != BLOCK_AIR && !registry.IsTransparent(id) &&
                    registry.IsSolid(id)
                ? 1
                : (registry.IsLiquid(id) ? 2 : 0);
      }
  std::array<uint8_t, CHUNK_VOLUME> sky{};
  SeedSkylightColumnsCpu(occupancy, sky);
  for (int y = 0; y < CHUNK_SIZE; ++y)
    for (int z = 0; z < CHUNK_SIZE; ++z)
      for (int x = 0; x < CHUNK_SIZE; ++x)
        chunk.SetLightLocal({x, y, z},
                            sky[(y * CHUNK_SIZE + z) * CHUNK_SIZE + x],
                            chunk.GetBlockLightLocal({x, y, z}));
  return true;
}

uint64_t GpuSkylightSeedDispatchCount() { return 0; }
uint64_t ConsumeGpuSkylightSeedReadbackCount() { return 0; }

uint64_t ConsumeGpuSkylightSeedApplyCount() { return 0; }

} // namespace cutum

#include "World/Lighting/GpuSkylightColumnSeed.h"

namespace cutum
{

// Host/unit-test stub: no GL. RelightChunkCoords falls through to CPU
// PropagateSkylightColumn without linking GpuSkylightColumnSeed.cpp.

bool TryGpuSeedSkylightColumns(const std::array<uint8_t, CHUNK_VOLUME> & /*occ*/,
                               std::array<uint8_t, CHUNK_VOLUME> & /*sky_out*/)
{
  return false;
}

bool ApplyGpuSkylightSeedToChunk(UChunk & /*chunk*/, UBlockRegistry & /*registry*/)
{
  return false;
}

uint64_t GpuSkylightSeedDispatchCount() { return 0; }
uint64_t ConsumeGpuSkylightSeedReadbackCount() { return 0; }

uint64_t ConsumeGpuSkylightSeedApplyCount() { return 0; }

} // namespace cutum

#pragma once

#include "World/Chunks/Chunk.h"
#include <array>
#include <cstdint>
#include <vector>

namespace cutum
{

class UBlockRegistry;

/// CPU reference: per-column skylight seed from opaque occupancy.
/// From +Y: sky=15 until first opaque, then 0 below (no horizontal flood).
inline void SeedSkylightColumnsCpu(const std::array<uint8_t, CHUNK_VOLUME> &occ,
                                   std::array<uint8_t, CHUNK_VOLUME> &sky_out)
{
  sky_out.fill(0);
  const int n = CHUNK_SIZE;
  for (int z = 0; z < n; ++z)
  {
    for (int x = 0; x < n; ++x)
    {
      uint8_t level = 15;
      for (int y = n - 1; y >= 0; --y)
      {
        const int li = (y * n + z) * n + x;
        if (occ[static_cast<size_t>(li)])
        {
          sky_out[static_cast<size_t>(li)] = 0;
          level = 0;
        }
        else
        {
          sky_out[static_cast<size_t>(li)] = level;
        }
      }
    }
  }
}

/// Returns true if GPU path ran (Desktop). Out must be CHUNK_VOLUME.
bool TryGpuSeedSkylightColumns(const std::array<uint8_t, CHUNK_VOLUME> &occ,
                               std::array<uint8_t, CHUNK_VOLUME> &sky_out);

/// Pack occupancy, GPU seed, WriteSkyLight into chunk. Main thread + GL only.
bool ApplyGpuSkylightSeedToChunk(UChunk &chunk, UBlockRegistry &registry);

uint64_t GpuSkylightSeedDispatchCount();
/// Successful apply count since last consume (FramePerf telemetry).
uint64_t ConsumeGpuSkylightSeedApplyCount();

} // namespace cutum

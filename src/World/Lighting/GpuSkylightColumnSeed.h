#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Lighting/LightUtil.h"
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

/// Keep GPU-written sky columns; copy block light from async/sync packed source.
inline void MergeBlockLightKeepingGpuSky(
    UChunk &chunk, const std::array<uint8_t, CHUNK_VOLUME> &source_packed)
{
  for (int ly = 0; ly < CHUNK_SIZE; ++ly)
  {
    for (int lz = 0; lz < CHUNK_SIZE; ++lz)
    {
      for (int lx = 0; lx < CHUNK_SIZE; ++lx)
      {
        const glm::ivec3 local(lx, ly, lz);
        const int li = (ly * CHUNK_SIZE + lz) * CHUNK_SIZE + lx;
        const uint8_t packed = source_packed[static_cast<size_t>(li)];
        const int sky = chunk.GetSkyLightLocal(local);
        const int block_level = UnpackBlock(packed);
        chunk.SetLightLocal(local, sky, block_level);
      }
    }
  }
}

uint64_t GpuSkylightSeedDispatchCount();
/// Successful apply count since last consume (FramePerf telemetry).
uint64_t ConsumeGpuSkylightSeedApplyCount();

} // namespace cutum

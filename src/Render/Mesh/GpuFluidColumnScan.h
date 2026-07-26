#pragma once

#include "World/Chunks/Chunk.h"
#include <cstdint>
#include <vector>

namespace cutum
{

void SetPreferGpuFluidColumnScan(bool prefer);
bool PreferGpuFluidColumnScan();

/// CPU reference: for each XZ column, find highest Y with fluid flag set.
/// fluid_flags layout: index = (y * CHUNK_SIZE + z) * CHUNK_SIZE + x, y in
/// [0, height).
inline void ScanFluidColumnsCpu(const uint8_t *fluid_flags, int height,
                                std::vector<int16_t> &out_top_y)
{
  const int n = CHUNK_SIZE;
  out_top_y.assign(static_cast<size_t>(n * n), static_cast<int16_t>(-1));
  for (int z = 0; z < n; ++z)
  {
    for (int x = 0; x < n; ++x)
    {
      int16_t top = -1;
      for (int y = 0; y < height; ++y)
      {
        const size_t i = static_cast<size_t>((y * n + z) * n + x);
        if (fluid_flags[i] != 0)
        {
          top = static_cast<int16_t>(y);
        }
      }
      out_top_y[static_cast<size_t>(z * n + x)] = top;
    }
  }
}

/// Desktop compute scan; returns false on Android / init failure.
bool TryGpuScanFluidColumns(const uint8_t *fluid_flags, int height,
                            std::vector<int16_t> &out_top_y);

uint64_t GpuFluidColumnScanDispatchCount();

} // namespace cutum

#include "Render/Mesh/GpuFluidColumnScan.h"

namespace cutum
{

namespace
{
bool gPreferGpuFluidColumnScan = false;
} // namespace

void SetPreferGpuFluidColumnScan(bool prefer) { gPreferGpuFluidColumnScan = prefer; }

bool PreferGpuFluidColumnScan() { return gPreferGpuFluidColumnScan; }

bool TryGpuScanFluidColumns(const uint8_t *fluid_flags, int height,
                            std::vector<int16_t> &out_top_y)
{
  ScanFluidColumnsCpu(fluid_flags, height, out_top_y);
  return true;
}

uint64_t GpuFluidColumnScanDispatchCount() { return 0; }

} // namespace cutum

#include "Render/Mesh/GpuFluidColumnScan.h"

namespace cutum
{

// Host/unit-test stub: no GL. PreferGpu stays off so BuildFluidSurfaceColumnSlice
// uses FindFluidColumnSurfaceAt (CPU) without linking GpuFluidColumnScan.cpp.

void SetPreferGpuFluidColumnScan(bool /*prefer*/) {}

bool PreferGpuFluidColumnScan() { return false; }

bool TryGpuScanFluidColumns(const uint8_t * /*fluid_flags*/, int /*height*/,
                            std::vector<int16_t> & /*out_top_y*/)
{
  return false;
}

uint64_t GpuFluidColumnScanDispatchCount() { return 0; }
uint64_t ConsumeGpuFluidReadbackCount() { return 0; }

} // namespace cutum

#pragma once

#include "Render/Mesh/GpuFluidColumnScan.h"
#include "Render/Pipeline/GpuTransparentSort.h"
#include "World/Lighting/GpuBlocklightFlood.h"

namespace cutum
{
inline void InspectPersistentComputeGpuBuffers(TerrainGpuBufferCensus &out)
{
  InspectFluidColumnScanGpuBuffers(out);
  InspectTransparentSortGpuBuffers(out);
  InspectBlocklightFloodGpuBuffers(out);
}

inline void ReleasePersistentComputeGpuStorage()
{
  ReleaseFluidColumnScanGpuStorage();
  ReleaseTransparentSortGpuStorage();
  ReleaseBlocklightFloodGpuStorage();
}
} // namespace cutum

#pragma once

#include "Render/Engine/TerrainGpuBuffers.h"

namespace cutum
{
struct FlightCaptureGpuCheckpoint
{
  TerrainGpuBufferSnapshot beforeShutdown;
  TerrainGpuBufferSnapshot afterShutdown;
};
} // namespace cutum

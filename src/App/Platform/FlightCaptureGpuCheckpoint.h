#pragma once

#include "App/Platform/FlightCaptureCpuStorage.h"
#include "Render/Engine/TerrainGpuBuffers.h"

namespace cutum
{
struct FlightCaptureGpuCheckpoint
{
  TerrainGpuBufferSnapshot beforeShutdown;
  TerrainGpuBufferSnapshot afterShutdown;
  FlightCaptureCpuStorage cpuBeforeShutdown, cpuAfterShutdown;
};
} // namespace cutum

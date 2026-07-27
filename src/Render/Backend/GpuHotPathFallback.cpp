#include "Render/Backend/GpuHotPathFallback.h"

namespace cutum
{
namespace
{
uint64_t gHotPathFallbacks = 0;
}

void NoteGpuHotPathFallback() { ++gHotPathFallbacks; }

uint64_t ConsumeGpuHotPathFallbackCount()
{
  const uint64_t v = gHotPathFallbacks;
  gHotPathFallbacks = 0;
  return v;
}

} // namespace cutum

#include "World/Lighting/GpuBlocklightFlood.h"

namespace cutum
{

// Host/unit-test stub: no GL. RelightChunkCoords falls back to CPU
// PropagateBlocklight without linking GpuBlocklightFlood.cpp.

bool TryGpuPropagateBlocklight(UBlockWorld & /*world*/,
                               UBlockRegistry & /*registry*/,
                               glm::ivec3 /*chunk_coord*/)
{
  return false;
}

uint64_t ConsumeGpuBlocklightFloodCount() { return 0; }

void NoteGpuBlocklightFlood() {}

} // namespace cutum

#include "World/Lighting/GpuBlocklightFlood.h"
#include "World/Lighting/GpuLightTransport.h"

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

// Explicit host-only backend rejection; these tests never claim GPU parity.
bool TryGpuTransportLight(const LightTransportInput &, std::vector<uint8_t> &,
                          GpuLightTransportWork &work, std::string &reason)
{
  work = {};
  reason = "host_test_no_gpu_backend";
  return false;
}
uint64_t GpuLightTransportDispatchCount() { return 0; }

} // namespace cutum

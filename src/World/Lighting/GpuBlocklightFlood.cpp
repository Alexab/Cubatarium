#include "World/Lighting/GpuBlocklightFlood.h"

namespace cutum
{
namespace
{
uint64_t gBlocklightFloods = 0;
}

void NoteGpuBlocklightFlood() { ++gBlocklightFloods; }

uint64_t ConsumeGpuBlocklightFloodCount()
{
  const uint64_t v = gBlocklightFloods;
  gBlocklightFloods = 0;
  return v;
}

} // namespace cutum

#include "World/Diagnostics/JobStageTrace.h"

#include <string>

namespace cutum
{

// Lightweight dependencies used by the CPU-only ChunkLighting unit targets.
uint64_t UJobStageTrace::GetStreamingFrameEpoch() { return 0; }

void CubatariumLogInfo(const char *, const std::string &) {}

} // namespace cutum

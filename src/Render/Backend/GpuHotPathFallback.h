#pragma once

#include <cstdint>

namespace cutum
{

/// GPF5: desktop GPU hot-path fallback activation (per-frame consume).
void NoteGpuHotPathFallback();
uint64_t ConsumeGpuHotPathFallbackCount();

} // namespace cutum

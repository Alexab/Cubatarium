#pragma once

#include "World/Chunks/Chunk.h"
#include "World/Lighting/LightUtil.h"

namespace cutum
{

// Install only computed domains after read-set/catalog validation. A vertical
// seed is not a completed light solution: it would erase horizontal skylight.
// Bulk publication bumps the revision once, and never for an unchanged result.
inline bool
InstallComputedLight(UChunk &chunk,
                     const std::array<uint8_t, CHUNK_VOLUME> &computed,
                     bool include_skylight, bool include_block_light)
{
  const uint8_t mask = static_cast<uint8_t>((include_skylight ? 0x0F : 0) |
                                            (include_block_light ? 0xF0 : 0));
  if (mask == 0)
  {
    return false;
  }
  auto merged = chunk.GetLightData();
  bool changed = false;
  for (size_t i = 0; i < merged.size(); ++i)
  {
    const uint8_t next =
        static_cast<uint8_t>((merged[i] & ~mask) | (computed[i] & mask));
    changed |= next != merged[i];
    merged[i] = next;
  }
  if (changed)
  {
    chunk.GetLightDataMutable() = merged;
  }
  return changed;
}

} // namespace cutum

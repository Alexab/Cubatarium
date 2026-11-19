#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>

namespace cutum
{
// Opt-in, one-column mutation evidence; independent of presentation readiness.
inline bool TraceChunkResidencyEnabled(glm::ivec3 coord)
{
  struct Selection
  {
    int x{0}, z{0};
    bool enabled{false};
  };
  static const Selection selected = []
  {
    Selection result;
    if (const char *value = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE"))
    {
      char trailing;
      result.enabled =
          std::sscanf(value, "%d,%d%c", &result.x, &result.z, &trailing) == 2;
    }
    return result;
  }();
  return selected.enabled && selected.x == coord.x && selected.z == coord.z;
}
inline void TraceChunkResidency(const char *source, glm::ivec3 coord,
                                glm::ivec3 origin, uint64_t value = 0)
{
  if (!TraceChunkResidencyEnabled(coord))
    return;
  static std::atomic<uint64_t> sequence{0};
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  std::fprintf(stderr,
               "[ChunkResidency] seq=%llu tick_ms=%lld source=%s "
               "coord=(%d,%d,%d) origin=(%d,%d,%d) value=%llu\n",
               static_cast<unsigned long long>(++sequence),
               static_cast<long long>(ms), source, coord.x, coord.y, coord.z,
               origin.x, origin.y, origin.z,
               static_cast<unsigned long long>(value));
}
} // namespace cutum

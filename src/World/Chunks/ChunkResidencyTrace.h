#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>

namespace cutum
{
inline void FlushChunkResidencyTrace();
namespace residency_trace_detail
{
struct Selection
{
  int x{0}, z{0};
  bool enabled{false}, buffered{false};
};
inline const Selection &Config()
{
  static const Selection selected = []
  {
    Selection result;
    if (const char *value = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE"))
    {
      char trailing;
      result.enabled =
          std::sscanf(value, "%d,%d%c", &result.x, &result.z, &trailing) == 2;
    }
    if (const char *mode = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_BUFFERED"))
      result.buffered = mode[0] == '1';
    return result;
  }();
  return selected;
}
inline FILE *OpenOutput()
{
  if (const char *path = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_FILE"))
    if (FILE *file = std::fopen(path, "ab"))
      return file;
  return stderr;
}
struct Event
{
  // Internal callers pass string literals; retain no chunk/world ownership.
  const char *source{nullptr};
  glm::ivec3 coord{0}, origin{0};
  uint64_t value{0};
  const void *manager{nullptr};
  int64_t tick_ms{0};
};
inline void Print(FILE *output, uint64_t sequence, const Event &event)
{
  std::fprintf(output,
               "[ChunkResidency] seq=%llu tick_ms=%lld source=%s "
               "coord=(%d,%d,%d) origin=(%d,%d,%d) value=%llu manager=%p\n",
               static_cast<unsigned long long>(sequence),
               static_cast<long long>(event.tick_ms), event.source,
               event.coord.x, event.coord.y, event.coord.z, event.origin.x,
               event.origin.y, event.origin.z,
               static_cast<unsigned long long>(event.value), event.manager);
}
inline constexpr size_t Capacity = 4096;
inline constexpr uint64_t Closed = uint64_t{1} << 63;
struct Slot
{
  Event event;
  std::atomic<bool> ready{false};
};
struct Buffer
{
  std::array<Slot, Capacity> slots{};
  std::atomic<uint64_t> reservations{0};
};
inline Buffer &Events()
{
  static Buffer buffer;
  return buffer;
}
inline void Record(const Event &event)
{
  auto &buffer = Events();
  // Register after buffer construction so normal-exit export precedes its
  // destruction. Flight-sim explicitly exports before std::_Exit instead.
  static const bool registered = []
  { return std::atexit(FlushChunkResidencyTrace) == 0; }();
  (void)registered;
  auto prior = buffer.reservations.load(std::memory_order_relaxed);
  while (!(prior & Closed))
    if (buffer.reservations.compare_exchange_weak(prior, prior + 1,
                                                  std::memory_order_relaxed))
    {
      if (prior < Capacity)
      {
        auto &slot = buffer.slots[static_cast<size_t>(prior)];
        slot.event = event;
        slot.ready.store(true, std::memory_order_release);
      }
      return;
    }
}
} // namespace residency_trace_detail

// Opt-in, one-column mutation evidence; independent of presentation readiness.
inline bool TraceChunkResidencyEnabled(glm::ivec3 coord)
{
  const auto &selected = residency_trace_detail::Config();
  return selected.enabled && selected.x == coord.x && selected.z == coord.z;
}
inline void TraceChunkResidency(const char *source, glm::ivec3 coord,
                                glm::ivec3 origin, uint64_t value = 0,
                                const void *manager = nullptr)
{
  if (!TraceChunkResidencyEnabled(coord))
    return;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
  const residency_trace_detail::Event event{source, coord,   origin,
                                            value,  manager, ms};
  if (residency_trace_detail::Config().buffered)
  {
    residency_trace_detail::Record(event);
    return;
  }
  static FILE *output = residency_trace_detail::OpenOutput();
  static std::atomic<uint64_t> sequence{0};
  residency_trace_detail::Print(output, ++sequence, event);
  std::fflush(output);
}
inline void FlushChunkResidencyTrace()
{
  using namespace residency_trace_detail;
  if (!Config().enabled || !Config().buffered)
    return;
  auto &buffer = Events();
  const auto reserved =
      buffer.reservations.fetch_or(Closed, std::memory_order_acq_rel);
  if (reserved & Closed)
    return;
  FILE *output = OpenOutput();
  const size_t count =
      static_cast<size_t>(std::min<uint64_t>(reserved, Capacity));
  size_t published = 0;
  for (size_t n = 0; n < count; ++n)
    if (buffer.slots[n].ready.load(std::memory_order_acquire))
    {
      Print(output, n + 1, buffer.slots[n].event);
      ++published;
    }
  const auto dropped = reserved > Capacity ? reserved - Capacity : 0;
  const auto unpublished = count - published;
  // A racing writer is never read partially; incomplete/overflow exports are
  // explicit and cannot prove absence. No worker joins on the exit path.
  std::fprintf(output,
               "[ChunkResidencySummary] buffered=1 reserved=%llu published=%zu "
               "dropped=%llu unpublished=%zu complete=%d\n",
               static_cast<unsigned long long>(reserved), published,
               static_cast<unsigned long long>(dropped), unpublished,
               dropped == 0 && unpublished == 0 ? 1 : 0);
  std::fflush(output);
  if (output != stderr)
    std::fclose(output);
}
} // namespace cutum

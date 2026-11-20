#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>

namespace cutum
{
inline void FlushChunkResidencyTrace();
namespace residency_trace_detail
{
struct Selection
{
  int x{0}, z{0};
  bool enabled{false}, buffered{false}, start_column{false};
  int secondary_x{0}, secondary_z{0};
  bool secondary{false};
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
    if (const char *mode =
            std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_START_COLUMN"))
      if (mode[0] == '1')
        result.enabled = result.start_column = true;
    // The second column is a bounded, buffered runtime observation only.
    // Separate files preserve the primary one-column evidence contract.
    const char *secondary = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY");
    const char *path = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE");
    const char *primary_path = std::getenv("CUBA_CHUNK_RESIDENCY_TRACE_FILE");
    if (result.start_column && result.buffered && secondary && path && *path &&
        primary_path && *primary_path && std::string_view(path) != primary_path)
    {
      char trailing;
      result.secondary = std::sscanf(secondary, "%d,%d%c", &result.secondary_x,
                                     &result.secondary_z, &trailing) == 2;
    }
    return result;
  }();
  return selected;
}
struct RuntimeColumn
{
  std::atomic<bool> claimed{false}, armed{false};
  int x{0}, z{0};
};
inline RuntimeColumn &StartColumn()
{
  static RuntimeColumn column;
  return column;
}
inline FILE *OpenOutput(bool secondary = false)
{
  if (const char *path =
          std::getenv(secondary ? "CUBA_CHUNK_RESIDENCY_TRACE_SECONDARY_FILE"
                                : "CUBA_CHUNK_RESIDENCY_TRACE_FILE"))
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

// Runtime selection is published once; startup scratch workers cannot select
// or redirect the observed column. Readers acquire the immutable coordinates.
inline void SelectChunkResidencyStartColumn(glm::ivec3 focus)
{
  if (!residency_trace_detail::Config().start_column)
    return;
  auto &column = residency_trace_detail::StartColumn();
  bool expected = false;
  if (column.claimed.compare_exchange_strong(expected, true))
  {
    column.x = focus.x;
    column.z = focus.z;
    column.armed.store(true, std::memory_order_release);
  }
}
inline std::optional<glm::ivec2> ChunkResidencyTraceColumn()
{
  const auto &selected = residency_trace_detail::Config();
  if (!selected.enabled)
    return std::nullopt;
  if (!selected.start_column)
    return glm::ivec2(selected.x, selected.z);
  const auto &column = residency_trace_detail::StartColumn();
  if (!column.armed.load(std::memory_order_acquire))
    return std::nullopt;
  return glm::ivec2(column.x, column.z);
}
inline std::optional<glm::ivec2> ChunkResidencyTraceSecondaryColumn()
{
  const auto &selected = residency_trace_detail::Config();
  const auto primary = ChunkResidencyTraceColumn();
  if (!selected.secondary || !primary ||
      (primary->x == selected.secondary_x &&
       primary->y == selected.secondary_z))
    return std::nullopt;
  return glm::ivec2(selected.secondary_x, selected.secondary_z);
}
// Opt-in mutation evidence; independent of presentation readiness.
inline bool TraceChunkResidencyEnabled(glm::ivec3 coord)
{
  const auto column = ChunkResidencyTraceColumn();
  if (column && column->x == coord.x && column->y == coord.z)
    return true;
  const auto secondary = ChunkResidencyTraceSecondaryColumn();
  return secondary && secondary->x == coord.x && secondary->y == coord.z;
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
inline void TraceChunkResidencyRuntimeOwner(const void *manager,
                                            glm::ivec3 focus)
{
  SelectChunkResidencyStartColumn(focus);
  if (const auto column = ChunkResidencyTraceColumn())
    TraceChunkResidency("runtime_owner", {column->x, focus.y, column->y}, focus,
                        0, manager);
  if (const auto column = ChunkResidencyTraceSecondaryColumn())
    TraceChunkResidency("runtime_owner", {column->x, focus.y, column->y}, focus,
                        0, manager);
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
  const auto secondary_column = ChunkResidencyTraceSecondaryColumn();
  FILE *secondary_output = secondary_column ? OpenOutput(true) : nullptr;
  const size_t count =
      static_cast<size_t>(std::min<uint64_t>(reserved, Capacity));
  size_t published = 0;
  size_t primary_published = 0, secondary_published = 0;
  for (size_t n = 0; n < count; ++n)
    if (buffer.slots[n].ready.load(std::memory_order_acquire))
    {
      const auto &event = buffer.slots[n].event;
      const bool secondary = secondary_column &&
                             event.coord.x == secondary_column->x &&
                             event.coord.z == secondary_column->y;
      if (secondary)
        Print(secondary_output, ++secondary_published, event);
      else
        Print(output, secondary_output ? ++primary_published : n + 1, event);
      ++published;
    }
  const auto dropped = reserved > Capacity ? reserved - Capacity : 0;
  const auto unpublished = count - published;
  // A racing writer is never read partially; incomplete/overflow exports are
  // explicit and cannot prove absence. No worker joins on the exit path.
  const auto summary = [&](FILE *file, uint64_t requested, size_t written)
  {
    std::fprintf(
        file,
        "[ChunkResidencySummary] buffered=1 reserved=%llu published=%zu "
        "dropped=%llu unpublished=%zu complete=%d\n",
        static_cast<unsigned long long>(requested), written,
        static_cast<unsigned long long>(dropped), unpublished,
        dropped == 0 && unpublished == 0 ? 1 : 0);
    std::fflush(file);
    if (file != stderr)
      std::fclose(file);
  };
  // Loss is charged to both files: an unrecorded event cannot be assigned to
  // either column. Neither file may certify completeness after shared loss.
  if (secondary_output)
  {
    summary(output, primary_published + dropped + unpublished,
            primary_published);
    summary(secondary_output, secondary_published + dropped + unpublished,
            secondary_published);
  }
  else
    summary(output, reserved, published);
}
} // namespace cutum

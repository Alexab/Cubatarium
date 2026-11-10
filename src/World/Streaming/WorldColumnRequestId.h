#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace cutum
{

/// The trace switch is configured before process startup and remains fixed
/// for the lifetime of this process; cache it because streaming checks this
/// helper for many columns per frame.
inline bool WorldColumnSourceTraceEnabled()
{
  static const bool enabled = []()
  {
    const char *value = std::getenv("CUBA_WORLD_COLUMN_SOURCE_TRACE");
    return value && value[0] == '1';
  }();
  return enabled;
}

/// Process-wide ID space shared by demand and accepted scheduler request IDs,
/// so both can be joined across source lookup and worker lifecycle traces.
inline uint64_t NextWorldColumnRequestId()
{
  static std::atomic<uint64_t> next_id{1};
  return next_id.fetch_add(1, std::memory_order_relaxed);
}

inline uint64_t NewWorldColumnRequestIdForTrace()
{
  return WorldColumnSourceTraceEnabled() ? NextWorldColumnRequestId() : 0;
}

} // namespace cutum

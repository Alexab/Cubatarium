#pragma once

#include "Audio/AudioEvent.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace cutum
{

/// Bounded MPMC ring for audio events with condvar wake.
///
/// Producers (game thread) call Post() — try-push without waiting; when the
/// ring is full the oldest event is evicted (footsteps are worth losing, a
/// stalled frame is not). The consumer (audio director thread) calls
/// DrainWait() which wakes immediately on a post so action sounds start
/// within ~1–10 ms, or after @p timeoutMs for housekeeping ticks.
class UAudioEventQueue
{
public:
  explicit UAudioEventQueue(std::size_t capacity = 256);

  /// Non-blocking post; wakes the consumer. Never allocates.
  void Post(const UAudioEvent &event);

  /// Drains all pending events. Returns false on timeout with none pending.
  bool DrainWait(std::vector<UAudioEvent> &out, std::uint32_t timeoutMs);

  /// Total events evicted by overflow since construction (telemetry).
  std::uint64_t DiscardedOverflow() const { return DiscardedCount; }

  std::size_t Capacity() const { return Capacity_; }

private:
  std::size_t Capacity_;
  std::deque<UAudioEvent> Items;
  mutable std::mutex Mutex;
  std::condition_variable WakeCv;
  std::uint64_t DiscardedCount{0};
};

} // namespace cutum

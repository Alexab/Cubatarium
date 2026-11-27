#pragma once

#include <cstddef>
#include <utility>

namespace cutum
{

/// Reserve only the first existing apply opportunity for a consumer's data.
/// The caller alternates this lane after an actual apply attempt, since a
/// time-limited frame may requeue every selected result after the first one.
template <typename Queue, typename Rank, typename Required>
auto DrainReadyDataForConsumers(Queue &queue, std::size_t max_count, Rank rank,
                                Required required, bool prefer_consumer,
                                std::size_t *available = nullptr,
                                double *wait_ms = nullptr,
                                double *held_ms = nullptr)
{
  if (!prefer_consumer)
    return queue.DrainBestByKeyUpTo(max_count, rank, available, wait_ms,
                                    held_ms);
  return queue.DrainBestByKeyUpTo(max_count, rank, available, wait_ms,
                                  held_ms, std::move(required));
}

} // namespace cutum

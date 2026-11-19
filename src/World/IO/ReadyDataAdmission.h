#pragma once

#include <cstddef>
#include <iterator>
#include <tuple>
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
  if (!prefer_consumer || max_count == 0)
    return queue.DrainBestByKeyUpTo(max_count, rank, available, wait_ms,
                                    held_ms);
  auto first = queue.DrainBestByKeyUpTo(
      1, [&](const auto &value)
      { return std::tuple{!required(value), rank(value)}; }, available, wait_ms,
      held_ms);
  if (first.empty() || max_count == 1)
    return first;
  double rest_wait = 0, rest_held = 0;
  auto rest = queue.DrainBestByKeyUpTo(max_count - 1, rank, nullptr, &rest_wait,
                                       &rest_held);
  if (wait_ms)
    *wait_ms += rest_wait;
  if (held_ms)
    *held_ms += rest_held;
  first.insert(first.end(), std::make_move_iterator(rest.begin()),
               std::make_move_iterator(rest.end()));
  return first;
}

} // namespace cutum

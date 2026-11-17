#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>

namespace cutum
{
// Running columns retain their queue obligations but cannot monopolize the
// capture lane. Each lane gets a bounded scan, including when priority is busy.
template <typename Key, typename Ready>
std::optional<Key> PopReadyRelightColumn(std::deque<Key> &priority,
                                         std::deque<Key> &ordinary, Ready ready,
                                         bool &from_priority,
                                         std::size_t scan_cap = 64)
{
  const auto pop = [&](std::deque<Key> &queue) -> std::optional<Key>
  {
    const auto count = std::min(scan_cap, queue.size());
    for (std::size_t i = 0; i < count; ++i)
    {
      const Key key = queue.front();
      queue.pop_front();
      if (ready(key))
        return key;
      queue.push_back(key);
    }
    return std::nullopt;
  };
  from_priority = true;
  if (auto key = pop(priority))
    return key;
  from_priority = false;
  return pop(ordinary);
}
} // namespace cutum

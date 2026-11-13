#include "Audio/AudioEventQueue.h"

#include <chrono>

namespace cutum
{

UAudioEventQueue::UAudioEventQueue(std::size_t capacity) : Capacity_(capacity) {}

void UAudioEventQueue::Post(const UAudioEvent &event)
{
  {
    std::lock_guard<std::mutex> lock(Mutex);
    if (Capacity_ == 0)
    {
      return;
    }
    while (Items.size() >= Capacity_)
    {
      Items.pop_front();
      ++DiscardedCount;
    }
    Items.push_back(event);
  }
  WakeCv.notify_one();
}

bool UAudioEventQueue::DrainWait(std::vector<UAudioEvent> &out,
                                 std::uint32_t timeoutMs)
{
  out.clear();
  std::unique_lock<std::mutex> lock(Mutex);
  if (Items.empty())
  {
    // Spurious wakeups and racing posts are fine: worst case we loop once
    // more with an empty ring and fall back to the timeout.
    WakeCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                    [this]() { return !Items.empty(); });
    if (Items.empty())
    {
      return false;
    }
  }
  out.assign(std::make_move_iterator(Items.begin()),
             std::make_move_iterator(Items.end()));
  Items.clear();
  return true;
}

} // namespace cutum

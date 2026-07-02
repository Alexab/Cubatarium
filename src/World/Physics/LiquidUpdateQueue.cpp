#include "World/Physics/LiquidUpdateQueue.h"
#include <algorithm>

namespace cutum
{

bool ULiquidUpdateQueue::Enqueue(glm::ivec3 blockPos)
{
  if (Queue.size() >= static_cast<size_t>(Budgets.LiquidQueueHardLimit))
  {
    ++Stats.Dropped;
    Stats.Depth = Queue.size();
    return false;
  }
  if (Queue.size() >= static_cast<size_t>(Budgets.LiquidQueueSoftLimit))
  {
    ++Stats.Deferred;
    Stats.Depth = Queue.size();
    return false;
  }
  if (!Keys.insert(blockPos).second)
  {
    return false;
  }
  Queue.push_back(blockPos);
  ++Stats.Enqueued;
  Stats.Depth = Queue.size();
  return true;
}

std::vector<glm::ivec3> ULiquidUpdateQueue::PopBudgeted()
{
  std::vector<glm::ivec3> out;
  const size_t budget =
      static_cast<size_t>(std::max(0, Budgets.LiquidEventsPerTickMax));
  out.reserve(budget);
  while (!Queue.empty() && out.size() < budget)
  {
    out.push_back(Queue.front());
    Keys.erase(Queue.front());
    Queue.pop_front();
  }
  if (!Queue.empty())
  {
    Stats.Deferred += Queue.size();
  }
  Stats.Processed += out.size();
  Stats.Depth = Queue.size();
  return out;
}

void ULiquidUpdateQueue::Clear()
{
  Queue.clear();
  Keys.clear();
  Stats = LiquidUpdateQueueStats{};
}

} // namespace cutum

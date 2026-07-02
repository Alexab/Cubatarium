#include "World/Physics/LiquidUpdateQueue.h"
#include "World/Physics/PhysicsChunkDistance.h"
#include "World/Physics/PhysicsQueuePurge.h"
#include <algorithm>

namespace cutum
{

bool ULiquidUpdateQueue::TryEvictOne()
{
  if (Queue.empty())
  {
    return false;
  }
  const LiquidQueueEntry worst = Queue.top();
  Queue.pop();
  Keys.erase(worst.BlockPos);
  ++Stats.Purged;
  WarnOncePhysicsQueuePurge("liquid_update");
  return true;
}

bool ULiquidUpdateQueue::Enqueue(glm::ivec3 block_pos)
{
  while (Queue.size() >= static_cast<size_t>(Budgets.LiquidQueueHardLimit))
  {
    if (!TryEvictOne())
    {
      ++Stats.Dropped;
      Stats.Depth = Queue.size();
      return false;
    }
  }
  if (Queue.size() >= static_cast<size_t>(Budgets.LiquidQueueSoftLimit))
  {
    ++Stats.Deferred;
    Stats.Depth = Queue.size();
    return false;
  }
  if (!Keys.insert(block_pos).second)
  {
    return false;
  }

  LiquidQueueEntry entry;
  entry.BlockPos = block_pos;
  entry.DistanceToFocus = ChebyshevBlockDistanceChunks(block_pos, FocusChunk);
  entry.InsertionOrder = ++NextInsertionOrder;
  Queue.push(entry);
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
    out.push_back(Queue.top().BlockPos);
    Keys.erase(Queue.top().BlockPos);
    Queue.pop();
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
  Queue = {};
  Keys.clear();
  NextInsertionOrder = 0;
  Stats = LiquidUpdateQueueStats{};
}

} // namespace cutum

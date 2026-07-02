#include "World/Physics/BlockUpdateQueue.h"
#include <algorithm>
#include <tuple>

namespace cutum
{

bool UBlockUpdateQueue::EventOrder::operator()(const BlockUpdateEvent &lhs,
                                               const BlockUpdateEvent &rhs) const
{
  const auto lk = std::make_tuple(lhs.TriggerTick, static_cast<int>(lhs.Priority),
                                  lhs.ChunkCoord.x, lhs.ChunkCoord.y,
                                  lhs.ChunkCoord.z, lhs.LocalOrder);
  const auto rk = std::make_tuple(rhs.TriggerTick, static_cast<int>(rhs.Priority),
                                  rhs.ChunkCoord.x, rhs.ChunkCoord.y,
                                  rhs.ChunkCoord.z, rhs.LocalOrder);
  return lk > rk;
}

size_t UBlockUpdateQueue::EventKeyHash::operator()(const EventKey &key) const
{
  size_t hash = static_cast<size_t>(key.triggerTick);
  hash ^= static_cast<size_t>(key.x * 73856093);
  hash ^= static_cast<size_t>(key.y * 19349663);
  hash ^= static_cast<size_t>(key.z * 83492791);
  hash ^= static_cast<size_t>(key.type) * 2654435761u;
  return hash;
}

UBlockUpdateQueue::EventKey
UBlockUpdateQueue::BuildKey(const BlockUpdateEvent &event) const
{
  return EventKey{event.BlockPos.x, event.BlockPos.y, event.BlockPos.z, event.Type,
                  event.TriggerTick};
}

bool UBlockUpdateQueue::Enqueue(const BlockUpdateEvent &event)
{
  if (Queue.size() >= static_cast<size_t>(Budgets.BlockQueueHardLimit))
  {
    ++Stats.Dropped;
    Stats.Depth = Queue.size();
    return false;
  }
  if (Queue.size() >= static_cast<size_t>(Budgets.BlockQueueSoftLimit) &&
      event.Priority == BlockUpdatePriority::Low)
  {
    ++Stats.Deferred;
    Stats.Depth = Queue.size();
    return false;
  }

  const EventKey key = BuildKey(event);
  if (!Keys.insert(key).second)
  {
    return false;
  }

  Queue.push(event);
  ++Stats.Enqueued;
  Stats.Depth = Queue.size();
  return true;
}

std::vector<BlockUpdateEvent> UBlockUpdateQueue::PopBudgeted()
{
  std::vector<BlockUpdateEvent> out;
  const size_t budget =
      static_cast<size_t>(std::max(0, Budgets.BlockEventsPerTickMax));
  out.reserve(budget);
  while (!Queue.empty() && out.size() < budget)
  {
    out.push_back(Queue.top());
    Keys.erase(BuildKey(Queue.top()));
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

void UBlockUpdateQueue::Clear()
{
  Queue = {};
  Keys.clear();
  Stats.Depth = 0;
}

} // namespace cutum

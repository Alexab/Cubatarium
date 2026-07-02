#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/PhysicsChunkDistance.h"
#include "World/Physics/PhysicsQueuePurge.h"
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

bool UBlockUpdateQueue::TryEvictOne()
{
  if (Queue.empty())
  {
    return false;
  }
  std::vector<BlockUpdateEvent> items;
  items.reserve(Queue.size());
  while (!Queue.empty())
  {
    items.push_back(Queue.top());
    Queue.pop();
  }
  size_t evict_index = 0;
  for (size_t i = 1; i < items.size(); ++i)
  {
    const BlockUpdateEvent &candidate = items[i];
    const BlockUpdateEvent &worst = items[evict_index];
    const int candidate_dist =
        ChebyshevChunkDistance(candidate.ChunkCoord, FocusChunk);
    const int worst_dist = ChebyshevChunkDistance(worst.ChunkCoord, FocusChunk);
    if (candidate_dist > worst_dist)
    {
      evict_index = i;
      continue;
    }
    if (candidate_dist < worst_dist)
    {
      continue;
    }
    if (static_cast<int>(candidate.Priority) < static_cast<int>(worst.Priority))
    {
      evict_index = i;
      continue;
    }
    if (static_cast<int>(candidate.Priority) == static_cast<int>(worst.Priority) &&
        candidate.LocalOrder > worst.LocalOrder)
    {
      evict_index = i;
    }
  }
  Keys.erase(BuildKey(items[evict_index]));
  items.erase(items.begin() + static_cast<std::ptrdiff_t>(evict_index));
  for (const BlockUpdateEvent &event : items)
  {
    Queue.push(event);
  }
  ++Stats.Purged;
  WarnOncePhysicsQueuePurge("block_update");
  return true;
}

bool UBlockUpdateQueue::Enqueue(const BlockUpdateEvent &event)
{
  while (Queue.size() >= static_cast<size_t>(Budgets.BlockQueueHardLimit))
  {
    if (!TryEvictOne())
    {
      ++Stats.Dropped;
      Stats.Depth = Queue.size();
      return false;
    }
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
  Stats = BlockUpdateQueueStats{};
}

} // namespace cutum

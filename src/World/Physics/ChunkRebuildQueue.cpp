#include "World/Physics/ChunkRebuildQueue.h"
#include "World/Physics/PhysicsChunkDistance.h"
#include "World/Physics/PhysicsQueuePurge.h"
#include <algorithm>

namespace cutum
{

void UChunkRebuildQueue::SetLimits(int per_tick_max, int soft_limit, int hard_limit)
{
  PerTickMax = std::max(0, per_tick_max);
  SoftLimit = std::max(0, soft_limit);
  HardLimit = std::max(SoftLimit, hard_limit);
}

bool UChunkRebuildQueue::TryEvictOne()
{
  if (Queue.empty())
  {
    return false;
  }
  std::vector<Request> items;
  items.reserve(Queue.size());
  while (!Queue.empty())
  {
    items.push_back(Queue.top());
    Queue.pop();
  }
  size_t evict_index = items.size();
  for (size_t i = 0; i < items.size(); ++i)
  {
    if (ProtectLowPriorities && items[i].Priority <= 2)
    {
      continue;
    }
    if (evict_index == items.size())
    {
      evict_index = i;
      continue;
    }
    const Request &candidate = items[i];
    const Request &worst = items[evict_index];
    if (candidate.DistanceToFocus > worst.DistanceToFocus)
    {
      evict_index = i;
      continue;
    }
    if (candidate.DistanceToFocus < worst.DistanceToFocus)
    {
      continue;
    }
    if (candidate.Priority > worst.Priority)
    {
      evict_index = i;
      continue;
    }
    if (candidate.Priority == worst.Priority &&
        candidate.LocalOrder > worst.LocalOrder)
    {
      evict_index = i;
    }
  }
  if (evict_index == items.size())
  {
    for (const Request &request : items)
    {
      Queue.push(request);
    }
    return false;
  }
  Keys.erase(items[evict_index].ChunkCoord);
  items.erase(items.begin() + static_cast<std::ptrdiff_t>(evict_index));
  for (const Request &request : items)
  {
    Queue.push(request);
  }
  ++Stats.Purged;
  WarnOncePhysicsQueuePurge("chunk_rebuild");
  return true;
}

bool UChunkRebuildQueue::Enqueue(glm::ivec3 chunk_coord, int priority,
                                 uint64_t local_order)
{
  while (Queue.size() >= static_cast<size_t>(HardLimit))
  {
    if (!TryEvictOne())
    {
      ++Stats.Dropped;
      Stats.Depth = Queue.size();
      return false;
    }
  }
  if (Queue.size() >= static_cast<size_t>(SoftLimit) && priority > 2)
  {
    ++Stats.Deferred;
    Stats.Depth = Queue.size();
    return false;
  }
  if (!Keys.insert(chunk_coord).second)
  {
    return false;
  }

  Request request;
  request.ChunkCoord = chunk_coord;
  request.Priority = priority;
  request.LocalOrder = local_order;
  request.DistanceToFocus = ChebyshevChunkDistance(chunk_coord, FocusChunk);
  Queue.push(request);
  ++Stats.Enqueued;
  Stats.Depth = Queue.size();
  return true;
}

size_t UChunkRebuildQueue::DrainBudgeted(
    const std::function<void(glm::ivec3)> &consume,
    const std::function<bool()> &may_continue)
{
  size_t processed = 0;
  const size_t budget = static_cast<size_t>(PerTickMax);
  while (!Queue.empty() && processed < budget && may_continue())
  {
    const Request request = Queue.top();
    consume(request.ChunkCoord);
    Queue.pop();
    Keys.erase(request.ChunkCoord);
    ++processed;
    ++Stats.Processed;
    Stats.Depth = Queue.size();
  }
  if (!Queue.empty())
    Stats.Deferred += Queue.size();
  Stats.Depth = Queue.size();
  return processed;
}

std::vector<glm::ivec3> UChunkRebuildQueue::PopBudgeted()
{
  std::vector<glm::ivec3> out;
  out.reserve(static_cast<size_t>(PerTickMax));
  DrainBudgeted([&](glm::ivec3 coord) { out.push_back(coord); },
                [] { return true; });
  return out;
}

void UChunkRebuildQueue::Clear()
{
  while (!Queue.empty())
  {
    Queue.pop();
  }
  Keys.clear();
  Stats = ChunkRebuildQueueStats{};
}

} // namespace cutum

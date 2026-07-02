#include "World/Physics/ChunkRebuildQueue.h"
#include <algorithm>

namespace cutum
{

void UChunkRebuildQueue::SetLimits(int per_tick_max, int soft_limit, int hard_limit)
{
  PerTickMax = std::max(0, per_tick_max);
  SoftLimit = std::max(0, soft_limit);
  HardLimit = std::max(SoftLimit, hard_limit);
}

bool UChunkRebuildQueue::Enqueue(glm::ivec3 chunk_coord, int priority,
                                 uint64_t local_order)
{
  if (Queue.size() >= static_cast<size_t>(HardLimit))
  {
    ++Stats.Dropped;
    Stats.Depth = Queue.size();
    return false;
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
  Queue.push(request);
  ++Stats.Enqueued;
  Stats.Depth = Queue.size();
  return true;
}

std::vector<glm::ivec3> UChunkRebuildQueue::PopBudgeted()
{
  std::vector<glm::ivec3> out;
  const size_t budget = static_cast<size_t>(PerTickMax);
  out.reserve(budget);
  while (!Queue.empty() && out.size() < budget)
  {
    const Request request = Queue.top();
    Queue.pop();
    Keys.erase(request.ChunkCoord);
    out.push_back(request.ChunkCoord);
  }
  if (!Queue.empty())
  {
    Stats.Deferred += Queue.size();
  }
  Stats.Processed += out.size();
  Stats.Depth = Queue.size();
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

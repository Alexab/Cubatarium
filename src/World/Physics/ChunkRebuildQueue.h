#ifndef CHUNKREBUILDQUEUE_H
#define CHUNKREBUILDQUEUE_H

#include "World/Chunks/ChunkManager.h"
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

namespace cutum
{

struct ChunkRebuildQueueStats
{
  uint64_t Enqueued{0};
  uint64_t Processed{0};
  uint64_t Deferred{0};
  uint64_t Dropped{0};
  size_t Depth{0};
};

class UChunkRebuildQueue
{
public:
  void SetLimits(int per_tick_max, int soft_limit, int hard_limit);
  bool Enqueue(glm::ivec3 chunk_coord, int priority, uint64_t local_order);
  std::vector<glm::ivec3> PopBudgeted();
  void Clear();

  size_t Size() const { return Queue.size(); }
  const ChunkRebuildQueueStats &GetStats() const { return Stats; }

private:
  struct Request
  {
    glm::ivec3 ChunkCoord{0};
    int Priority{0};
    uint64_t LocalOrder{0};
  };

  struct RequestOrder
  {
    bool operator()(const Request &lhs, const Request &rhs) const
    {
      if (lhs.Priority != rhs.Priority)
      {
        return lhs.Priority > rhs.Priority;
      }
      return lhs.LocalOrder > rhs.LocalOrder;
    }
  };

  int PerTickMax{8};
  int SoftLimit{512};
  int HardLimit{4096};
  std::priority_queue<Request, std::vector<Request>, RequestOrder> Queue;
  std::unordered_set<glm::ivec3, IVec3Hash> Keys;
  ChunkRebuildQueueStats Stats;
};

} // namespace cutum

#endif // CHUNKREBUILDQUEUE_H

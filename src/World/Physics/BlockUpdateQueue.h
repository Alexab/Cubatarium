#ifndef BLOCKUPDATEQUEUE_H
#define BLOCKUPDATEQUEUE_H

#include "World/Physics/BlockUpdateEvent.h"
#include "World/Physics/PhysicsProfile.h"
#include <queue>
#include <unordered_set>
#include <vector>

namespace cutum
{

struct BlockUpdateQueueStats
{
  uint64_t Enqueued{0};
  uint64_t Processed{0};
  uint64_t Deferred{0};
  uint64_t Dropped{0};
  size_t Depth{0};
};

class UBlockUpdateQueue
{
public:
  void SetBudgets(const PhysicsBudgets &budgets) { Budgets = budgets; }
  bool Enqueue(const BlockUpdateEvent &event);
  std::vector<BlockUpdateEvent> PopBudgeted();
  void Clear();

  size_t Size() const { return Queue.size(); }
  const BlockUpdateQueueStats &GetStats() const { return Stats; }

private:
  struct EventOrder
  {
    bool operator()(const BlockUpdateEvent &lhs,
                    const BlockUpdateEvent &rhs) const;
  };

  struct EventKey
  {
    int x{0};
    int y{0};
    int z{0};
    BlockUpdateEventType type{BlockUpdateEventType::BlockChanged};
    uint64_t triggerTick{0};
    bool operator==(const EventKey &other) const
    {
      return x == other.x && y == other.y && z == other.z &&
             type == other.type && triggerTick == other.triggerTick;
    }
  };

  struct EventKeyHash
  {
    size_t operator()(const EventKey &key) const;
  };

  EventKey BuildKey(const BlockUpdateEvent &event) const;

  PhysicsBudgets Budgets;
  std::priority_queue<BlockUpdateEvent, std::vector<BlockUpdateEvent>, EventOrder>
      Queue;
  std::unordered_set<EventKey, EventKeyHash> Keys;
  BlockUpdateQueueStats Stats;
};

} // namespace cutum

#endif // BLOCKUPDATEQUEUE_H

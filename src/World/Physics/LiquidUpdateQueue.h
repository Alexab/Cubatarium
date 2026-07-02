#ifndef LIQUIDUPDATEQUEUE_H
#define LIQUIDUPDATEQUEUE_H

#include "World/Physics/PhysicsProfile.h"
#include <glm/glm.hpp>
#include <deque>
#include <unordered_set>
#include <vector>

namespace cutum
{

struct LiquidUpdateQueueStats
{
  uint64_t Enqueued{0};
  uint64_t Processed{0};
  uint64_t Deferred{0};
  uint64_t Dropped{0};
  size_t Depth{0};
};

class ULiquidUpdateQueue
{
public:
  void SetBudgets(const PhysicsBudgets &budgets) { Budgets = budgets; }
  bool Enqueue(glm::ivec3 blockPos);
  std::vector<glm::ivec3> PopBudgeted();
  void Clear();
  size_t Size() const { return Queue.size(); }
  const LiquidUpdateQueueStats &GetStats() const { return Stats; }

private:
  struct IVec3Hash
  {
    size_t operator()(const glm::ivec3 &v) const
    {
      size_t hash = static_cast<size_t>(v.x * 73856093);
      hash ^= static_cast<size_t>(v.y * 19349663);
      hash ^= static_cast<size_t>(v.z * 83492791);
      return hash;
    }
  };

  PhysicsBudgets Budgets;
  std::deque<glm::ivec3> Queue;
  std::unordered_set<glm::ivec3, IVec3Hash> Keys;
  LiquidUpdateQueueStats Stats;
};

} // namespace cutum

#endif // LIQUIDUPDATEQUEUE_H

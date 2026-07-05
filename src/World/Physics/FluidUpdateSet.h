#ifndef FLUIDUPDATESET_H
#define FLUIDUPDATESET_H

#include "World/Physics/PhysicsProfile.h"
#include <deque>
#include <glm/glm.hpp>
#include <unordered_set>
#include <vector>

namespace cutum
{

class UBlockWorld;
class UBlockDefinitionStorage;

struct FluidUpdateSetStats
{
  uint64_t Enqueued{0};
  uint64_t Processed{0};
  uint64_t Dropped{0};
  size_t Depth{0};
};

class UFluidUpdateSet
{
public:
  void SetBudgets(const PhysicsBudgets &budgets) { Budgets = budgets; }
  bool Enqueue(glm::ivec3 block_pos);
  void EnqueueFrontier(const UBlockWorld &world,
                       const UBlockDefinitionStorage &definitions,
                       glm::ivec3 center, int radius_blocks);
  std::vector<glm::ivec3> PopBudgeted();
  const FluidUpdateSetStats &GetStats() const { return Stats; }
  void Clear();
  size_t Size() const { return Keys.size(); }

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
  std::unordered_set<glm::ivec3, IVec3Hash> Keys;
  std::deque<glm::ivec3> Fifo;
  FluidUpdateSetStats Stats;
};

} // namespace cutum

#endif

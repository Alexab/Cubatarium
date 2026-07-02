#include "World/Physics/FluidUpdateSet.h"

#include "Blocks/BlockDefinitionStorage.h"
#include "World/Core/BlockWorld.h"

#include <algorithm>

namespace cutum
{

bool UFluidUpdateSet::Enqueue(glm::ivec3 block_pos)
{
  if (Keys.find(block_pos) != Keys.end())
  {
    return false;
  }
  if (Keys.size() >= static_cast<size_t>(Budgets.LiquidQueueHardLimit))
  {
    ++Stats.Dropped;
    return false;
  }
  Keys.insert(block_pos);
  Order.push_back({NextInsertionOrder++, block_pos});
  ++Stats.Enqueued;
  Stats.Depth = Keys.size();
  return true;
}

void UFluidUpdateSet::EnqueueFrontier(
    const UBlockWorld &world, const UBlockDefinitionStorage &definitions,
    glm::ivec3 center, int radius_blocks)
{
  const int radius = std::max(0, radius_blocks);
  for (int dx = -radius; dx <= radius; ++dx)
  {
    for (int dy = -radius; dy <= radius; ++dy)
    {
      for (int dz = -radius; dz <= radius; ++dz)
      {
        const glm::ivec3 pos(center.x + dx, center.y + dy, center.z + dz);
        const BlockId id = world.GetBlock(pos);
        if (const BlockDefinition *def = definitions.GetById(id))
        {
          if (def->Physics.IsLiquid)
          {
            Enqueue(pos);
          }
        }
      }
    }
  }
}

std::vector<glm::ivec3> UFluidUpdateSet::PopBudgeted()
{
  const int budget = std::max(
      0, Budgets.FluidBlocksPerTickMax > 0 ? Budgets.FluidBlocksPerTickMax
                                             : Budgets.LiquidEventsPerTickMax);
  std::vector<glm::ivec3> out;
  out.reserve(static_cast<size_t>(budget));
  std::sort(Order.begin(), Order.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  size_t index = 0;
  while (index < Order.size() && static_cast<int>(out.size()) < budget)
  {
    const glm::ivec3 pos = Order[index].second;
    ++index;
    if (Keys.erase(pos) == 0)
    {
      continue;
    }
    out.push_back(pos);
    ++Stats.Processed;
  }
  if (index > 0)
  {
    Order.erase(Order.begin(), Order.begin() + static_cast<ptrdiff_t>(index));
  }
  Stats.Depth = Keys.size();
  return out;
}

void UFluidUpdateSet::Clear()
{
  Keys.clear();
  Order.clear();
  NextInsertionOrder = 0;
  Stats = {};
}

} // namespace cutum

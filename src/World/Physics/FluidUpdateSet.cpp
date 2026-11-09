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
    // W6.3: remember the drop for retry instead of losing the cell.
    if (DroppedRetry.size() < kMaxDroppedRetry)
    {
      DroppedRetry.push_back(block_pos);
    }
    return false;
  }
  Keys.insert(block_pos);
  Fifo.push_back(block_pos);
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
  while (!Fifo.empty() && static_cast<int>(out.size()) < budget)
  {
    const glm::ivec3 pos = Fifo.front();
    Fifo.pop_front();
    if (Keys.erase(pos) == 0)
    {
      continue;
    }
    out.push_back(pos);
    ++Stats.Processed;
  }
  // W6.3: once the pressure is gone, give the dropped cells another turn.
  const size_t half_limit =
      static_cast<size_t>(std::max(1, Budgets.LiquidQueueHardLimit)) / 2;
  if (!DroppedRetry.empty() && Keys.size() < half_limit)
  {
    size_t retried = 0;
    while (!DroppedRetry.empty() && retried < kRetryBatchMax)
    {
      const glm::ivec3 pos = DroppedRetry.front();
      DroppedRetry.pop_front();
      // The retry re-enters Enqueue; if the queue is full again the position
      // lands back in DroppedRetry via the bounded push above.
      if (Enqueue(pos))
      {
        ++retried;
      }
    }
  }
  Stats.Depth = Keys.size();
  return out;
}

void UFluidUpdateSet::Clear()
{
  Keys.clear();
  Fifo.clear();
  DroppedRetry.clear();
  Stats = {};
}

} // namespace cutum

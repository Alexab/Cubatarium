#include "World/Physics/BlockUpdateQueue.h"
#include "World/Physics/ChunkRebuildQueue.h"
#include "World/Physics/FluidUpdateSet.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "physics_budget_guard_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 2;
  budgets.BlockQueueSoftLimit = 4;
  budgets.BlockQueueHardLimit = 8;
  budgets.LiquidEventsPerTickMax = 2;
  budgets.LiquidQueueSoftLimit = 8;
  budgets.LiquidQueueHardLimit = 8;
  budgets.VisualRemeshPerTickMax = 2;
  budgets.VisualRemeshQueueSoftLimit = 8;
  budgets.VisualRemeshQueueHardLimit = 8;
  budgets.CollisionRebuildPerTickMax = 2;
  budgets.CollisionRebuildQueueSoftLimit = 8;
  budgets.CollisionRebuildQueueHardLimit = 8;

  cutum::UBlockUpdateQueue block_queue;
  cutum::UFluidUpdateSet liquid_queue;
  cutum::UChunkRebuildQueue visual_queue;
  cutum::UChunkRebuildQueue collision_queue;
  block_queue.SetBudgets(budgets);
  liquid_queue.SetBudgets(budgets);
  visual_queue.SetLimits(budgets.VisualRemeshPerTickMax,
                         budgets.VisualRemeshQueueSoftLimit,
                         budgets.VisualRemeshQueueHardLimit);
  collision_queue.SetLimits(budgets.CollisionRebuildPerTickMax,
                            budgets.CollisionRebuildQueueSoftLimit,
                            budgets.CollisionRebuildQueueHardLimit);

  cutum::BlockUpdateEvent event;
  event.Type = cutum::BlockUpdateEventType::BlockChanged;
  event.Priority = cutum::BlockUpdatePriority::High;
  for (int i = 0; i < 32; ++i)
  {
    event.BlockPos = glm::ivec3(i, 0, 0);
    event.TriggerTick = static_cast<uint64_t>(i);
    event.LocalOrder = static_cast<uint64_t>(i);
    block_queue.Enqueue(event);
    liquid_queue.Enqueue(glm::ivec3(i, 0, 0));
    visual_queue.Enqueue(glm::ivec3(i, 0, 0), 0, static_cast<uint64_t>(i));
    collision_queue.Enqueue(glm::ivec3(i, 0, 0), 0, static_cast<uint64_t>(i));
  }

  Expect(block_queue.GetStats().Purged > 0,
         "block queue must purge at hard limit");
  Expect(liquid_queue.GetStats().Dropped > 0,
         "liquid queue must drop at hard limit");
  Expect(block_queue.Size() <= static_cast<size_t>(budgets.BlockQueueHardLimit),
         "block queue depth must stay within hard limit");
  Expect(liquid_queue.Size() <= static_cast<size_t>(budgets.LiquidQueueHardLimit),
         "liquid queue depth must stay within hard limit");

  size_t max_block_depth = block_queue.Size();
  size_t max_liquid_depth = liquid_queue.Size();
  for (int tick = 0; tick < 64; ++tick)
  {
    block_queue.PopBudgeted();
    liquid_queue.PopBudgeted();
    visual_queue.PopBudgeted();
    collision_queue.PopBudgeted();
    max_block_depth = std::max(max_block_depth, block_queue.Size());
    max_liquid_depth = std::max(max_liquid_depth, liquid_queue.Size());
  }

  Expect(max_block_depth <= static_cast<size_t>(budgets.BlockQueueHardLimit),
         "block queue must not grow beyond hard limit while draining");
  Expect(max_liquid_depth <= static_cast<size_t>(budgets.LiquidQueueHardLimit),
         "liquid queue must not grow beyond hard limit while draining");
  Expect(block_queue.GetStats().Processed > 0, "block queue must process events");

  std::cout << "physics_budget_guard_test: OK" << std::endl;
  return 0;
}

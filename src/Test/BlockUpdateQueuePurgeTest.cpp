#include "World/Physics/BlockUpdateQueue.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_update_queue_purge_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UBlockUpdateQueue queue;
  cutum::PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 16;
  budgets.BlockQueueSoftLimit = 8;
  budgets.BlockQueueHardLimit = 4;
  queue.SetBudgets(budgets);
  queue.SetFocusChunk(glm::ivec3(0, 0, 0));

  cutum::BlockUpdateEvent far_event;
  far_event.Type = cutum::BlockUpdateEventType::BlockChanged;
  far_event.Priority = cutum::BlockUpdatePriority::Low;
  far_event.BlockPos = glm::ivec3(100, 0, 0);
  far_event.ChunkCoord = glm::ivec3(6, 0, 0);
  far_event.TriggerTick = 1;
  far_event.LocalOrder = 1;

  for (int i = 0; i < 4; ++i)
  {
  cutum::BlockUpdateEvent ev = far_event;
  ev.BlockPos.x = 100 + i;
  ev.ChunkCoord.x = 6 + i;
  ev.LocalOrder = static_cast<uint64_t>(i);
  Expect(queue.Enqueue(ev), "expected far enqueue");
  }

  cutum::BlockUpdateEvent near_event = far_event;
  near_event.Priority = cutum::BlockUpdatePriority::Critical;
  near_event.BlockPos = glm::ivec3(0, 0, 0);
  near_event.ChunkCoord = glm::ivec3(0, 0, 0);
  near_event.LocalOrder = 99;
  Expect(queue.Enqueue(near_event), "near critical should evict and enqueue");
  Expect(queue.GetStats().Purged >= 1, "expected purge when at hard limit");

  const std::vector<cutum::BlockUpdateEvent> popped = queue.PopBudgeted();
  Expect(!popped.empty(), "expected popped events");
  Expect(popped.front().Priority == cutum::BlockUpdatePriority::Critical,
         "near critical should be processed first");

  std::cout << "block_update_queue_purge_test: OK" << std::endl;
  return 0;
}

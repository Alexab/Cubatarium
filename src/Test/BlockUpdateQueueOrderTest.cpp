#include "World/Physics/BlockUpdateQueue.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "block_update_queue_order_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UBlockUpdateQueue queue;
  cutum::PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 16;
  queue.SetBudgets(budgets);

  cutum::BlockUpdateEvent low;
  low.Type = cutum::BlockUpdateEventType::BlockChanged;
  low.Priority = cutum::BlockUpdatePriority::Low;
  low.BlockPos = glm::ivec3(0, 0, 0);
  low.ChunkCoord = glm::ivec3(0, 0, 0);
  low.TriggerTick = 10;
  low.LocalOrder = 2;

  cutum::BlockUpdateEvent high = low;
  high.Priority = cutum::BlockUpdatePriority::Critical;
  high.LocalOrder = 1;
  high.BlockPos = glm::ivec3(1, 0, 0);

  Expect(queue.Enqueue(low), "expected enqueue low event");
  Expect(queue.Enqueue(high), "expected enqueue high event");

  const std::vector<cutum::BlockUpdateEvent> events = queue.PopBudgeted();
  Expect(events.size() == 2, "expected two queued events");
  Expect(events[0].Priority == cutum::BlockUpdatePriority::Critical,
         "critical priority must be processed first");

  // Dedup check: same position/type/tick should be ignored.
  Expect(queue.Enqueue(low), "expected enqueue low event again");
  Expect(!queue.Enqueue(low), "expected duplicate event rejection");

  cutum::UBlockUpdateQueue softQueue;
  cutum::PhysicsBudgets softBudgets;
  softBudgets.BlockEventsPerTickMax = 16;
  softBudgets.BlockQueueSoftLimit = 1;
  softBudgets.BlockQueueHardLimit = 8;
  softQueue.SetBudgets(softBudgets);

  cutum::BlockUpdateEvent softLow = low;
  softLow.LocalOrder = 10;
  softLow.BlockPos = glm::ivec3(2, 0, 0);
  Expect(softQueue.Enqueue(softLow), "expected first low-priority enqueue");

  cutum::BlockUpdateEvent softLow2 = softLow;
  softLow2.LocalOrder = 11;
  softLow2.BlockPos = glm::ivec3(3, 0, 0);
  Expect(!softQueue.Enqueue(softLow2),
         "expected soft-limit defer for low-priority event");

  std::cout << "block_update_queue_order_test: OK" << std::endl;
  return 0;
}

#include "World/Physics/FluidUpdateSet.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_queue_priority_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UFluidUpdateSet queue;
  cutum::PhysicsBudgets budgets;
  budgets.LiquidEventsPerTickMax = 4;
  budgets.LiquidQueueSoftLimit = 16;
  budgets.LiquidQueueHardLimit = 32;
  queue.SetBudgets(budgets);

  Expect(queue.Enqueue(glm::ivec3(50, 0, 0)), "enqueue far");
  Expect(queue.Enqueue(glm::ivec3(0, 0, 0)), "enqueue near");

  const std::vector<glm::ivec3> popped = queue.PopBudgeted();
  Expect(popped.size() == 2, "expected two entries");
  Expect(popped[0] == glm::ivec3(50, 0, 0), "fifo fluid queue order");

  std::cout << "liquid_queue_priority_test: OK" << std::endl;
  return 0;
}

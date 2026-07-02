#include "World/Physics/FluidUpdateSet.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_queue_backpressure_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  cutum::UFluidUpdateSet queue;
  cutum::PhysicsBudgets budgets;
  budgets.FluidBlocksPerTickMax = 2;
  budgets.LiquidQueueSoftLimit = 6;
  budgets.LiquidQueueHardLimit = 6;
  queue.SetBudgets(budgets);

  for (int i = 0; i < 6; ++i)
  {
    Expect(queue.Enqueue(glm::ivec3(i, 0, 0)), "fill to hard limit");
  }
  Expect(!queue.Enqueue(glm::ivec3(99, 0, 0)),
         "enqueue beyond hard limit should fail");
  Expect(queue.GetStats().Dropped > 0, "hard limit should drop overflow");
  Expect(queue.Size() <= static_cast<size_t>(budgets.LiquidQueueHardLimit),
         "queue depth must respect hard limit");

  const std::vector<glm::ivec3> batch = queue.PopBudgeted();
  Expect(!batch.empty(), "budgeted pop should drain some events");
  Expect(queue.GetStats().Processed >= batch.size(),
         "processed counter should advance");

  std::cout << "liquid_queue_backpressure_test: OK" << std::endl;
  return 0;
}

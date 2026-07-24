#include "World/Physics/Replay/ReplayInputLog.h"
#include "World/Physics/Replay/ReplayWorldFixture.h"

#include <cstdlib>
#include <iostream>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "physics_profile_hash_test: " << message << std::endl;
    std::exit(1);
  }
}

static bool CompareRuns(const std::vector<cutum::ReplayTickHash> &lhs,
                        const std::vector<cutum::ReplayTickHash> &rhs)
{
  if (lhs.size() != rhs.size())
  {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i)
  {
    if (lhs[i].StateHash != rhs[i].StateHash)
    {
      return false;
    }
  }
  return true;
}

static cutum::PhysicsBudgets PrimitiveReplayBudgets()
{
  cutum::PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 2;
  budgets.LiquidEventsPerTickMax = 2;
  budgets.BlockQueueSoftLimit = 8;
  budgets.BlockQueueHardLimit = 16;
  budgets.LiquidQueueSoftLimit = 6;
  budgets.LiquidQueueHardLimit = 12;
  return budgets;
}

static cutum::PhysicsBudgets StandardReplayBudgets()
{
  cutum::PhysicsBudgets budgets;
  budgets.BlockEventsPerTickMax = 128;
  budgets.LiquidEventsPerTickMax = 128;
  return budgets;
}

static std::vector<cutum::ReplayTickHash>
RunProfileScenario(const cutum::PhysicsBudgets &budgets)
{
  cutum::UReplayWorldFixture fixture;
  fixture.GetBlockWorld().SetBlock(glm::ivec3(3, 2, 1), 5);

  cutum::UReplayInputLog log;
  log.SetBudgets(budgets);
  log.LoadGoldenScenario();
  return log.Run(&fixture.GetBlockWorld(), glm::ivec3(-16, -16, -16),
                 glm::ivec3(16, 16, 16));
}

int main()
{
  const auto primitive_a = RunProfileScenario(PrimitiveReplayBudgets());
  const auto primitive_b = RunProfileScenario(PrimitiveReplayBudgets());
  Expect(CompareRuns(primitive_a, primitive_b),
         "primitive replay hashes must be deterministic");

  const auto standard_a = RunProfileScenario(StandardReplayBudgets());
  const auto standard_b = RunProfileScenario(StandardReplayBudgets());
  Expect(CompareRuns(standard_a, standard_b),
         "standard replay hashes must be deterministic");

  Expect(!primitive_a.empty() && !standard_a.empty(),
         "both profiles must produce tick hashes");

  std::cout << "physics_profile_hash_test: OK (primitive=" << primitive_a.size()
            << " standard=" << standard_a.size() << " ticks)" << std::endl;
  return 0;
}

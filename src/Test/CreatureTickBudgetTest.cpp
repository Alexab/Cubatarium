#include "Activity/CreatureTickBudget.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "creature_tick_budget_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  using cutum::UCreatureTickBudget;

  Expect(UCreatureTickBudget::BudgetFor(10) == 10, "small worlds tick everyone");
  Expect(UCreatureTickBudget::BudgetFor(200) == 50, "a quarter of a crowd");

  // 200 creatures of mixed behaviours: under stress every creature is ticked
  // within 4 ticks, none twice before the others, and each gets the time it
  // missed.
  std::vector<uint64_t> ids;
  for (uint64_t id = 1; id <= 200; ++id)
  {
    ids.push_back(id);
  }
  UCreatureTickBudget budget;
  std::map<uint64_t, int> ticks;
  std::map<uint64_t, float> lastDt;
  for (int tick = 0; tick < 4; ++tick)
  {
    for (const auto &[id, dt] : budget.Select(ids, 0.05f,
                                              UCreatureTickBudget::BudgetFor(ids.size())))
    {
      ++ticks[id];
      lastDt[id] = dt;
    }
  }
  Expect(ticks.size() == 200, "everyone ticked within 4 stress ticks");
  for (const auto &[id, count] : ticks)
  {
    Expect(count == 1, "nobody ticked twice before the others");
  }
  Expect(std::abs(lastDt[1] - 0.05f) < 1e-5f, "first slice owed one tick");
  Expect(std::abs(lastDt[200] - 0.20f) < 1e-5f,
         "last slice gets the four ticks it waited for");

  // The cursor survives creatures joining and leaving.
  ids.erase(ids.begin(), ids.begin() + 10);
  ids.push_back(500);
  const auto next = budget.Select(ids, 0.05f, 5);
  Expect(next[0].first == 500 && next[1].first == 11,
         "a newcomer after the cursor goes next, then it wraps to the lowest live id");
  budget.Remove(12);

  // Long waits are capped.
  UCreatureTickBudget slow;
  const std::vector<uint64_t> crowd(ids.begin(), ids.end());
  for (int i = 0; i < 30; ++i)
  {
    slow.Select(crowd, 0.05f, 1);
  }
  float maxDt = 0.0f;
  for (const auto &[id, dt] : slow.Select(crowd, 0.05f, crowd.size()))
  {
    maxDt = std::max(maxDt, dt);
  }
  Expect(maxDt <= UCreatureTickBudget::kMaxCreatureDt + 1e-6f,
         "time owed is capped");

  std::cout << "creature_tick_budget_test: ok" << std::endl;
  return 0;
}

#include "World/Physics/LiquidSimulationSystem.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_viscosity_gate_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const glm::ivec3 pos(1, 5, 2);
  int allowed_ticks = 0;
  for (uint64_t tick = 0; tick < 2; ++tick)
  {
    if (cutum::ULiquidSimulationSystem::ShouldProcessLiquidTick(tick, pos, 2.0f))
    {
      ++allowed_ticks;
    }
  }

  Expect(allowed_ticks == 1, "viscosity=2 should allow only one tick in two");
  Expect(cutum::ULiquidSimulationSystem::ShouldProcessLiquidTick(0, pos, 1.0f),
         "viscosity=1 should always allow tick 0");
  Expect(cutum::ULiquidSimulationSystem::ShouldProcessLiquidTick(1, pos, 1.0f),
         "viscosity=1 should always allow tick 1");

  std::cout << "liquid_viscosity_gate_test: OK" << std::endl;
  return 0;
}

#include "World/Physics/FluidSpreadSystem.h"
#include <glm/glm.hpp>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "liquid_viscosity_gate_test: " << message << std::endl;
    std::exit(1);
  }
}

static uint32_t FluidPhase(glm::ivec3 block_pos, int spread_period)
{
  const int period = spread_period > 0 ? spread_period : 1;
  const uint32_t x = static_cast<uint32_t>(block_pos.x);
  const uint32_t y = static_cast<uint32_t>(block_pos.y);
  const uint32_t z = static_cast<uint32_t>(block_pos.z);
  return (x * 73856093u ^ y * 19349663u ^ z * 83492791u) %
         static_cast<uint32_t>(period);
}

int main()
{
  const glm::ivec3 pos(3, 10, 7);

  int lava_hits = 0;
  for (uint64_t tick = 0; tick < 60; ++tick)
  {
    if (cutum::UFluidSpreadSystem::ShouldProcessFluidTick(tick, pos, 30))
    {
      ++lava_hits;
    }
  }
  Expect(lava_hits == 2, "lava period 30 should fire twice in 60 ticks");

  const uint32_t water_phase = FluidPhase(pos, 5);
  uint64_t aligned_tick = 0;
  for (uint64_t tick = 0; tick < 5; ++tick)
  {
    if (cutum::UFluidSpreadSystem::ShouldProcessFluidTick(tick, pos, 5))
    {
      aligned_tick = tick;
      break;
    }
  }
  Expect(cutum::UFluidSpreadSystem::ShouldProcessFluidTick(aligned_tick, pos, 5),
         "water should process when tick aligns with spatial phase");
  Expect(!cutum::UFluidSpreadSystem::ShouldProcessFluidTick(
             (aligned_tick + 1) % 5, pos, 5),
         "water should skip off-phase ticks");
  (void)water_phase;

  std::cout << "liquid_viscosity_gate_test: OK" << std::endl;
  return 0;
}

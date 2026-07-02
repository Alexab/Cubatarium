#ifndef LIQUIDSIMULATIONSYSTEM_H
#define LIQUIDSIMULATIONSYSTEM_H

#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace cutum
{

class UWorld;
class UBlockWorld;
class UBlockRegistry;
class UBlockDefinitionStorage;

struct LiquidSimulationStats
{
  uint64_t Candidates{0};
  uint64_t Applied{0};
  uint64_t Deferred{0};
  uint64_t Dropped{0};
  glm::ivec3 AppliedDest{0};
  bool HasAppliedDest{false};
  bool SourceCleared{false};
};

class ULiquidSimulationSystem
{
public:
  bool ShadowMode{true};
  bool DebugTraceEnabled{false};
  static bool ShouldProcessLiquidTick(uint64_t physics_tick, glm::ivec3 blockPos,
                                      float viscosity)
  {
    const int period =
        std::max(1, static_cast<int>(std::ceil(viscosity)));
    const uint32_t x = static_cast<uint32_t>(blockPos.x);
    const uint32_t y = static_cast<uint32_t>(blockPos.y);
    const uint32_t z = static_cast<uint32_t>(blockPos.z);
    const uint32_t phase =
        (x * 73856093u ^ y * 19349663u ^ z * 83492791u) %
        static_cast<uint32_t>(period);
    return (physics_tick + phase) % static_cast<uint32_t>(period) == 0;
  }
  static bool HasFlowTarget(UWorld &world, glm::ivec3 blockPos);
  LiquidSimulationStats Tick(UWorld &world, glm::ivec3 blockPos);
  LiquidSimulationStats TickBlock(UBlockWorld &blockWorld,
                                  const UBlockRegistry &registry,
                                  uint64_t physics_tick, glm::ivec3 blockPos);
  LiquidSimulationStats TickBlock(UBlockWorld &blockWorld,
                                  const UBlockDefinitionStorage &definitions,
                                  uint64_t physics_tick, glm::ivec3 blockPos);
};

} // namespace cutum

#endif // LIQUIDSIMULATIONSYSTEM_H

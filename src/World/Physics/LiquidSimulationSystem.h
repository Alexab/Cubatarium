#ifndef LIQUIDSIMULATIONSYSTEM_H
#define LIQUIDSIMULATIONSYSTEM_H

#include <glm/glm.hpp>
#include <cstdint>

namespace cutum
{

class UWorld;

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
  static bool HasFlowTarget(UWorld &world, glm::ivec3 blockPos);
  LiquidSimulationStats Tick(UWorld &world, glm::ivec3 blockPos);
};

} // namespace cutum

#endif // LIQUIDSIMULATIONSYSTEM_H

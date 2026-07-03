#ifndef FLUIDSPREADSYSTEM_H
#define FLUIDSPREADSYSTEM_H

#include "World/Physics/FluidSpreadTypes.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace cutum
{

class UWorld;
class UBlockWorld;
class UBlockRegistry;
class UBlockDefinitionStorage;

class UFluidSpreadSystem
{
public:
  bool ShadowMode{true};

  static bool HasSpreadTarget(const UBlockWorld &world,
                              const UBlockDefinitionStorage &definitions,
                              glm::ivec3 block_pos);

  static bool CanReceiveFluid(const UBlockWorld &blockWorld,
                              const UBlockRegistry &registry,
                              glm::ivec3 pos);
  static bool ShouldReplaceBlockWithFluid(const UBlockWorld &blockWorld,
                                          const UBlockRegistry &registry,
                                          glm::ivec3 pos);
  static bool CanReceiveFluid(const UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              glm::ivec3 pos);
  static bool ShouldReplaceBlockWithFluid(const UBlockWorld &blockWorld,
                                          const UBlockDefinitionStorage &definitions,
                                          glm::ivec3 pos);

  FluidSpreadStats Tick(UWorld &world, glm::ivec3 block_pos);
  FluidSpreadStats TickBlock(UBlockWorld &blockWorld,
                             const UBlockRegistry &registry,
                             uint64_t physics_tick, glm::ivec3 block_pos);
  FluidSpreadStats TickBlock(UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             uint64_t physics_tick, glm::ivec3 block_pos);

  static bool ShouldProcessFluidTick(uint64_t physics_tick,
                                     glm::ivec3 block_pos, int spread_period);

private:
};

} // namespace cutum

#endif

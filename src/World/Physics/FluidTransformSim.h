#ifndef FLUIDTRANSFORMSIM_H
#define FLUIDTRANSFORMSIM_H

#include "World/Physics/FluidSpreadTypes.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace cutum
{

class UBlockWorld;
class UBlockDefinitionStorage;

class UFluidTransformSim
{
public:
  static FluidSpreadStats TickBlock(UBlockWorld &blockWorld,
                                    const UBlockDefinitionStorage &definitions,
                                    uint64_t physics_tick, glm::ivec3 block_pos,
                                    int sea_level, bool shadow_mode);
};

} // namespace cutum

#endif

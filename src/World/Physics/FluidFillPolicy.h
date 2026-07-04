#ifndef FLUIDFILLPOLICY_H
#define FLUIDFILLPOLICY_H

#include "World/Math/BlockTypes.h"
#include "World/Math/FluidCellState.h"
#include <glm/glm.hpp>

namespace cutum
{

class UBlockWorld;
class UBlockDefinitionStorage;

class UFluidFillPolicy
{
public:
  static bool CanReceiveFluid(const UBlockWorld &blockWorld,
                              const UBlockDefinitionStorage &definitions,
                              glm::ivec3 pos);
  static bool ShouldReplaceBlockWithFluid(const UBlockWorld &blockWorld,
                                          const UBlockDefinitionStorage &definitions,
                                          glm::ivec3 pos);
  static FluidCellState StoredFluidStateForCell(const UBlockWorld &blockWorld,
                                                const UBlockDefinitionStorage &definitions,
                                                glm::ivec3 pos,
                                                FluidCellState state);
  static void ApplyFluidFill(UBlockWorld &blockWorld,
                             const UBlockDefinitionStorage &definitions,
                             glm::ivec3 pos, BlockId fluid_id,
                             FluidCellState state);
};

} // namespace cutum

#endif

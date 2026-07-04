#pragma once

#include "Blocks/BlockDefinition.h"
#include "World/Math/BlockTypes.h"

namespace cutum
{

inline bool IsFluidPermeableFromDefinition(BlockId id, const BlockDefinition *def,
                                           bool is_liquid)
{
  if (id == BLOCK_AIR || is_liquid)
  {
    return false;
  }
  if (!def)
  {
    return false;
  }
  if (def->Physics.Movement.Occupancy >= 1.0f)
  {
    return false;
  }
  return def->Render.Style == BlockRenderStyle::Cross ||
         def->Render.Style == BlockRenderStyle::Cutout;
}

} // namespace cutum

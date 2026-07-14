#pragma once

#include "Blocks/BlockDefinition.h"

namespace cutum
{

inline FluidKind FluidKindFromDefinition(const BlockDefinition *def)
{
  if (!def || !def->Physics.IsLiquid)
  {
    return FluidKind::None;
  }
  if (def->Physics.FluidKindPreset != FluidKind::None)
  {
    return def->Physics.FluidKindPreset;
  }
  return def->Physics.FluidMaxLevel >= 7 ? FluidKind::Water : FluidKind::Lava;
}

inline bool IsWaterFluidDefinition(const BlockDefinition *def)
{
  return FluidKindFromDefinition(def) == FluidKind::Water;
}

} // namespace cutum

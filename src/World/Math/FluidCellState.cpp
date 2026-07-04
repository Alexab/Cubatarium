#include "World/Math/FluidCellState.h"

#include <cassert>

namespace cutum
{

FluidCellState FluidCellState::Source()
{
  FluidCellState state{};
  state.Level = 0;
  state.Falling = 0;
  state.Kind = 0;
  return state;
}

FluidCellState FluidCellState::Flowing(uint8_t level, bool falling)
{
  assert(level >= 1 && level <= FLUID_LEVEL_MAX);
  FluidCellState state{};
  state.Level = static_cast<uint8_t>(level & 7u);
  state.Falling = falling ? 1u : 0u;
  state.Kind = 0;
  return state;
}

uint8_t PackFluidCellState(const FluidCellState &state)
{
  return static_cast<uint8_t>((state.Level & 7u) | (state.Falling ? 0x08u : 0u) |
                              ((static_cast<uint8_t>(state.Kind) & 15u) << 4));
}

FluidCellState UnpackFluidCellState(uint8_t packed)
{
  FluidCellState state{};
  state.Level = static_cast<uint8_t>(packed & 7u);
  state.Falling = static_cast<uint8_t>((packed & 0x08u) ? 1u : 0u);
  state.Kind = static_cast<uint8_t>((packed >> 4) & 15u);
  return state;
}

bool FluidCellHasActiveFluid(uint8_t packed)
{
  return (packed & 0x0Fu) != 0 || ((packed >> 4) & 15u) != 0;
}

} // namespace cutum

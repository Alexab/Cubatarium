#include "World/Math/FluidCellState.h"

#include <cassert>

namespace cutum
{

FluidCellState FluidCellState::Source()
{
  FluidCellState state{};
  state.Level = 0;
  state.Falling = 0;
  return state;
}

FluidCellState FluidCellState::Flowing(uint8_t level, bool falling)
{
  assert(level >= 1 && level <= FLUID_LEVEL_MAX);
  FluidCellState state{};
  state.Level = static_cast<uint8_t>(level & 7u);
  state.Falling = falling ? 1u : 0u;
  return state;
}

uint8_t PackFluidCellState(const FluidCellState &state)
{
  return static_cast<uint8_t>((state.Level & 7u) | (state.Falling ? 0x08u : 0u));
}

FluidCellState UnpackFluidCellState(uint8_t packed)
{
  FluidCellState state{};
  state.Level = static_cast<uint8_t>(packed & 7u);
  state.Falling = static_cast<uint8_t>((packed & 0x08u) ? 1u : 0u);
  return state;
}

} // namespace cutum

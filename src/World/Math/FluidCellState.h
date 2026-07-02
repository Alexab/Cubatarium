#ifndef FLUIDCELLSTATE_H
#define FLUIDCELLSTATE_H

#include <cstdint>

namespace cutum
{

constexpr uint8_t FLUID_LEVEL_MAX = 7;
constexpr uint8_t LAVA_LEVEL_MAX = 3;

struct FluidCellState
{
  uint8_t Level : 3;
  uint8_t Falling : 1;
  uint8_t Reserved : 4;

  static FluidCellState Source();
  static FluidCellState Flowing(uint8_t level, bool falling = false);
  bool IsSource() const { return Level == 0 && Falling == 0; }
};

uint8_t PackFluidCellState(const FluidCellState &state);
FluidCellState UnpackFluidCellState(uint8_t packed);

} // namespace cutum

#endif

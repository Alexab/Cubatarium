#ifndef FLUIDCELLSTATE_H
#define FLUIDCELLSTATE_H

#include <cstdint>

namespace cutum
{

constexpr uint8_t FLUID_LEVEL_MAX = 7;
constexpr uint8_t LAVA_LEVEL_MAX = 3;

enum class FluidKind : uint8_t
{
  None = 0,
  Water = 1,
  Lava = 2,
};

struct FluidCellState
{
  uint8_t Level : 3;
  uint8_t Falling : 1;
  uint8_t Kind : 4;

  static FluidCellState Source();
  static FluidCellState Flowing(uint8_t level, bool falling = false);
  bool IsSource() const { return Level == 0 && Falling == 0; }

  FluidKind GetKind() const { return static_cast<FluidKind>(Kind); }
  void SetKind(FluidKind kind) { Kind = static_cast<uint8_t>(kind) & 15u; }
  bool HasExplicitKind() const { return Kind != 0; }

  FluidCellState WithKind(FluidKind kind) const
  {
    FluidCellState copy = *this;
    copy.SetKind(kind);
    return copy;
  }
};

uint8_t PackFluidCellState(const FluidCellState &state);
FluidCellState UnpackFluidCellState(uint8_t packed);
bool FluidCellHasActiveFluid(uint8_t packed);

} // namespace cutum

#endif

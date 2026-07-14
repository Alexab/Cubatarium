#include "World/Math/FluidCellState.h"

#include <cstdlib>
#include <iostream>

static void Expect(bool cond, const char *message)
{
  if (!cond)
  {
    std::cerr << "fluid_state_pack_test: " << message << std::endl;
    std::exit(1);
  }
}

int main()
{
  const cutum::FluidCellState source = cutum::FluidCellState::Source();
  Expect(source.IsSource(), "source IsSource");
  Expect(source.Level == 0, "source level 0");
  Expect(source.Falling == 0, "source not falling");

  const uint8_t packed_source = cutum::PackFluidCellState(source);
  Expect(packed_source == 0, "packed source is 0");
  const cutum::FluidCellState unpacked_source =
      cutum::UnpackFluidCellState(packed_source);
  Expect(unpacked_source.IsSource(), "round-trip source");

  for (uint8_t level = 1; level <= cutum::FLUID_LEVEL_MAX; ++level)
  {
    const cutum::FluidCellState flowing =
        cutum::FluidCellState::Flowing(level, false);
    const uint8_t packed = cutum::PackFluidCellState(flowing);
    const cutum::FluidCellState round_trip = cutum::UnpackFluidCellState(packed);
    Expect(round_trip.Level == level, "level round-trip");
    Expect(round_trip.Falling == 0, "not falling round-trip");
  }

  const cutum::FluidCellState falling =
      cutum::FluidCellState::Flowing(1, true);
  const uint8_t packed_falling = cutum::PackFluidCellState(falling);
  Expect((packed_falling & 0x08u) != 0, "falling bit set");
  const cutum::FluidCellState unpacked_falling =
      cutum::UnpackFluidCellState(packed_falling);
  Expect(unpacked_falling.Falling == 1, "falling round-trip");
  Expect(unpacked_falling.Level == 1, "falling level 1");

  const cutum::FluidCellState waterlogged =
      cutum::FluidCellState::Flowing(1).WithKind(cutum::FluidKind::Water);
  const uint8_t packed_waterlogged = cutum::PackFluidCellState(waterlogged);
  Expect(packed_waterlogged == 0x11, "waterlogged grass packs kind+level");
  Expect(cutum::UnpackFluidCellState(packed_waterlogged).GetKind() ==
             cutum::FluidKind::Water,
         "water kind round-trip");

  const cutum::FluidCellState lava_kind =
      cutum::FluidCellState::Source().WithKind(cutum::FluidKind::Lava);
  const uint8_t packed_lava = cutum::PackFluidCellState(lava_kind);
  Expect(packed_lava == 0x20, "lava source kind packs upper nibble");
  Expect(cutum::FluidCellHasActiveFluid(packed_lava), "kind-only cell is active");

  std::cout << "fluid_state_pack_test: OK" << std::endl;
  return 0;
}

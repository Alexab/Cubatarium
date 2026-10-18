#pragma once

#include <cstdint>

namespace cutum
{

struct AquiferParams
{
  int CellSizeX{24};
  int CellSizeY{16};
  int CellSizeZ{24};
  float CellJitter{0.28f};
  float DryCellChance{0.38f};
  int WaterLevelOffset{-12};
  int WaterLevelAmplitude{17};
  float BarrierBand{2.5f};
  float BarrierNoiseThreshold{0.55f};
  int PressureDifferenceThreshold{8};
  int LavaMaxY{10};
  float LavaCellChance{0.16f};
};

enum class AquiferFluid : uint8_t
{
  Dry,
  Water,
  Lava,
  Barrier,
};

struct AquiferSample
{
  AquiferFluid Fluid{AquiferFluid::Dry};
  int FluidLevel{-1};
};

/// Deterministic 3D aquifer cells. Results depend only on the world seed and
/// block coordinates, so independently generated chunks agree at their edges.
class UAquiferSampler
{
public:
  UAquiferSampler(uint32_t seed, int seaLevel, int maxHeight,
                  AquiferParams params = {});

  AquiferSample SampleAt(int x, int y, int z) const;

private:
  struct Cell;
  Cell SampleCell(int cell_x, int cell_y, int cell_z) const;
  int FluidLevelForCell(const Cell &cell) const;
  float CellRandom(int cell_x, int cell_y, int cell_z,
                   uint32_t channel) const;

  uint32_t Seed;
  int SeaLevel;
  int MaxHeight;
  AquiferParams Params;
};

} // namespace cutum

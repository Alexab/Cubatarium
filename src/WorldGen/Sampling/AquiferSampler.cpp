#include "WorldGen/Sampling/AquiferSampler.h"

#include "WorldGen/Core/Noise.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace cutum
{

struct UAquiferSampler::Cell
{
  int CellX{0};
  int CellY{0};
  int CellZ{0};
  float CenterX{0.0f};
  float CenterY{0.0f};
  float CenterZ{0.0f};
  AquiferFluid Fluid{AquiferFluid::Dry};
};

namespace
{

uint32_t Mix32(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

float UnitFloat(uint32_t value)
{
  return static_cast<float>(value) / 4294967295.0f;
}

int FloorDiv(int value, int divisor)
{
  const int quotient = value / divisor;
  const int remainder = value % divisor;
  return remainder < 0 ? quotient - 1 : quotient;
}

float Smoothstep01(float value)
{
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

} // namespace

UAquiferSampler::UAquiferSampler(uint32_t seed, int seaLevel, int maxHeight,
                                 AquiferParams params)
    : Seed(seed), SeaLevel(std::max(4, seaLevel)),
      MaxHeight(std::max(SeaLevel + 4, maxHeight)), Params(params)
{
  Params.CellSizeX = std::clamp(Params.CellSizeX, 8, 64);
  Params.CellSizeY = std::clamp(Params.CellSizeY, 8, 48);
  Params.CellSizeZ = std::clamp(Params.CellSizeZ, 8, 64);
  Params.CellJitter = std::clamp(Params.CellJitter, 0.0f, 0.45f);
  Params.DryCellChance = std::clamp(Params.DryCellChance, 0.0f, 0.9f);
  Params.WaterLevelOffset = std::clamp(Params.WaterLevelOffset, -48, 8);
  Params.WaterLevelAmplitude = std::clamp(Params.WaterLevelAmplitude, 0, 32);
  Params.BarrierBand = std::clamp(Params.BarrierBand, 0.0f, 8.0f);
  Params.BarrierNoiseThreshold =
      std::clamp(Params.BarrierNoiseThreshold, -1.0f, 1.0f);
  Params.PressureDifferenceThreshold =
      std::clamp(Params.PressureDifferenceThreshold, 0, 48);
  Params.LavaMaxY = std::clamp(Params.LavaMaxY, 1, MaxHeight / 2);
  Params.LavaCellChance = std::clamp(Params.LavaCellChance, 0.0f, 0.75f);
}

float UAquiferSampler::CellRandom(int cell_x, int cell_y, int cell_z,
                                  uint32_t channel) const
{
  uint32_t value = Seed ^ 0xA511E9B3u;
  value = Mix32(value ^ Mix32(static_cast<uint32_t>(cell_x) + 0x9E3779B9u));
  value = Mix32(value ^ Mix32(static_cast<uint32_t>(cell_y) + 0x85EBCA6Bu));
  value = Mix32(value ^ Mix32(static_cast<uint32_t>(cell_z) + 0xC2B2AE35u));
  value = Mix32(value ^ Mix32(channel + 0x27D4EB2Fu));
  return UnitFloat(value);
}

UAquiferSampler::Cell UAquiferSampler::SampleCell(int cell_x, int cell_y,
                                                  int cell_z) const
{
  Cell cell;
  cell.CellX = cell_x;
  cell.CellY = cell_y;
  cell.CellZ = cell_z;
  const float jitter_x = (CellRandom(cell_x, cell_y, cell_z, 1u) * 2.0f - 1.0f) *
                         Params.CellJitter;
  const float jitter_y = (CellRandom(cell_x, cell_y, cell_z, 2u) * 2.0f - 1.0f) *
                         Params.CellJitter;
  const float jitter_z = (CellRandom(cell_x, cell_y, cell_z, 3u) * 2.0f - 1.0f) *
                         Params.CellJitter;
  cell.CenterX = (static_cast<float>(cell_x) + 0.5f + jitter_x) *
                 static_cast<float>(Params.CellSizeX);
  cell.CenterY = (static_cast<float>(cell_y) + 0.5f + jitter_y) *
                 static_cast<float>(Params.CellSizeY);
  cell.CenterZ = (static_cast<float>(cell_z) + 0.5f + jitter_z) *
                 static_cast<float>(Params.CellSizeZ);

  if (CellRandom(cell_x, cell_y, cell_z, 4u) < Params.DryCellChance)
  {
    cell.Fluid = AquiferFluid::Dry;
    return cell;
  }

  if (cell.CenterY <= static_cast<float>(Params.LavaMaxY) &&
      CellRandom(cell_x, cell_y, cell_z, 5u) < Params.LavaCellChance)
  {
    cell.Fluid = AquiferFluid::Lava;
    return cell;
  }

  cell.Fluid = AquiferFluid::Water;
  return cell;
}

int UAquiferSampler::FluidLevelForCell(const Cell &cell) const
{
  if (cell.Fluid == AquiferFluid::Dry)
  {
    return -1;
  }
  if (cell.Fluid == AquiferFluid::Lava)
  {
    const int lava_span = std::max(1, Params.LavaMaxY - 3);
    return std::clamp(
        3 + static_cast<int>(CellRandom(cell.CellX, cell.CellY, cell.CellZ, 6u) *
                             lava_span),
        1, Params.LavaMaxY);
  }
  const float level_noise = FBM3D(
      cell.CenterX * 0.012f, cell.CenterY * 0.022f, cell.CenterZ * 0.012f,
      Seed + 8301u, 2, 0.5f, 2.0f);
  return std::clamp(
      SeaLevel + Params.WaterLevelOffset +
          static_cast<int>(std::lround(level_noise * Params.WaterLevelAmplitude)),
      3, std::max(3, std::min(SeaLevel - 2, MaxHeight - 1)));
}

AquiferSample UAquiferSampler::SampleAt(int x, int y, int z) const
{
  if (y < 1 || y >= MaxHeight)
  {
    return {};
  }

  const int base_x = FloorDiv(x, Params.CellSizeX);
  const int base_y = FloorDiv(y, Params.CellSizeY);
  const int base_z = FloorDiv(z, Params.CellSizeZ);
  const Cell *nearest = nullptr;
  const Cell *second = nullptr;
  float nearest_dist_sq = std::numeric_limits<float>::max();
  float second_dist_sq = std::numeric_limits<float>::max();

  std::array<Cell, 27> cells{};
  size_t cell_index = 0;
  for (int dz = -1; dz <= 1; ++dz)
  {
    for (int dy = -1; dy <= 1; ++dy)
    {
      for (int dx = -1; dx <= 1; ++dx)
      {
        cells[cell_index] = SampleCell(base_x + dx, base_y + dy, base_z + dz);
        const Cell &cell = cells[cell_index++];
        const float ox = static_cast<float>(x) - cell.CenterX;
        const float oy = static_cast<float>(y) - cell.CenterY;
        const float oz = static_cast<float>(z) - cell.CenterZ;
        const float distance_sq = ox * ox + oy * oy + oz * oz;
        if (distance_sq < nearest_dist_sq)
        {
          second = nearest;
          second_dist_sq = nearest_dist_sq;
          nearest = &cell;
          nearest_dist_sq = distance_sq;
        }
        else if (distance_sq < second_dist_sq)
        {
          second = &cell;
          second_dist_sq = distance_sq;
        }
      }
    }
  }

  if (nearest == nullptr)
  {
    return {};
  }

  const int nearest_level = FluidLevelForCell(*nearest);
  const int second_level = FluidLevelForCell(*second);

  const float first_distance = std::sqrt(nearest_dist_sq);
  const float second_distance = std::sqrt(second_dist_sq);
  const float distance_gap = second_distance - first_distance;
  const bool level_conflict =
      nearest->Fluid != second->Fluid ||
      (nearest->Fluid != AquiferFluid::Dry && second->Fluid != AquiferFluid::Dry &&
       std::abs(nearest_level - second_level) >=
           Params.PressureDifferenceThreshold);
  const bool below_aquifer_level = y <= std::max(nearest_level, second_level);
  if (Params.BarrierBand > 0.0f && level_conflict && below_aquifer_level &&
      distance_gap <= Params.BarrierBand)
  {
    const float barrier_noise = Perlin3D(
        static_cast<float>(x) * 0.055f, static_cast<float>(y) * 0.07f,
        static_cast<float>(z) * 0.055f, Seed + 8302u);
    if (barrier_noise > Params.BarrierNoiseThreshold)
    {
      return {AquiferFluid::Barrier, -1};
    }
  }

  if (nearest->Fluid == AquiferFluid::Dry || y > nearest_level)
  {
    return {AquiferFluid::Dry, nearest_level};
  }

  int fluid_level = nearest_level;
  if (nearest->Fluid == AquiferFluid::Water &&
      second->Fluid == AquiferFluid::Water && Params.BarrierBand > 0.0f)
  {
    const float second_weight =
        0.5f * (1.0f - Smoothstep01(distance_gap / Params.BarrierBand));
    fluid_level = static_cast<int>(std::lround(
        nearest_level * (1.0f - second_weight) +
        second_level * second_weight));
    if (y > fluid_level)
    {
      return {AquiferFluid::Dry, fluid_level};
    }
  }
  return {nearest->Fluid, fluid_level};
}

} // namespace cutum

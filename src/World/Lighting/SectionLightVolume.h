#pragma once
#include "World/Chunks/ChunkDimensions.h"
#include <array>
#include <cstdint>

namespace cutum
{
/// Exact sampling footprint for a section's face lighting, including fallback.
struct SectionLightVolume
{
  static constexpr int Radius = 2;
  static constexpr int Width = CHUNK_SIZE + 2 * Radius;
  static constexpr int CellCount = Width * Width * Width;
  std::array<uint8_t, CellCount> cells{};
  static constexpr int Index(int x, int y, int z)
  {
    return (x + Radius) + Width * ((y + Radius) + Width * (z + Radius));
  }
};
} // namespace cutum

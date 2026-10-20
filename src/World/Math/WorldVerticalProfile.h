#pragma once

#include "World/Math/GridMath.h"

namespace cutum
{

inline constexpr int kWorldDatumY = 0;
inline constexpr int kDefaultWorldMinY = -64;
inline constexpr int kDefaultWorldMaxY = 319;
inline constexpr int kDefaultWorldSeaLevelY = 63;

/// Vertical coordinate profile shared by generation and runtime
/// systems. DatumY is the coordinate origin; SeaLevelY is an independent
/// terrain/water setting.
struct WorldVerticalProfile
{
  constexpr WorldVerticalProfile(
      int min_y = kDefaultWorldMinY, int max_y = kDefaultWorldMaxY,
      int sea_level_y = kDefaultWorldSeaLevelY,
      int bedrock_top_y = kDefaultWorldMinY, int datum_y = kWorldDatumY)
      : MinY(min_y), MaxY(max_y), SeaLevelY(sea_level_y),
        BedrockTopY(bedrock_top_y), DatumY(datum_y)
  {
  }

  constexpr bool IsValid() const { return MinY <= MaxY; }
  constexpr bool ContainsY(int y) const { return y >= MinY && y <= MaxY; }
  constexpr int ClampY(int y) const
  {
    return y < MinY ? MinY : (y > MaxY ? MaxY : y);
  }
  int MinChunkY(int chunk_size) const { return FloorDiv(MinY, chunk_size); }
  int MaxChunkY(int chunk_size) const { return FloorDiv(MaxY, chunk_size); }

  int MinY;
  int MaxY;
  int SeaLevelY;
  int BedrockTopY;
  int DatumY;
};

} // namespace cutum

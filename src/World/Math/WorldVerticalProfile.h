#pragma once

#include "World/Math/GridMath.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

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
  constexpr WorldVerticalProfile(int min_y = kDefaultWorldMinY,
                                 int max_y = kDefaultWorldMaxY,
                                 int sea_level_y = kDefaultWorldSeaLevelY,
                                 int bedrock_top_y = kDefaultWorldMinY,
                                 int datum_y = kWorldDatumY)
      : MinY(min_y), MaxY(max_y), SeaLevelY(sea_level_y),
        BedrockTopY(bedrock_top_y), DatumY(datum_y)
  {
  }

  constexpr bool IsValid() const { return MinY <= MaxY; }
  constexpr bool ContainsY(int y) const { return y >= MinY && y <= MaxY; }
  /// Recovery starts below the configured world's bottom face, not datum Y.
  float MinimumEyeY() const { return static_cast<float>(MinY) - 0.5f; }
  constexpr int ClampY(int y) const
  {
    return y < MinY ? MinY : (y > MaxY ? MaxY : y);
  }
  int MinChunkY(int chunk_size) const { return FloorDiv(MinY, chunk_size); }
  int MaxChunkY(int chunk_size) const { return FloorDiv(MaxY, chunk_size); }

  /// Inclusive block range owned by a vertical slice, clipped to this world.
  /// Missing intersection is distinct from a valid dark/empty slice.
  std::optional<std::pair<int, int>> SliceBounds(int cy, int chunk_size) const
  {
    if (!IsValid() || chunk_size <= 0)
      return std::nullopt;
    const int64_t low = static_cast<int64_t>(cy) * chunk_size;
    const int64_t high = low + chunk_size - 1;
    if (high < MinY || low > MaxY)
      return std::nullopt;
    return std::pair<int, int>{static_cast<int>(std::max<int64_t>(MinY, low)),
                               static_cast<int>(std::min<int64_t>(MaxY, high))};
  }

  int MinY;
  int MaxY;
  int SeaLevelY;
  int BedrockTopY;
  int DatumY;
};

} // namespace cutum

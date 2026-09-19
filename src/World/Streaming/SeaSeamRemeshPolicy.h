#pragma once
// BUDGET_MS: 0.0
// R06 field: when first drawable coverage publishes, remesh sea/subsea seams.
// Drawable stays out of InputsStillValid stamp (ADR Strategy A).

#include <algorithm>
#include <cmath>

namespace cutum
{

/// Surface band |cy-sea|<=2 inland; underwater/near_water widens to subsea_band.
inline bool ShouldRemeshSeaSeamOnFirstDrawable(int chunk_cy, int sea_cy,
                                              bool underwater_or_near_water,
                                              int surface_band = 2,
                                              int subsea_band = 4)
{
  const int band = underwater_or_near_water ? subsea_band : surface_band;
  return std::abs(chunk_cy - sea_cy) <= band;
}

inline int SeaSeamRemeshMinY(int sea_level, int chunk_size,
                             bool underwater_or_near_water)
{
  const int depth = underwater_or_near_water ? chunk_size * 4 : chunk_size;
  return std::max(0, sea_level - depth);
}

inline int SeaSeamRemeshMaxY(int sea_level, int chunk_size, int max_height)
{
  return std::min(max_height, sea_level + chunk_size * 2);
}

} // namespace cutum

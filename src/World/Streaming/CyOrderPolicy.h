#pragma once

#include <algorithm>
#include <vector>

namespace cutum
{

/// Era33 P1: mesh Immediate / FirstMesh cy visit order.
/// Land: ground → ±1 → upward canopy (avoid tree tops before terrain).
/// Ocean (FillWater): sea/prefer first, then ± expand.
inline std::vector<int> BuildMeshCyVisitOrder(int cy0, int cy1, int prefer_cy,
                                              int sea_cy, bool fill_water)
{
  std::vector<int> order;
  if (cy1 < cy0)
  {
    return order;
  }
  order.reserve(static_cast<size_t>(cy1 - cy0 + 1));
  auto push_cy = [&](int cy)
  {
    if (cy < cy0 || cy > cy1)
    {
      return;
    }
    if (std::find(order.begin(), order.end(), cy) == order.end())
    {
      order.push_back(cy);
    }
  };
  prefer_cy = std::clamp(prefer_cy, cy0, cy1);
  if (fill_water)
  {
    push_cy(prefer_cy);
    push_cy(sea_cy);
    for (int d = 1; d <= std::max(prefer_cy - cy0, cy1 - prefer_cy); ++d)
    {
      push_cy(prefer_cy - d);
      push_cy(prefer_cy + d);
    }
  }
  else
  {
    push_cy(prefer_cy);
    push_cy(prefer_cy - 1);
    push_cy(prefer_cy + 1);
    for (int cy = prefer_cy + 2; cy <= cy1; ++cy)
    {
      push_cy(cy);
    }
    for (int cy = prefer_cy - 2; cy >= cy0; --cy)
    {
      push_cy(cy);
    }
  }
  return order;
}

} // namespace cutum

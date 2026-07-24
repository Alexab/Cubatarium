#include "WorldGen/Features/CaveDepthBand.h"
#include <algorithm>

namespace cutum
{

CaveDepthBand ComputeCaveDepthBand(int surface_y, const CaveParams &params)
{
  CaveDepthBand band;
  if (surface_y < params.minY)
  {
    return band;
  }
  band.y_top = surface_y - params.minDepthBelowSurface;
  band.y_bottom =
      std::max(params.minY, surface_y - params.maxDepthBelowSurface);
  band.valid = band.y_bottom <= band.y_top;
  return band;
}

} // namespace cutum

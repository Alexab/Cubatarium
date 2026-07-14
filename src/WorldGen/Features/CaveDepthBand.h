#pragma once

#include "WorldGen/Features/CaveCarver.h"

namespace cutum
{

struct CaveDepthBand
{
  int y_bottom{0};
  int y_top{0};
  bool valid{false};
};

CaveDepthBand ComputeCaveDepthBand(int surface_y, const CaveParams &params);

} // namespace cutum

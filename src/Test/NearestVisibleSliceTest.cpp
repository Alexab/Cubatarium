#include "World/Streaming/NearestVisibleSlice.h"

#include <cstdlib>
#include <iostream>

int main()
{
  // Translation across X=0 must preserve the selected vertical section.
  for (int x : {-17, -1, 0, 1, 17})
  {
    cutum::NearestVisibleSlice target;
    if (target.Coord || !target.Consider({x, -3, -1}, 0) ||
        target.Consider({x, -1, -1}, 32) || target.Coord->y != -3 ||
        target.Coord->x != x || target.Distance != 0)
    {
      std::cerr << "nearest section lost at signed X=" << x << '\n';
      return EXIT_FAILURE;
    }
    // Equal-distance observations must not churn target identity/metadata.
    if (target.Consider({x, -2, -1}, 0) || target.Coord->y != -3)
      return EXIT_FAILURE;
  }
  cutum::NearestVisibleSlice target;
  target.Consider({-1, -4, -1}, 16);
  if (!target.Consider({-1, -1, -1}, 3) || *target.Coord != glm::ivec3(-1) ||
      target.Distance != 3)
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}

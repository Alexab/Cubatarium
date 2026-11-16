#include "World/Math/WorldVerticalProfile.h"

#include <cstdlib>
#include <iostream>
#include <limits>

static void Expect(bool value, const char *message)
{
  if (!value)
  {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

int main()
{
  const cutum::WorldVerticalProfile normal;
  Expect(normal.MinimumEyeY() == -64.5f &&
             cutum::WorldVerticalProfile(-128, 511).MinimumEyeY() == -128.5f,
         "player recovery floor follows the configured bottom block face");
  const auto check = [&](int cy, int low, int high)
  {
    const auto bounds = normal.SliceBounds(cy, 16);
    Expect(bounds && bounds->first == low && bounds->second == high,
           "slice range must preserve signed world coordinates");
  };
  check(-4, -64, -49);
  check(-3, -48, -33);
  check(-1, -16, -1);
  check(0, 0, 15);
  check(19, 304, 319);
  Expect(!normal.SliceBounds(-5, 16) && !normal.SliceBounds(20, 16),
         "slices outside the world have no light band");
  const cutum::WorldVerticalProfile partial(-60, 310);
  Expect(partial.SliceBounds(-4, 16) == std::make_pair(-60, -49),
         "partial bottom slice is clipped at configured floor");
  Expect(partial.SliceBounds(19, 16) == std::make_pair(304, 310),
         "partial top slice is clipped at configured ceiling");
  Expect(!normal.SliceBounds(std::numeric_limits<int>::max(), 16) &&
             !normal.SliceBounds(std::numeric_limits<int>::min(), 16),
         "invalid extreme slice coordinates must not overflow");
  Expect(!normal.SliceBounds(0, 0) &&
             !cutum::WorldVerticalProfile(1, 0).SliceBounds(0, 16),
         "invalid profiles and chunk sizes cannot create light jobs");
  return EXIT_SUCCESS;
}

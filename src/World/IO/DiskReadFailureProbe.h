#pragma once

#include "World/IO/DiskAirCancellationProbe.h"

namespace cutum
{
// Bounded opt-in diagnostic: reject five returning reads of one upper slice,
// exhausting one ordinary retry batch, then let actual disk reads recover.
class DiskReadFailureProbe
{
public:
  explicit DiskReadFailureProbe(std::optional<glm::ivec2> target = {})
      : Target(target)
  {
  }
  bool Enabled() const { return Target.has_value(); }
  void ObserveFocus(glm::ivec3 focus)
  {
    if (!Target)
      return;
    const auto distance = [](int a, int b)
    {
      const int64_t d = int64_t(a) - b;
      return d < 0 ? -d : d;
    };
    WasFar |= std::max(distance(focus.x, Target->x),
                       distance(focus.z, Target->y)) >= 16;
  }
  bool InjectFor(glm::ivec3 coord)
  {
    if (!Target || !WasFar || Remaining == 0 || coord.y != 19 ||
        glm::ivec2(coord.x, coord.z) != *Target)
      return false;
    --Remaining;
    return true;
  }
  bool CheckSaveOnce()
  {
    if (!Target || SaveChecked)
      return false;
    SaveChecked = true;
    return true;
  }

private:
  std::optional<glm::ivec2> Target;
  int Remaining{5};
  bool WasFar{false}, SaveChecked{false};
};
} // namespace cutum

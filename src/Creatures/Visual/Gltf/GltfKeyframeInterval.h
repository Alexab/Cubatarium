#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cutum
{
// Estimate an interval, validate the exact lower-bound bracket, then fall
// back to binary search for nonuniform timestamps. No interpolation changes.
inline size_t GltfKeyframeUpperIndex(const std::vector<float> &times, float time)
{
  const size_t count = times.size();
  if (!count) return 0;
  if (count <= 16)
  {
    size_t upper = 1;
    while (upper < count && times[upper] < time) ++upper;
    return upper;
  }
  // These also preserve the old scan for NaN time and infinities.
  if (!(time > times[1])) return 1;
  if (time > times.back()) return count;
  if (std::isfinite(time) && std::isfinite(times.front()) &&
      std::isfinite(times.back()) && times.back() > times.front())
  {
    const double position = (static_cast<double>(time) - times.front()) /
      (static_cast<double>(times.back()) - times.front()) * (count - 1);
    size_t upper = std::min(count - 1, static_cast<size_t>(position) + 1);
    if (times[upper] < time && upper + 1 < count) ++upper;
    else if (upper > 1 && !(times[upper - 1] < time)) --upper;
    if (!(times[upper] < time) && (upper == 1 || times[upper - 1] < time))
      return upper;
  }
  return static_cast<size_t>(std::lower_bound(times.begin() + 1, times.end(), time) - times.begin());
}
}

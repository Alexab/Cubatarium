#pragma once
#include "World/Chunks/ChunkDimensions.h"
#include "World/Math/WorldVerticalProfile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
namespace cutum
{
struct LightSampleApproachOptions
{
  double horizon_seconds{2};
  double gravity{0};
  // Motor envelope includes every discrete gravity step up to its terminal
  // bound. Zero selects the exact constant-gravity loose-body trajectory
  // instead.
  double terminal_fall_speed{0};
};
inline constexpr int kMaxLightSampleApproachSections = 1024;
// Swept segments follow motion; no full3D bounding box of the entire route.
// Fixed storage/iteration limits also apply to malformed/extreme inputs.
// Clipping is observable. Forecasts never grant readiness authority.
template <typename Visit>
int VisitLightSampleApproach(glm::ivec3 cell, glm::vec3 velocity,
                             const WorldVerticalProfile &profile, Visit visit,
                             LightSampleApproachOptions options = {},
                             bool *clipped = nullptr)
{
  if (clipped)
    *clipped = false;
  if (!profile.IsValid())
    return 0;
  const auto bounded = [&](double v, double lo, double hi, double fallback)
  {
    if (!std::isfinite(v))
      return fallback;
    const double result = std::clamp(v, lo, hi);
    if (clipped && result != v)
      *clipped = true;
    return result;
  };
  const double horizon = bounded(options.horizon_seconds, 0, 6, 2);
  const double gravity = bounded(options.gravity, -64, 64, 0);
  const double terminal = bounded(options.terminal_fall_speed, 0, 256, 0);
  glm::dvec3 speed;
  for (int a = 0; a < 3; ++a)
    speed[a] = bounded(velocity[a], -256, 256, 0);
  std::array<glm::ivec3, kMaxLightSampleApproachSections> visited;
  int count = 0;
  const auto emit = [&](glm::ivec3 c)
  {
    if (!profile.SliceBounds(c.y, CHUNK_SIZE) ||
        std::find(visited.begin(), visited.begin() + count, c) !=
            visited.begin() + count)
      return;
    if (count == kMaxLightSampleApproachSections)
    {
      if (clipped)
        *clipped = true;
      return;
    }
    visited[count++] = c;
    visit(c);
  };
  emit({FloorDiv(cell.x, CHUNK_SIZE), FloorDiv(cell.y, CHUNK_SIZE),
        FloorDiv(cell.z, CHUNK_SIZE)});
  const double max_speed = std::max(
      {std::abs(speed.x), std::abs(speed.y), std::abs(speed.z), terminal});
  if (horizon == 0 || (max_speed == 0 && gravity == 0))
    return count;
  const int steps =
      std::clamp(static_cast<int>(std::ceil(
                     (max_speed + std::abs(gravity) * horizon) * horizon / 8)),
                 1, 256);
  const auto section = [](int start, double offset, bool upper)
  {
    const double p =
        double(start) + (upper ? std::ceil(offset) : std::floor(offset));
    return FloorDiv(
        static_cast<int>(std::clamp(p, double(std::numeric_limits<int>::min()),
                                    double(std::numeric_limits<int>::max()))),
        CHUNK_SIZE);
  };
  for (int step = 0; step < steps; ++step)
  {
    const double t0 = horizon * step / steps, t1 = horizon * (step + 1) / steps;
    glm::dvec3 lo = glm::min(speed * t0, speed * t1),
               hi = glm::max(speed * t0, speed * t1);
    if (terminal > 0)
    {
      lo.y = std::min({lo.y, -terminal * t0, -terminal * t1});
      hi.y = std::max({hi.y, -terminal * t0, -terminal * t1});
    }
    else if (gravity != 0)
    {
      const auto y = [&](double t)
      { return speed.y * t + .5 * gravity * t * t; };
      lo.y = std::min(y(t0), y(t1));
      hi.y = std::max(y(t0), y(t1));
      const double apex = -speed.y / gravity;
      if (apex > t0 && apex < t1)
      {
        lo.y = std::min(lo.y, y(apex));
        hi.y = std::max(hi.y, y(apex));
      }
    }
    glm::ivec3 begin, end;
    for (int a = 0; a < 3; ++a)
    {
      begin[a] = section(cell[a], lo[a], false);
      end[a] = section(cell[a], hi[a], true);
    }
    begin.y = std::max(begin.y, profile.MinChunkY(CHUNK_SIZE));
    end.y = std::min(end.y, profile.MaxChunkY(CHUNK_SIZE));
    for (int y = begin.y; y <= end.y; ++y)
      for (int z = begin.z; z <= end.z; ++z)
        for (int x = begin.x; x <= end.x; ++x)
          emit({x, y, z});
  }
  return count;
}
} // namespace cutum

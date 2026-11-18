#include "World/Lighting/LightSampleApproach.h"
#include "World/Lighting/LightSampleDeliveryBudget.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

static void Expect(bool ok, const char *message)
{
  if (!ok)
  {
    std::cerr << "light_sample_prediction_test: " << message << '\n';
    std::exit(1);
  }
}

int main()
{
  using namespace cutum;
  // Native producer receipt: 1.329 seconds from submission to application.
  // Independent dense trajectory uses actual terminal fall speed, not the
  // predictor's clamped displacement or its section traversal algorithm.
  const glm::ivec3 cell(-3, 15, 39);
  const glm::vec3 velocity(0, -50, 0);
  std::vector<glm::ivec3> requested;
  VisitLightSampleApproach(cell, velocity, {},
                           [&](glm::ivec3 c) { requested.push_back(c); });
  for (int step = 0; step <= 1329; ++step)
  {
    const double seconds = step / 1000.0;
    const int y = static_cast<int>(std::floor(cell.y - 50 * seconds));
    const glm::ivec3 expected(FloorDiv(cell.x, CHUNK_SIZE),
                              FloorDiv(y, CHUNK_SIZE),
                              FloorDiv(cell.z, CHUNK_SIZE));
    if (std::find(requested.begin(), requested.end(), expected) ==
        requested.end())
    {
      std::cerr << "uncovered native fall at seconds=" << seconds
                << " section_y=" << expected.y << '\n';
      Expect(false,
             "motion footprint must cover the measured delivery interval");
    }
  }
  LightSampleDeliveryBudget budget;
  Expect(budget.HorizonSeconds(0) == 2,
         "startup has an explicit delivery reserve");
  budget.Observe(1000, 1329);
  const double horizon = budget.HorizonSeconds(1000);
  Expect(horizon > 2 && horizon > 1.329 && horizon <= 6,
         "measured delivery extends the forecast with bounded jitter reserve");
  Expect(
      budget.HorizonSeconds(1000, 6000) == 6,
      "currently stalled producer affects budget before its first completion");
  Expect(
      budget.HorizonSeconds(32000) == 2,
      "old latency observations expire rather than inflating demand forever");
  budget.Observe(32000, std::numeric_limits<double>::infinity());
  Expect(budget.HorizonSeconds(32000) == 2,
         "invalid observations cannot poison budget");
  budget.Reset();
  Expect(budget.HorizonSeconds(1000) == 2,
         "world cancellation resets latency history");

  const WorldVerticalProfile profile;
  const auto collect =
      [&](glm::ivec3 start, glm::vec3 speed, LightSampleApproachOptions options)
  {
    requested.clear();
    bool clipped = false;
    const int n = VisitLightSampleApproach(
        start, speed, profile, [&](glm::ivec3 c) { requested.push_back(c); },
        options, &clipped);
    Expect(!clipped && n == requested.size() &&
               n <= kMaxLightSampleApproachSections,
           "supported trajectory fits fixed storage without truncation");
    for (auto c : requested)
      Expect(std::count(requested.begin(), requested.end(), c) == 1,
             "each forecast section is visited exactly once");
  };
  const auto contains = [&](glm::dvec3 position)
  {
    glm::ivec3 at;
    for (int a = 0; a < 3; ++a)
      at[a] = static_cast<int>(std::floor(position[a] + .5));
    if (!profile.ContainsY(at.y))
      return;
    const glm::ivec3 section(FloorDiv(at.x, CHUNK_SIZE),
                             FloorDiv(at.y, CHUNK_SIZE),
                             FloorDiv(at.z, CHUNK_SIZE));
    Expect(std::find(requested.begin(), requested.end(), section) !=
               requested.end(),
           "independent dense physical trajectory stays within requested "
           "sections");
  };
  for (float initial_y_speed : {0.f, 8.f, -10.f, -50.f})
    for (double dt : {1.0 / 240, 1.0 / 60, .1, .25})
    {
      const glm::ivec3 start(-1, 63, 15);
      const glm::vec3 speed(-30, initial_y_speed, 10);
      LightSampleApproachOptions options;
      options.horizon_seconds = horizon;
      options.terminal_fall_speed = 50;
      collect(start, speed, options);
      // Separate semi-implicit motor integration over several frame quanta.
      glm::dvec3 position = glm::dvec3(start) + glm::dvec3(.49);
      double vy = initial_y_speed;
      contains(position);
      for (double time = 0; time < horizon;)
      {
        const double step = std::min(dt, horizon - time);
        vy = std::max(-50.0, vy - 20 * step);
        position += glm::dvec3(-30, vy, 10) * step;
        contains(position);
        time += step;
      }
    }
  {
    const glm::ivec3 start(15, 15, -1);
    LightSampleApproachOptions options;
    options.horizon_seconds = 3;
    options.gravity = -20;
    collect(start, {60, 20, -20}, options);
    for (int n = 0; n <= 3000; ++n)
    {
      const double t = n / 1000.0;
      contains(glm::dvec3(start) + glm::dvec3(.49) +
               glm::dvec3(60 * t, 20 * t - 10 * t * t, -20 * t));
    }
  }
  collect({1, 1, 1}, {40, 0, 40}, {});
  Expect(requested.size() <= 32 &&
             std::find(requested.begin(), requested.end(),
                       glm::ivec3(0, 0, 4)) == requested.end(),
         "diagonal route does not request the entire bounding square");
  collect({1, 1, 1}, {}, {});
  Expect(requested.size() == 1,
         "static supported consumer has no future footprint");
  bool clipped = false;
  LightSampleApproachOptions excessive;
  excessive.horizon_seconds = 1e30;
  VisitLightSampleApproach(
      {1, 1, 1}, glm::vec3(1e30f), profile, [](glm::ivec3) {}, excessive,
      &clipped);
  Expect(clipped, "out-of-contract motion or time clipping remains observable");
  std::cout << "light_sample_prediction_test: OK\n";
}

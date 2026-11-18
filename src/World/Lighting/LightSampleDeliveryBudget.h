#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace cutum
{
// Main-thread observation only: never a light/input readiness certificate.
class LightSampleDeliveryBudget
{
public:
  void Observe(double now_ms, double elapsed_ms)
  {
    if (!std::isfinite(now_ms) || !std::isfinite(elapsed_ms) || elapsed_ms < 0)
      return;
    Samples[Cursor++ % Samples.size()] = {now_ms, elapsed_ms, true};
  }
  double HorizonSeconds(double now_ms, double pending_age_ms = 0) const
  {
    double worst =
        std::isfinite(pending_age_ms) ? std::max(0.0, pending_age_ms) : 0;
    for (const auto &s : Samples)
      if (s.valid && now_ms >= s.time && now_ms - s.time <= 30000)
        worst = std::max(worst, s.elapsed);
    return std::clamp(worst * .0015 + .25, 2.0, 6.0);
  }
  void Reset()
  {
    Samples = {};
    Cursor = 0;
  }

private:
  struct Sample
  {
    double time{0}, elapsed{0};
    bool valid{false};
  };
  std::array<Sample, 64> Samples{};
  std::size_t Cursor{0};
};
} // namespace cutum

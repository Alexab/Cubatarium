#ifndef STALLTIMER_H
#define STALLTIMER_H

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace cutum
{

/// Reports one stall to stderr and appends it to stall_log.txt in the working
/// directory (console output is not always capturable, e.g. headless runs).
inline void StallLogLine(const char *kind, const char *label, double ms)
{
  std::fprintf(stderr, "[Stall] %s %s %.1f ms\n", kind, label, ms);
  std::fflush(stderr);
  if (FILE *f = std::fopen("stall_log.txt", "a"))
  {
    std::fprintf(f, "%s %s %.1f ms\n", kind, label, ms);
    std::fclose(f);
  }
}

inline bool StallTraceAll()
{
  static const bool on = std::getenv("CUBA_STALL_TRACE") != nullptr;
  return on;
}

/// Scoped main-thread stall probe: reports when the scope took longer than
/// `threshold_ms` (always with CUBA_STALL_TRACE=1). Cheap enough to leave in
/// startup / world-operation paths as a regression guard.
class ScopedStallTimer
{
public:
  explicit ScopedStallTimer(const char *label, double threshold_ms = 50.0)
      : Label(label), ThresholdMs(threshold_ms),
        Begin(std::chrono::steady_clock::now())
  {
  }
  ScopedStallTimer(const ScopedStallTimer &) = delete;
  ScopedStallTimer &operator=(const ScopedStallTimer &) = delete;
  ~ScopedStallTimer()
  {
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - Begin)
                          .count();
    if (ms >= ThresholdMs || StallTraceAll())
    {
      StallLogLine("scope", Label, ms);
    }
  }

private:
  const char *Label;
  double ThresholdMs;
  std::chrono::steady_clock::time_point Begin;
};

/// Sequential stage marker: reports the time since the previous mark, i.e. the
/// stage that just finished. Use for linear startup / prepare sequences.
inline void StallStageMark(const char *finished_stage,
                           double threshold_ms = 50.0)
{
  static std::chrono::steady_clock::time_point last =
      std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double ms =
      std::chrono::duration<double, std::milli>(now - last).count();
  last = now;
  if (ms >= threshold_ms || StallTraceAll())
  {
    StallLogLine("stage", finished_stage, ms);
  }
}

} // namespace cutum

#endif

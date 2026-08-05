#ifndef FRAME_PERF_MONITOR_H
#define FRAME_PERF_MONITOR_H

#include <string>

namespace cutum
{

class UWorld;

/// Periodic InGame performance logging to glog + bin/logs/perf_*.jsonl.
/// Does not require saving the world.
class UFramePerfMonitor
{
public:
  /// Call once after logging is initialized (optional; opens on first frame).
  static void EnsureSession();

  /// Record one InGame frame. `swap_wait_ms` is wall time spent in SwapBuffers.
  /// `frame_wall_ms` is same-frame wall (begin→after swap); does not mutate
  /// World wall delta used for gameplay timing.
  /// `interval_sec` comes from UiSettings::PerfLogIntervalSec (default 2).
  static void OnInGameFrame(UWorld &world, double swap_wait_ms,
                            double interval_sec, double frame_wall_ms);

  static void Shutdown();

  /// Absolute path of the current/last perf_*.jsonl session (empty if none).
  static std::string GetLastSessionPath();
};

} // namespace cutum

#endif

#ifndef APP_RUNNER_H
#define APP_RUNNER_H

#include <string>

namespace cutum
{

class IUPlatformPaths;
class IUPlatformWindow;

int RunCubatarium(IUPlatformWindow &window, IUPlatformPaths &paths);

/// Hidden-window GUI smoke: Enter Game with default_world, exit after N in-game frames.
int RunEnterGameSmoke(IUPlatformPaths &paths, int in_game_frames = 5);

/// Agent flight simulation: load world, fly forward, quit, write JSON report.
/// Manual World_164 ocean pass (perf_20260720-024756): idle ~50s at focus
/// (-35,6), then fly west (−X) ~11 chunks with holes/wall spikes. Autopilot
/// mirrors that heading (yaw 180° → −X) after a short settle.
struct FlightSimOptions
{
  std::string WorldName; // empty = config default_world
  double InGameSeconds{45.0};
  bool Fly{true};
  bool HoldForward{true};
  /// Seconds after InGame before holding W (streaming settle).
  double IdleBeforeFlySec{8.0};
  /// Look direction: 180° = −X (west), matching World_164 manual ocean flight.
  float FaceYawDeg{180.0f};
  float FacePitchDeg{-10.0f};
  /// Eye height above sea when too low (ocean cruise).
  float MinAltitudeAboveSea{28.0f};
  bool Sprint{false};
  /// Reset to a fixed ocean cruise start each run (matches World_164 manual).
  bool TeleportToCruiseStart{false};
  float CruiseStartChunkX{-47.0f};
  float CruiseStartChunkZ{5.0f};
  std::string PerfOutPath;   // optional copy of perf jsonl
  std::string ReportPath;    // JSON verdict path (default bin/flight_sim_report.json)
  /// Wall-clock from process start (covers long World_164 load + flight).
  double SafetyTimeoutSec{900.0};
  /// Fly west then release W and hold still (stop-recovery scenario).
  bool FlyStopMode{false};
  double FlyPhaseSec{40.0};
  double StopPhaseSec{35.0};
};

int RunFlightSim(IUPlatformPaths &paths, const FlightSimOptions &options);

} // namespace cutum

#endif

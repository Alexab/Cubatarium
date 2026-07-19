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
struct FlightSimOptions
{
  std::string WorldName; // empty = config default_world
  double InGameSeconds{45.0};
  bool Fly{true};
  bool HoldForward{true};
  std::string PerfOutPath;   // optional copy of perf jsonl
  std::string ReportPath;    // JSON verdict path (default bin/flight_sim_report.json)
  double SafetyTimeoutSec{180.0};
};

int RunFlightSim(IUPlatformPaths &paths, const FlightSimOptions &options);

} // namespace cutum

#endif

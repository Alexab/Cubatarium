#pragma once
#include "Creatures/Locomotion/LocomotionTypes.h"
#include "Creatures/Visual/CreatureRenderStats.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cutum
{
// Opt-in QA only. No disk IO during frames and no blocking GPU result reads.
// The GPU interval encloses Queue.Flush commands, not the whole world renderer.
class UCreaturePassTimingProbe
{
public:
  UCreaturePassTimingProbe();
  bool Enabled() const { return !Path.empty(); }
  void BeginFrame(float dt, float cameraX, float cameraY, float cameraZ);
  void RecordPose(LocomotionState state, double milliseconds);
  void BeginGpuDraw();
  void EndGpuDraw();
  void EndFrame(const CreatureRenderStats &stats);
  static void CompleteWindowFrame(double wallMs, double swapMs);
  static void ShutdownSession();
  void Shutdown(); // Call while the GL context still exists.

private:
  using Clock = std::chrono::steady_clock;
  struct Frame
  {
    double PassMs{0}, PoseMs{0}, WallMs{-1}, SwapMs{-1};
    float Dt{0};
    std::array<float, 3> Camera{};
    CreatureRenderStats Stats;
    std::array<uint32_t, static_cast<size_t>(LocomotionState::Count)> States{};
    uint64_t GpuNs{0};
    bool GpuReady{false};
  };
  struct QueryPair
  {
    unsigned Begin{0}, End{0};
    size_t FrameIndex{0};
    bool Pending{false};
  };
  void Poll(QueryPair &pair);
  static UCreaturePassTimingProbe *WindowFrameProbe;
  static UCreaturePassTimingProbe *SessionProbe;
  std::string Path;
  std::vector<Frame> Frames;
  std::array<QueryPair, 16> Queries{};
  size_t Current{0}, Slot{0};
  Clock::time_point Start;
  uint64_t DroppedFrames{0}, SkippedGpuQueries{0};
  bool Initialized{false}, GpuSupported{false}, QueryActive{false},
      FrameActive{false};
};
} // namespace cutum

#pragma once

#include "World/Core/RuntimeTuning.h"

#include <algorithm>

namespace cutum
{

/// UE-style streaming performance level: only admission/budget policy.
/// Does not change ring-gate math, Dirty order, or worldgen.
enum class StreamingPressureLevel : int
{
  Green = 0,
  Yellow = 1,
  Red = 2,
};

struct StreamingPressureInput
{
  int pending_light{0};
  int dirty{0};
  double frame_ms{0.0};
  /// Missing GreedyCache in focus only (not pending light).
  bool visual_holes{false};
  bool underfeet_need{false};
  /// Near-focus columns awaiting first light (relight throughput signal).
  int pending_light_focus{0};
};

struct StreamingPressureState
{
  StreamingPressureLevel level{StreamingPressureLevel::Green};
  /// Frames that must pass at exit thresholds before level drops one step.
  int deescalate_hold_frames{0};
  /// Keep focus-pressure schedule/starve after holes clear (anti-flicker).
  int focus_mode_hold_frames{0};
};

struct StreamingPressureCaps
{
  StreamingPressureLevel level{StreamingPressureLevel::Green};
  /// When false, fly MaxLoadOps/commits stay at base (no boost).
  bool allow_fly_load_boost{true};
  bool allow_prefetch{true};
  /// Hard ceilings; -1 means "no extra clamp beyond existing logic".
  int max_load_ops_cap{-1};
  int max_commits_cap{-1};
  int recover_n_cap{-1};
  int mesh_fly_cap{-1};
  int bg_budget_floor{0};
  bool focus_pressure_mode{false};
};

namespace streaming_pressure
{
// Enter thresholds (escalate immediately when exceeded).
inline constexpr int kEnterPendingYellow = 10;
inline constexpr int kEnterPendingRed = 45;
inline constexpr int kEnterDirtyYellow = 420;
inline constexpr int kEnterDirtyRed = 900;
inline constexpr double kEnterWallYellowMs = 20.0;
inline constexpr double kEnterWallRedMs = 40.0;

// Exit thresholds (must be at/below to de-escalate) + hold.
inline constexpr int kExitPendingYellow = 8;
inline constexpr int kExitPendingRed = 15;
inline constexpr int kExitDirtyYellow = 300;
/// Was 600: Dirty often plateaus ~600–700 while holes remain, trapping Red
/// forever and starving idle focus recover/mesh. Allow leave-Red sooner.
/// Exit at 500 so plateaus ~850–950 cannot oscillate forever at the 800 edge.
inline constexpr int kExitDirtyRed = 750;
inline constexpr double kExitWallYellowMs = 16.0;
inline constexpr double kExitWallRedMs = 28.0;
inline constexpr int kLevelHoldFrames = 45;
inline constexpr int kFocusModeHoldFrames = 45;
} // namespace streaming_pressure

inline StreamingPressureLevel
RawStreamingPressureLevel(const StreamingPressureInput &in)
{
  using namespace streaming_pressure;
  // Wall hitch alone must NOT enter Red — manual flight sat at wall~50–60ms
  // with Dirty~350 and locked Red forever (FPS death spiral).
  if (in.pending_light > kEnterPendingRed || in.dirty > kEnterDirtyRed)
  {
    return StreamingPressureLevel::Red;
  }
  if (in.pending_light > kEnterPendingYellow || in.dirty > kEnterDirtyYellow ||
      in.frame_ms > kEnterWallYellowMs)
  {
    return StreamingPressureLevel::Yellow;
  }
  return StreamingPressureLevel::Green;
}

inline bool CanExitTo(StreamingPressureLevel target,
                      const StreamingPressureInput &in)
{
  using namespace streaming_pressure;
  if (target == StreamingPressureLevel::Green)
  {
    return in.pending_light <= kExitPendingYellow &&
           in.dirty <= kExitDirtyYellow && in.frame_ms <= kExitWallYellowMs;
  }
  if (target == StreamingPressureLevel::Yellow)
  {
    return in.pending_light <= kExitPendingRed && in.dirty <= kExitDirtyRed &&
           in.frame_ms <= kExitWallRedMs;
  }
  return true;
}

/// Update hysteresis state and produce admission caps for this frame.
inline StreamingPressureCaps
EvaluateStreamingPressure(const StreamingPressureInput &in,
                          StreamingPressureState &state)
{
  using namespace streaming_pressure;
  const StreamingPressureLevel raw = RawStreamingPressureLevel(in);

  if (static_cast<int>(raw) > static_cast<int>(state.level))
  {
    state.level = raw;
    state.deescalate_hold_frames = kLevelHoldFrames;
  }
  else if (static_cast<int>(raw) < static_cast<int>(state.level))
  {
    const StreamingPressureLevel step_down =
        static_cast<StreamingPressureLevel>(static_cast<int>(state.level) - 1);
    if (CanExitTo(step_down, in))
    {
      if (state.deescalate_hold_frames > 0)
      {
        --state.deescalate_hold_frames;
      }
      if (state.deescalate_hold_frames <= 0)
      {
        state.level = step_down;
        state.deescalate_hold_frames = kLevelHoldFrames;
      }
    }
    else
    {
      state.deescalate_hold_frames = kLevelHoldFrames;
    }
  }
  else
  {
    state.deescalate_hold_frames = kLevelHoldFrames;
  }

  if (in.visual_holes || in.underfeet_need)
  {
    state.focus_mode_hold_frames = kFocusModeHoldFrames;
  }
  else if (state.focus_mode_hold_frames > 0)
  {
    --state.focus_mode_hold_frames;
  }

  StreamingPressureCaps caps;
  caps.level = state.level;
  caps.focus_pressure_mode = in.visual_holes || in.underfeet_need ||
                             state.focus_mode_hold_frames > 0;

  switch (state.level)
  {
  case StreamingPressureLevel::Green:
    caps.allow_fly_load_boost = true;
    caps.allow_prefetch = true;
    caps.max_load_ops_cap = -1;
    caps.max_commits_cap = -1;
    caps.recover_n_cap = -1;
    caps.mesh_fly_cap = -1;
    caps.bg_budget_floor = 0;
    break;
  case StreamingPressureLevel::Yellow:
    caps.allow_fly_load_boost = false;
    caps.allow_prefetch = true;
    caps.max_load_ops_cap = -1;
    caps.max_commits_cap = -1;
    caps.recover_n_cap = (in.dirty > 800) ? 4 : 8;
    caps.mesh_fly_cap = URuntimeTuning::Get().MeshFlyCapYellow;
    caps.bg_budget_floor = std::min(16, std::max(4, in.pending_light / 2));
    break;
  case StreamingPressureLevel::Red:
    caps.allow_fly_load_boost = false;
    caps.allow_prefetch = false;
    caps.max_load_ops_cap = 2;
    caps.max_commits_cap = 1;
    caps.recover_n_cap = (in.dirty > 800) ? 2 : 4;
    caps.mesh_fly_cap = URuntimeTuning::Get().MeshFlyCapRed;
    caps.bg_budget_floor = std::min(32, std::max(12, in.pending_light / 2));
    // Visual holes under Red: feed focus hard — westbound cruise hit Red at
    // pending~27 and starved load (holes latched for the whole flight).
    if (in.visual_holes || in.underfeet_need)
    {
      caps.max_load_ops_cap = 6;
      caps.max_commits_cap = 4;
      caps.bg_budget_floor = std::max(caps.bg_budget_floor, 24);
    }
    break;
  }
  // Do NOT uncap mesh/recover on holes — that defeated Red admission during
  // crisis. Reserved focus mesh slots (MeshPriorityQueue) replace uncap.
  return caps;
}

inline int ApplyPressureCap(int value, int cap)
{
  if (cap < 0)
  {
    return value;
  }
  return std::min(value, cap);
}

} // namespace cutum

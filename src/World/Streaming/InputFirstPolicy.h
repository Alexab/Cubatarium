#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

#include <algorithm>

namespace cutum
{

/// Inland cruise SLA used to decide whether speed-clamp may brake the player.
constexpr double kInputFirstWallSlaMs = 130.0;
constexpr double kInputFirstDoMovementSlaMs = 16.0;

inline bool IsInputFirstSlaBroken(double wall_ms, double do_movement_ms)
{
  return wall_ms > kInputFirstWallSlaMs ||
         do_movement_ms > kInputFirstDoMovementSlaMs;
}

/// Input-first A1: SLA uses player locomotion block, not full do_movement.
inline bool IsInputFirstPlayerSlaBroken(double wall_ms, double player_block_ms)
{
  return IsInputFirstSlaBroken(wall_ms, player_block_ms);
}

/// Underfeet reservation: guaranteed mesh drain/schedule when feet need a mesh
/// and light is already done. Does not raise Immediate or Capture.
struct UnderfeetReservation
{
  bool active{false};
  int mesh_drain_floor{0};
  int mesh_schedule_floor{0};
};

inline UnderfeetReservation EvaluateUnderfeetReservation(bool underfeet_need,
                                                         bool underfeet_has_mesh,
                                                         int pending_light)
{
  UnderfeetReservation out;
  if (!underfeet_need || underfeet_has_mesh)
  {
    return out;
  }
  if (pending_light > 0)
  {
    return out;
  }
  out.active = true;
  out.mesh_drain_floor = 8;
  out.mesh_schedule_floor = 6;
  return out;
}

inline void ApplyUnderfeetReservationFloors(int &mesh_drain, int &mesh_schedule,
                                            const UnderfeetReservation &res)
{
  if (!res.active)
  {
    return;
  }
  mesh_drain = std::max(mesh_drain, res.mesh_drain_floor);
  mesh_schedule = std::max(mesh_schedule, res.mesh_schedule_floor);
}

/// Leftover phase cap (~1.5 ms after stream overrun) must not starve nh≤1 apply.
inline bool ShouldProtectNearEmergeFromPhaseClamp(bool missing_underfeet,
                                                  int miss_horiz,
                                                  bool underfeet_has_mesh)
{
  return missing_underfeet || !underfeet_has_mesh || miss_horiz <= 1;
}

inline double ApplyPhaseEmergeClamp(double emerge_ms, double phase_cap_ms,
                                    bool protect_near)
{
  if (phase_cap_ms <= 0.0)
  {
    return emerge_ms;
  }
  if (protect_near)
  {
    return emerge_ms;
  }
  return std::min(emerge_ms, phase_cap_ms);
}

struct StreamSpeedClampInput
{
  bool moving{false};
  bool missing_underfeet{false};
  bool hole_towards{false};
  bool airborne{false};
  bool player_sla_broken{false};
  float border_scale{1.0f};
  /// Low-alt frontier: y below cruise band with void pressure.
  bool low_alt_frontier{false};
};

/// Flight clamp is softer than ground. If player SLA is already broken, do not
/// brake locomotion — streaming must yield, not the controller.
inline float ComputeStreamSpeedClampScale(const StreamSpeedClampInput &in)
{
  if (!in.moving || in.player_sla_broken)
  {
    return 1.0f;
  }
  float scale = in.border_scale > 0.0f ? std::min(1.0f, in.border_scale) : 1.0f;
  if (in.low_alt_frontier)
  {
    const float low_alt = in.airborne ? 0.85f : 0.7f;
    scale = std::min(scale, low_alt);
  }
  if (in.missing_underfeet || in.hole_towards)
  {
    const float integrity = in.airborne ? 0.95f : 0.85f;
    scale = std::min(scale, integrity);
  }
  return scale;
}

/// Enter burst: defer far Capture first N frames after gate (underfeet nh≤2 win).
inline bool ShouldDeferFarRelightDuringEnterBurst(int frames_since_enter_lit,
                                                  int burst_frames = 90)
{
  return frames_since_enter_lit >= 0 &&
         frames_since_enter_lit < burst_frames;
}

} // namespace cutum

#pragma once

#include <algorithm>

namespace cutum
{

/// Pure idle-recovery policy (I1/I2): tiered visual drain gate + hot-frame
/// Capture budget cap. Extracted from ChunkEmergeCoordinator / WorldStreaming.

struct IdleVisualDrainInput
{
  double last_frame_ms{0.0};
  int idle_visual_drain_cd{0};
  int pending_focus_count{0};
  bool missing_visible_mesh{false};
  int idle_pending_plateau_frames{0};
};

struct IdleVisualDrainDecision
{
  bool run_drain{false};
  int budget{0};
  bool allow_sync{false};
  int idle_visual_drain_cd_next{0};
};

inline int BaseIdleVisualDrainBudget(int pending_focus_count)
{
  if (pending_focus_count > 30)
  {
    return 8;
  }
  if (pending_focus_count > 15)
  {
    return 6;
  }
  if (pending_focus_count > 0)
  {
    return 4;
  }
  return 3;
}

inline IdleVisualDrainDecision EvaluateIdleVisualDrain(
    const IdleVisualDrainInput &in)
{
  IdleVisualDrainDecision out;
  if (in.idle_visual_drain_cd > 0)
  {
    return out;
  }

  const int base_budget = BaseIdleVisualDrainBudget(in.pending_focus_count);
  const double ms = in.last_frame_ms;

  if (ms > 120.0)
  {
    return out;
  }

  if (ms > 80.0)
  {
    if (in.pending_focus_count <= 30 && !in.missing_visible_mesh)
    {
      return out;
    }
    out.run_drain = true;
    out.budget = 1;
    out.allow_sync = false;
  }
  else if (ms > 55.0)
  {
    out.run_drain = true;
    out.budget = std::max(1, (base_budget + 3) / 4);
    out.allow_sync = false;
  }
  else if (ms > 28.0)
  {
    out.run_drain = true;
    out.budget = std::max(1, (base_budget + 1) / 2);
    out.allow_sync = false;
  }
  else
  {
    out.run_drain = true;
    out.budget = base_budget;
    const int plateau_frames = in.missing_visible_mesh ? 12 : 45;
    const double plateau_wall_ms = in.missing_visible_mesh ? 120.0 : 18.0;
    out.allow_sync =
        in.idle_pending_plateau_frames >= plateau_frames &&
        ms <= plateau_wall_ms && in.pending_focus_count > 0 && ms <= 40.0;
  }

  if (out.run_drain)
  {
    out.idle_visual_drain_cd_next =
        in.pending_focus_count > 8 ? 1
                                   : (in.pending_focus_count > 0 ? 2 : 8);
  }
  return out;
}

struct StickyRemeshDrainInput
{
  int black_sticky{0};
  double last_frame_ms{0.0};
};

struct StickyRemeshDrainDecision
{
  bool run_drain{false};
  int budget{0};
};

inline StickyRemeshDrainDecision EvaluateStickyRemeshDrain(
    const StickyRemeshDrainInput &in)
{
  StickyRemeshDrainDecision out;
  if (in.black_sticky <= 0 || in.last_frame_ms > 80.0)
  {
    return out;
  }
  out.run_drain = true;
  if (in.last_frame_ms <= 55.0)
  {
    out.budget =
        in.black_sticky > 4 ? 3 : (in.black_sticky > 1 ? 2 : 1);
  }
  else
  {
    out.budget = 1;
  }
  return out;
}

struct IdleFocusDirtyDebtInput
{
  bool moving{false};
  int pending_focus_count{0};
  int black_sticky{0};
  bool missing_visible_mesh{false};
  int focus_dirty_early{0};
  int prev_focus_dirty{0};
  int high_frames{0};
};

struct IdleFocusDirtyDebtDecision
{
  bool active{false};
  int prev_focus_dirty_next{0};
  int high_frames_next{0};
};

/// I4: avoid latching lit-but-dirty debt unless it persists and does not improve.
inline IdleFocusDirtyDebtDecision EvaluateIdleFocusDirtyDebt(
    const IdleFocusDirtyDebtInput &in)
{
  IdleFocusDirtyDebtDecision out;
  out.prev_focus_dirty_next = in.focus_dirty_early;

  if (in.moving || in.black_sticky > 0 || in.pending_focus_count > 16)
  {
    out.high_frames_next = 0;
    return out;
  }

  const bool threshold_hit =
      in.missing_visible_mesh ? (in.focus_dirty_early > 320)
                              : (in.focus_dirty_early > 280);
  if (!threshold_hit)
  {
    out.high_frames_next = 0;
    return out;
  }

  const int dirty_delta = in.focus_dirty_early - in.prev_focus_dirty;
  const bool not_improving = dirty_delta >= -8;
  out.high_frames_next = not_improving ? (in.high_frames + 1) : 0;
  out.active = out.high_frames_next >= 3;
  return out;
}

struct IdleRecoveryBgBudgetInput
{
  bool idle_recovery{false};
  double frame_ms{0.0};
  double k_bad_frame_ms{24.0};
  int pending_light_focus_n{0};
  int black_sticky_focus{0};
  bool missing_focus_mesh{false};
  int mesh_async_n{0};
  int bg_budget_in{0};
};

/// Remnant dark-face count that is still "perf-calm" (matches analyze
/// classify_stop_period: sticky alone is visual, not light-debt recovery).
/// I4b idle-clean: black_sticky=2 blocked all I4b caps → sync~36ms/frame.
inline constexpr int kIdleCalmStickyRemnant = 2;

struct IdleMeshDrainCapInput
{
  bool moving{false};
  bool missing_visible_mesh{false};
  int pending_focus_count{0};
  int black_sticky{0};
  int not_ready_early{0};
  double last_frame_ms{0.0};
  int mesh_drain{0};
  int mesh_schedule{0};
};

struct IdleMeshDrainCapDecision
{
  bool active{false};
  int mesh_drain{0};
  int mesh_schedule{0};
  double snapshot_budget_ms{0.0};
  /// Hard wall for RebuildDirtyChunksWithStats (dirty_tick dominant on calm).
  double emerge_total_budget_ms{0.0};
  int sync_cap{0};
  double sync_budget_ms{0.0};
};

/// Calm stand with no light/mesh debt: pace dirty_tick so wall can recover.
/// I4b: also cap emerge total + SyncRebuild.
/// I4d: sticky remnant keeps remesh drain (I4c drain=2 grew sticky).
/// I4e: do not early-out on sticky — I4d calm had sticky=8 so caps never fired
/// and SyncRebuild still burned ~35ms. Sticky remesh is async; SyncRebuild is
/// for missing first-mesh (already gated by missing_visible_mesh).
inline IdleMeshDrainCapDecision EvaluateIdleMeshDrainCap(
    const IdleMeshDrainCapInput &in)
{
  IdleMeshDrainCapDecision out;
  out.mesh_drain = in.mesh_drain;
  out.mesh_schedule = in.mesh_schedule;
  if (in.moving || in.missing_visible_mesh || in.pending_focus_count > 0 ||
      in.not_ready_early > 0)
  {
    return out;
  }
  const bool remnant_sticky = in.black_sticky > 0;
  if (in.last_frame_ms > 55.0)
  {
    out.active = true;
    if (remnant_sticky)
    {
      out.mesh_drain = std::min(in.mesh_drain, 8);
      out.mesh_schedule = std::min(in.mesh_schedule, 8);
      out.snapshot_budget_ms = 2.0;
      out.emerge_total_budget_ms = 12.0;
    }
    else
    {
      out.mesh_drain = std::min(in.mesh_drain, 2);
      out.mesh_schedule = std::min(in.mesh_schedule, 3);
      out.snapshot_budget_ms = 1.0;
      out.emerge_total_budget_ms = 8.0;
    }
    out.sync_cap = 0;
    out.sync_budget_ms = 0.5;
  }
  else if (in.last_frame_ms > 28.0)
  {
    out.active = true;
    if (remnant_sticky)
    {
      out.mesh_drain = std::min(in.mesh_drain, 10);
      out.mesh_schedule = std::min(in.mesh_schedule, 10);
      out.snapshot_budget_ms = 2.0;
      out.emerge_total_budget_ms = 16.0;
    }
    else
    {
      out.mesh_drain = std::min(in.mesh_drain, 3);
      out.mesh_schedule = std::min(in.mesh_schedule, 4);
      out.snapshot_budget_ms = 1.5;
      out.emerge_total_budget_ms = 12.0;
    }
    out.sync_cap = 0;
    out.sync_budget_ms = 0.5;
  }
  else if (in.last_frame_ms > 16.0)
  {
    // Mild headroom toward IDLE_CLEAN ≤55 wall while FPS is already OK-ish.
    out.active = true;
    out.mesh_drain = std::min(in.mesh_drain, remnant_sticky ? 10 : 6);
    out.mesh_schedule = std::min(in.mesh_schedule, remnant_sticky ? 10 : 6);
    out.snapshot_budget_ms = 2.0;
    out.emerge_total_budget_ms = remnant_sticky ? 20.0 : 16.0;
    out.sync_cap = 0;
    out.sync_budget_ms = 1.0;
  }
  return out;
}

inline int ComputeIdleRecoveryBgBudget(const IdleRecoveryBgBudgetInput &in)
{
  if (!in.idle_recovery)
  {
    return in.bg_budget_in;
  }

  int bg = in.bg_budget_in;
  const bool hot = in.frame_ms > in.k_bad_frame_ms;
  if (hot)
  {
    bg = std::max(bg, 4);
    if (in.black_sticky_focus > 0 || in.mesh_async_n >= 40 ||
        in.missing_focus_mesh)
    {
      bg = std::max(bg, 6);
    }
    else if (in.pending_light_focus_n > 15)
    {
      bg = std::max(bg, 4);
    }
  }
  else
  {
    bg = std::max(bg, 32);
    if (in.black_sticky_focus > 0 || in.mesh_async_n >= 40 ||
        in.pending_light_focus_n > 15 || in.missing_focus_mesh)
    {
      bg = std::max(bg, 40);
    }
    if (in.pending_light_focus_n > 15 && in.black_sticky_focus == 0 &&
        !in.missing_focus_mesh)
    {
      bg = std::max(bg, 48);
    }
  }
  return bg;
}

} // namespace cutum

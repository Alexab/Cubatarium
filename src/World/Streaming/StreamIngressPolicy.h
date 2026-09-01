#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>
#include <glm/glm.hpp>

namespace cutum
{

/// I18 hotfix: Tier-2 witness comfort gated until Gate A passes on clean cruise.
inline constexpr bool kI18WitnessComfortEnabled = false;
/// I18 hotfix.4: narrow underfeet drawable hold (miss_horiz≤1) without full Tier-2.
inline constexpr bool kI18UnderfeetGraceEnabled = true;
inline constexpr int kI18UnderfeetGraceFrames = 2;

/// I17-P1: ring resync only when focus/keep ring actually changed.
inline bool ShouldRefreshRingResyncForFocusJump(bool focus_ground_jumped,
                                                bool keep_cols_changed,
                                                bool ring_sample_valid,
                                                int frame_epoch_delta,
                                                int witness_retarget_delta = 0,
                                                int max_epoch_reuse = 4)
{
  if (witness_retarget_delta > 0 && !focus_ground_jumped && !keep_cols_changed)
  {
    return false;
  }
  if (focus_ground_jumped || keep_cols_changed || !ring_sample_valid)
  {
    return true;
  }
  return frame_epoch_delta > max_epoch_reuse;
}

/// I17-P1: cruise unfinished sample cadence (frames between full ring walks).
inline int UnfinishedSampleCooldownFrames(int unfinished_visual)
{
  return unfinished_visual <= 1 ? 24 : 12;
}

/// I18-B4: restore I12 rim fast-path cadence when stable cruise rim.
inline int UnfinishedSampleCooldownFramesCruise(bool diet_cruise, int miss_horiz,
                                                int unfinished_visual)
{
  if (diet_cruise && miss_horiz >= 2 && miss_horiz <= 4 && unfinished_visual <= 1)
  {
    return 32;
  }
  return UnfinishedSampleCooldownFrames(unfinished_visual);
}

/// I17-P1: reuse cached unfinished between sample ticks on cruise.
inline bool ShouldReuseUnfinishedVisualSample(bool diet_cruise, bool visual_holes,
                                              bool pending_underfeet,
                                              int sample_cd_remaining)
{
  return diet_cruise && !visual_holes && !pending_underfeet &&
         sample_cd_remaining > 0;
}

/// I17-P1: VB raw scan cadence — throttle when stalled plateau stable.
inline int VbRawScanCadenceFrames(bool diet_cruise, int vb_focus_stable_frames,
                                  int vb_stalled_n, int vb_published)
{
  if (!diet_cruise)
  {
    return vb_focus_stable_frames >= 4 ? 10 : 4;
  }
  if (vb_stalled_n >= 15 && vb_focus_stable_frames >= 4)
  {
    return 16;
  }
  if (vb_published < 20)
  {
    return 8;
  }
  return 10;
}

/// I17-P2: defer revision bump on rim while GPU apply in flight (drawable held).
inline bool ShouldDeferRimRevisionBumpForPendingGpu(bool focus_valid, int horiz,
                                                  bool pending_gpu,
                                                  bool has_drawable,
                                                  bool witness_hop_window = false)
{
  if (witness_hop_window && focus_valid && has_drawable && horiz >= 0 &&
      horiz <= 4)
  {
    return true;
  }
  return focus_valid && pending_gpu && has_drawable && horiz >= 0 && horiz <= 4;
}

/// I17-P2: cruise moving rim ingress FM schedule floor.
inline int RimIngressFmScheduleFloor(bool moving, int miss_horiz, int dirty_fm_n)
{
  if (!moving || miss_horiz < 2 || miss_horiz > 4 || dirty_fm_n <= 0)
  {
    return 0;
  }
  return std::min(4, std::max(1, dirty_fm_n));
}

/// I18-A2: chain stall kick threshold (frames). Hotfix: restore 8f default.
inline int RimChainStallKickFrames(bool schedule_starved)
{
  return schedule_starved ? 4 : 8;
}

/// I18-F: ingress debt levels for coordinated load shedding.
enum class IngressDebtLevel : uint8_t
{
  Ok = 0,
  Watch = 1,
  ShedFar = 2,
  ShedRim = 3,
};

struct IngressDebtInput
{
  bool moving{false};
  int chain_progress_frames{0};
  int schedule_ok_n{0};
  int dirty_fm_n{0};
  int fm_dirty_gpu_watch_max_age{0};
  int fm_dirty_gpu_watch_n{0};
  int softdefer_witness_retarget_delta{0};
  int miss_horiz{0};
  int fm_dirty_to_gpu_finish_n{0};
  int visible_black_focus_n{0};
};

/// I18-F1: chain stall predicate (watch + zero FM→GPU finish).
inline bool IsIngressChainStalled(const IngressDebtInput &in)
{
  if (in.dirty_fm_n <= 0)
  {
    return false;
  }
  if (in.fm_dirty_to_gpu_finish_n == 0 &&
      (in.chain_progress_frames == 0 || in.fm_dirty_gpu_watch_max_age >= 8))
  {
    return true;
  }
  return in.chain_progress_frames == 0 && in.fm_dirty_gpu_watch_n > 0;
}

/// I18-A1: FM dirty GPU watch coord on rim ingress ring.
inline bool IsRimIngressWatchCoord(glm::ivec3 coord, glm::ivec3 focus_ground,
                                  int max_horiz = 4)
{
  const int horiz =
      std::max(std::abs(coord.x - focus_ground.x),
               std::abs(coord.z - focus_ground.z));
  return horiz >= 0 && horiz <= max_horiz;
}

/// I18-F3: dynamic kick_cut bias from FM watch depth on rim.
inline double DynamicKickCutBiasForFmWatch(int watch_rim_n, double base_kick_cut)
{
  if (watch_rim_n <= 0)
  {
    return base_kick_cut;
  }
  const int capped = std::min(watch_rim_n, 4);
  return std::max(base_kick_cut, 0.70 + 0.05 * static_cast<double>(capped));
}

/// I17-P3: consume ticketed VB on cruise when stalled plateau without PL debt.
inline bool ShouldConsumeTicketedVbStalledCruise(bool moving, int vb_stalled_n,
                                                 int pending_light_focus_n,
                                                 int visible_black_focus_n,
                                                 int stalled_floor = 10,
                                                 int vb_focus_floor = 25)
{
  return moving && pending_light_focus_n == 0 &&
         vb_stalled_n >= stalled_floor &&
         visible_black_focus_n >= vb_focus_floor;
}

/// I17-P0: effective-holes blink transition detector.
inline bool IsBlinkTransition(int prev_unfinished, int cur_unfinished)
{
  if (cur_unfinished <= 0)
  {
    return false;
  }
  if (prev_unfinished <= 0)
  {
    return true;
  }
  return cur_unfinished >= prev_unfinished + 2;
}

inline IngressDebtLevel EvaluateIngressDebt(const IngressDebtInput &in,
                                            int watch_streak_periods)
{
  const bool ingress_pressure =
      in.moving ||
      (in.visible_black_focus_n >= 20 && in.dirty_fm_n > 0);
  if (!ingress_pressure)
  {
    return IngressDebtLevel::Ok;
  }
  const bool chain_stall = IsIngressChainStalled(in);
  if (!chain_stall)
  {
    return IngressDebtLevel::Ok;
  }
  if (watch_streak_periods < 2)
  {
    return IngressDebtLevel::Watch;
  }
  if (in.schedule_ok_n < 2 || in.fm_dirty_gpu_watch_max_age > 30)
  {
    if (in.softdefer_witness_retarget_delta > 0 && in.miss_horiz >= 2 &&
        in.miss_horiz <= 4)
    {
      return IngressDebtLevel::ShedRim;
    }
    return IngressDebtLevel::ShedFar;
  }
  if (in.softdefer_witness_retarget_delta > 0)
  {
    return IngressDebtLevel::ShedRim;
  }
  return IngressDebtLevel::ShedFar;
}

/// I18-F5: rate limit better_horiz hops under ingress debt.
inline bool ShouldRateLimitWitnessRetargetUnderDebt(
    IngressDebtLevel debt, int periods_since_last_retarget, bool visual_holes,
    int rate_limit_periods = 48)
{
  if (visual_holes)
  {
    return false;
  }
  if (debt < IngressDebtLevel::ShedFar)
  {
    return false;
  }
  return periods_since_last_retarget < rate_limit_periods;
}

/// I18-D1: hold prior column drawable briefly on witness column swap.
struct WitnessSwapGrace
{
  glm::ivec2 prior_xz{INT_MAX, INT_MAX};
  int frames_left{0};
};

inline bool ShouldHoldPriorColumnDrawableOnWitnessSwap(bool had_drawable,
                                                     bool new_column_ready,
                                                     int nh, int frames_left)
{
  return had_drawable && !new_column_ready && nh >= 0 && nh <= 4 &&
         frames_left > 0;
}

inline bool IsWitnessSwapGraceActive(const WitnessSwapGrace &grace,
                                   glm::ivec2 coord_xz)
{
  return grace.frames_left > 0 && grace.prior_xz.x == coord_xz.x &&
         grace.prior_xz.y == coord_xz.y;
}

} // namespace cutum

#pragma once

#include <algorithm>
#include <cstdint>

namespace cutum
{

/// I17-P1: ring resync only when focus/keep ring actually changed.
inline bool ShouldRefreshRingResyncForFocusJump(bool focus_ground_jumped,
                                                bool keep_cols_changed,
                                                bool ring_sample_valid,
                                                int frame_epoch_delta,
                                                int max_epoch_reuse = 4)
{
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
                                                  bool has_drawable)
{
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

} // namespace cutum

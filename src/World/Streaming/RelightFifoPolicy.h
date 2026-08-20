#pragma once

#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>

namespace cutum
{

/// Era40: miss / SoftDefer-empty Relight pin covers LitDrawable ring (not only
/// near horiz<=2). Matches hide-until-lit publication radius.
inline int RelightMissPinMaxHoriz(int ring = kVisualStageLitDrawableHoriz)
{
  return ring;
}

/// P1: far FIFO overflow must not pop the pinned miss / PromoteRelightHold key.
inline bool ShouldProtectRelightFifoPinKey(int victim_cx, int victim_cz,
                                           bool pin_valid, int pin_cx,
                                           int pin_cz)
{
  return pin_valid && victim_cx == pin_cx && victim_cz == pin_cz;
}

/// P1: nh≤2 pin always lives in the priority deque (not far overflow).
inline bool ShouldForcePinColumnPriority(bool is_pin_key, int miss_horiz)
{
  return is_pin_key && miss_horiz >= 0 &&
         miss_horiz <= kVisualStageNearFovHoriz;
}

/// P2/P6: cruise Apply count — at least 1; up to 3 when last apply cheap + pin stable.
inline int CruiseRelightApplyBudget(bool moving, double last_apply_ms,
                                    int requested, bool fifo_pin_stable,
                                    bool near_pending_light = false)
{
  (void)near_pending_light;
  if (requested <= 0)
  {
    return 0;
  }
  if (!moving)
  {
    return requested;
  }
  if (last_apply_ms > 8.0)
  {
    return 1;
  }
  int cap = 1;
  // ColPipe P6: cheap Apply under stable pin — up to 3 (was 2).
  if (fifo_pin_stable && last_apply_ms >= 0.0 && last_apply_ms < 4.0)
  {
    cap = 3;
  }
  else if (fifo_pin_stable && last_apply_ms < 5.0)
  {
    cap = 2;
  }
  return requested < cap ? requested : cap;
}

/// P3: skip heavy apply side-effects when prior apply already blew SLA.
inline bool ShouldDeferHeavyApplySideEffects(double last_apply_ms)
{
  return last_apply_ms > 8.0;
}

/// Era40: force FIFO Enqueue for FOV miss even when Keys/FIFO ghost-empty.
inline bool ShouldForceMissColumnFifoEnqueue(bool miss_or_visual_hole,
                                             bool pending_or_void_or_undrawn,
                                             bool already_in_fifo)
{
  if (!miss_or_visual_hole || !pending_or_void_or_undrawn)
  {
    return false;
  }
  return !already_in_fifo;
}

/// P2: unsplit finalize (`finalize_gate=true`) only for near-FOV witness
/// (nh≤2). Rim nh=3–4 stays Y-band split so cruise Capture cannot hitch like
/// Era40 full-ring finalize (drain max 597 ms on 195810).
inline bool ShouldPreferMissFinalizeBand(int miss_horiz,
                                         int ring = kVisualStageNearFovHoriz)
{
  return miss_horiz >= 0 && miss_horiz <= ring;
}

/// Hold nh≤2 witness until MarkRelit or greedy appears — pending OR missing.
/// 215411: miss was often cy=2 without pending on that key, so hold never armed.
inline bool ShouldHoldPinnedRelightWitness(int pinned_horiz,
                                          bool pinned_still_pending,
                                          bool pinned_still_missing = false)
{
  if (pinned_horiz < 0 || pinned_horiz > kVisualStageNearFovHoriz)
  {
    return false;
  }
  return pinned_still_pending || pinned_still_missing;
}

/// Overlay on Era27 SoftDefer retarget: nh≤2 pending hold wins over pin_T /
/// better_horiz hop (`softdefer_witness_retarget` 0→192 on 195810).
inline bool ShouldRetargetRelightWitness(bool era27_retarget,
                                         bool hold_nh2_pending)
{
  if (hold_nh2_pending)
  {
    return false;
  }
  return era27_retarget;
}

/// P3: FirstMeshQ head = underfeet or just-MarkRelit nh≤2 (not a quota bump).
inline bool ShouldFirstMeshSortBoost(int horiz, bool just_relit_column)
{
  if (horiz == 0)
  {
    return true;
  }
  return just_relit_column && horiz >= 0 &&
         horiz <= kVisualStageNearFovHoriz;
}

/// F3b: skip terrain relight FIFO when column is lit-settled and surface has
/// no FullyDark drawable faces in the requested band.
inline bool ShouldSkipNoOpTerrainRelightEnqueue(bool pending_light_before_mesh,
                                                bool column_lit_ready,
                                                bool surface_band_needs_relight)
{
  if (pending_light_before_mesh || !column_lit_ready)
  {
    return false;
  }
  return !surface_band_needs_relight;
}

/// F3d: under FIFO pressure, defer relight enqueue outside LitDrawable pin ring.
inline bool ShouldDeferFarRelightEnqueueOnFifoPressure(int horiz_from_focus,
                                                       int pin_horiz, int fifo_n,
                                                       int soft_cap,
                                                       float fifo_admit_frac)
{
  if (soft_cap <= 0 || horiz_from_focus <= pin_horiz)
  {
    return false;
  }
  const int thresh = static_cast<int>(
      static_cast<float>(soft_cap) * std::max(0.1f, fifo_admit_frac));
  return fifo_n >= thresh;
}

/// Era40: FIFO full under FOV miss but no apply/inflight progress ⇒ raise Capture.
/// Do not use RelightCompletedN (ring occupancy) — Apply drains it same frame.
inline bool ShouldBoostRelightDrainUnderFifoMissStarve(int fifo_n, int soft_cap,
                                                       int async_inflight,
                                                       bool miss_or_visual_hole,
                                                       double relight_apply_ms_prev)
{
  if (!miss_or_visual_hole || soft_cap <= 0 || fifo_n < soft_cap)
  {
    return false;
  }
  if (async_inflight > 0)
  {
    return false;
  }
  return relight_apply_ms_prev < 0.5;
}

/// Cruise wall P4: Red + fifo≥frac*cap + holes + focus light debt → Capture/trim SLA.
inline bool ShouldCruiseRedFifoLightDrain(int stream_pressure, int fifo_n,
                                          int soft_cap, float fifo_frac,
                                          bool holes_or_miss,
                                          int pending_light_focus)
{
  if (stream_pressure < 2 || !holes_or_miss || pending_light_focus <= 0 ||
      soft_cap <= 0)
  {
    return false;
  }
  const int thresh = static_cast<int>(
      static_cast<float>(soft_cap) * std::max(0.1f, fifo_frac));
  return fifo_n >= thresh;
}

/// Era40 P3: analyze soft-fail when FIFO stuck + dropped churn + no apply.
inline bool RelightFifoStuckSoftFail(int fifo_med, int soft_cap,
                                     double relight_apply_ms_med,
                                     int fifo_dropped_delta,
                                     bool miss_end_or_stuck)
{
  if (!miss_end_or_stuck || soft_cap <= 0)
  {
    return false;
  }
  return fifo_med >= soft_cap - 1 && relight_apply_ms_med < 0.5 &&
         fifo_dropped_delta > 0;
}

} // namespace cutum

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

/// Cold cruise high PL: use Apply floor matching Capture rate (not Enter×64).
/// Thresh 30 matches TickAsyncChunkSystems pending_light_focus_n > 30.
inline bool ShouldUseHighPlCruiseApplyFloor(bool moving,
                                            int pending_light_focus_n,
                                            int enter_pl_thresh = 30)
{
  return moving && pending_light_focus_n > enter_pl_thresh;
}

/// Floor so Apply can keep pace with DynamicCaptureMovingBgCap (≤2).
inline int HighPlCruiseApplyFloorN()
{
  return 4;
}

/// RateMatch R0: stop Apply slice after ≥1 column when wall ≥ MissReservedMs.
/// Enter FOV lit pass is exempt (full Enter budget).
inline bool ShouldStopRelightApplySlice(double elapsed_ms, int applied_n,
                                        double slice_ms, bool enter_pass)
{
  if (enter_pass || applied_n < 1)
  {
    return false;
  }
  return elapsed_ms >= slice_ms;
}

/// S0: Apply drain count = min(budget, ready). Budget ≤0 → 0.
inline int ClampRelightDrainN(int budget, int ready_n)
{
  if (budget <= 0 || ready_n <= 0)
  {
    return 0;
  }
  return budget < ready_n ? budget : ready_n;
}

/// Admit Capture iff pipeline depth below Apply capacity (not worker max alone).
inline bool ShouldAdmitRelightCapture(int completed_n, int inflight_n,
                                      int depth_cap)
{
  if (depth_cap <= 0)
  {
    return true;
  }
  return (completed_n + inflight_n) < depth_cap;
}

/// SoftDefer/miss may keep one Capture slot even when depth admit is false.
inline int SoftDeferCaptureFloorWhenDepthFull(bool soft_defer_or_miss_hole,
                                              int bg_cap)
{
  if (!soft_defer_or_miss_hole)
  {
    return bg_cap;
  }
  return bg_cap < 1 ? 1 : bg_cap;
}

/// Keep live GPU draw even if FullyDark face flag is set while repair is in
/// progress. RateMatch R2: LitDrawable ring (default), not underfeet-only.
inline bool ShouldKeepLiveGpuOpaqueDespiteFullyDark(
    bool has_live_gpu_draw, int horiz, bool has_repair_progress,
    int keep_horiz = kVisualStageLitDrawableHoriz)
{
  return has_live_gpu_draw && has_repair_progress && horiz >= 0 &&
         horiz <= keep_horiz;
}

/// P2/P6/LitRing/Flicker: cruise Apply — caps from per-column unit cost, not
/// batch wall. Near-PL floor preserved. Early-out cap=1 only when unit_ms>8
/// (or NPrev≤1 with batch>8).
inline int CruiseRelightApplyBudget(bool moving, double last_apply_ms,
                                    int requested, bool fifo_pin_stable,
                                    bool near_pending_light = false,
                                    int last_apply_n = 0)
{
  if (requested <= 0)
  {
    return 0;
  }
  if (!moving)
  {
    return requested;
  }
  const double unit_ms =
      (last_apply_n > 0)
          ? (last_apply_ms / static_cast<double>(last_apply_n))
          : last_apply_ms;
  const bool unit_blow =
      unit_ms > 8.0 || (last_apply_n <= 1 && last_apply_ms > 8.0);
  if (unit_blow)
  {
    // Probe only while prior batch was near the 8ms edge (manual ~8.2).
    // Hot batches (>12) stay cap=1 so wall_fly does not explode.
    if (fifo_pin_stable && last_apply_n <= 1 && last_apply_ms <= 12.0)
    {
      const int probe = near_pending_light ? 3 : 2;
      return requested < probe ? requested : probe;
    }
    return 1;
  }
  int cap = 1;
  // Manual 093804: apply_ms med≈4.8 kept cap at 1–2 and PL stuck ~57.
  // With unit-cost, same thresholds apply to per-column ms.
  if (fifo_pin_stable && unit_ms >= 0.0 && unit_ms < 3.0)
  {
    cap = 6;
  }
  else if (fifo_pin_stable && unit_ms < 5.0)
  {
    cap = 4;
  }
  else if (fifo_pin_stable && unit_ms < 6.5)
  {
    cap = 3;
  }
  else if (fifo_pin_stable && unit_ms < 8.0)
  {
    cap = 2;
  }
  if (near_pending_light && fifo_pin_stable && unit_ms < 8.0)
  {
    cap = cap < 4 ? 4 : cap;
  }
  return requested < cap ? requested : cap;
}

/// P3: skip heavy apply side-effects when prior per-column apply blew SLA.
inline bool ShouldDeferHeavyApplySideEffects(double last_apply_ms,
                                             int last_apply_n = 0)
{
  const double unit_ms =
      (last_apply_n > 0)
          ? (last_apply_ms / static_cast<double>(last_apply_n))
          : last_apply_ms;
  return unit_ms > 8.0 || (last_apply_n <= 1 && last_apply_ms > 8.0);
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

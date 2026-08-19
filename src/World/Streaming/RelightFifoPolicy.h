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

/// P2: cruise Apply count — at least 1; second only if last apply was cheap.
/// Near-pending hint is accepted for callsite compatibility, but moving apply
/// remains capped at 2 to keep Apply cheap and predictable under cruise load.
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
  if (fifo_pin_stable && last_apply_ms >= 0.0 && last_apply_ms < 5.0)
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

/// Era40: soft-cap FIFO stuck with empty Completed under FOV miss ⇒ raise drain.
inline bool ShouldBoostRelightDrainUnderFifoMissStarve(int fifo_n, int soft_cap,
                                                       int completed_n,
                                                       bool miss_or_visual_hole)
{
  if (!miss_or_visual_hole || soft_cap <= 0)
  {
    return false;
  }
  return fifo_n >= soft_cap && completed_n <= 0;
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

/// Era40 P3: analyze soft-fail when FIFO stuck + dropped churn + no completed.
inline bool RelightFifoStuckSoftFail(int fifo_med, int soft_cap,
                                     int completed_med, int fifo_dropped_delta,
                                     bool miss_end_or_stuck)
{
  if (!miss_end_or_stuck || soft_cap <= 0)
  {
    return false;
  }
  return fifo_med >= soft_cap - 1 && completed_med <= 0 &&
         fifo_dropped_delta > 0;
}

} // namespace cutum

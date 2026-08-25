#pragma once

#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>

namespace cutum
{

/// FZ2.6-Perf0: why Apply loop stopped (binding constraint SoT).
enum class ApplyBinding : uint8_t
{
  QueueEmpty = 0,
  TimeSlice = 1,
  CountCap = 2,
  FatUnit = 3,
};

/// FZ2.6-Perf1: consume idle slice (not defer stage).
inline double RelightConsumeSliceMs(double miss_reserved_ms, bool consume_mode,
                                    bool moving)
{
  if (consume_mode && !moving)
  {
    return std::max(miss_reserved_ms, 16.0);
  }
  return miss_reserved_ms;
}

/// FZ2.6-Perf2 / FZ2.7-C: do not boost Capture/mesh when consumer-bound.
/// Cost-bound: light_unit > slice/3 even if fifo is not at cap.
inline bool ShouldSuppressProducerBoostWhenConsumerBound(
    int apply_n_prev, int fifo_n, int soft_cap, ApplyBinding binding,
    double last_light_unit_ms = 0.0, double slice_ms = 0.0)
{
  if (apply_n_prev >= 2)
  {
    return false;
  }
  const bool fifo_bound = soft_cap > 0 && fifo_n >= soft_cap - 1;
  const bool cost_bound =
      slice_ms > 0.1 && last_light_unit_ms > slice_ms / 3.0;
  if (!fifo_bound && !cost_bound)
  {
    return false;
  }
  if (cost_bound)
  {
    return true;
  }
  return binding == ApplyBinding::TimeSlice || binding == ApplyBinding::FatUnit;
}

/// FZ2.7-P9: never suppress Capture refill while Completed is empty and fifo piled.
inline bool ShouldSuppressProducerBoostWhenConsumerBoundP9(
    int apply_n_prev, int fifo_n, int soft_cap, ApplyBinding binding,
    double last_light_unit_ms, double slice_ms, int completed_n)
{
  if (completed_n <= 0 &&
      (fifo_n >= 50 || (soft_cap > 0 && fifo_n >= soft_cap / 2)))
  {
    return false;
  }
  return ShouldSuppressProducerBoostWhenConsumerBound(
      apply_n_prev, fifo_n, soft_cap, binding, last_light_unit_ms, slice_ms);
}

/// FZ2.7-P9: sim kill-switch — no Capture/Apply producer boost.
inline bool ShouldKillProducerBoostOnSimHot(double sim_ms_prev,
                                            double kill_ms = 135.0)
{
  return sim_ms_prev > kill_ms;
}

/// FZ2.7-P9: Apply floor/budget raise only when Completed has work.
inline bool ShouldRaiseApplyBudgetOnlyWhenReady(int ready_n)
{
  return ready_n >= 1;
}

/// FZ2.7-P9: one-shot MarkMissing on LitReady slim — not every Apply.
inline bool ShouldMarkMissingOnceOnLitReady(bool finalize_gate,
                                            bool slim_or_consume, int schedule_n,
                                            int focus_horiz,
                                            bool focus_has_no_mesh_debt,
                                            int protect_horiz =
                                                kVisualStageProtectHoriz)
{
  if (!finalize_gate || !slim_or_consume || schedule_n > 0)
  {
    return false;
  }
  if (!focus_has_no_mesh_debt)
  {
    return false;
  }
  return focus_horiz >= 0 && focus_horiz <= protect_horiz;
}

/// FZ2.7-P9: !drawable protect ring must not leave-in SoftDefer thrash.
inline bool ShouldLeaveInDirtyUnderPlForSchedule(bool leave_in_pl_policy,
                                                 bool has_drawable)
{
  return leave_in_pl_policy && has_drawable;
}

/// FZ2.7-P12 B1/B2: do not trim PendingLight while holes + unfinished/PL debt.
inline bool ShouldTrimPendingLightUnderHoles(bool visual_holes, int unfinished,
                                             int pending_light_focus)
{
  if (!visual_holes)
  {
    return true;
  }
  if (unfinished > 20 || pending_light_focus > 15)
  {
    return false;
  }
  return true;
}

/// FZ2.7-P12 B4: already-Noted hole — do not expand PL bands (churn).
inline bool ShouldSuppressDuplicatePendingLightWithoutMeshProgress(
    bool already_pending, bool has_greedy_mesh)
{
  return already_pending && !has_greedy_mesh;
}

/// FZ2.7-P12 C2: better_horiz hop only if Δhoriz≥2 under unfinished storm.
inline bool ShouldAllowBetterHorizWitnessRetarget(int unfinished, int cand_horiz,
                                                  int pin_horiz)
{
  if (pin_horiz <= 0 || cand_horiz <= 0)
  {
    return false;
  }
  if (unfinished > 30)
  {
    return cand_horiz + 1 < pin_horiz;
  }
  return cand_horiz < pin_horiz;
}

/// FZ2.7-P12 C5: damp land-frontier hops while cruise unfinished is high.
inline bool ShouldDampWitnessRetargetOnUnfinishedCruise(bool moving,
                                                        int unfinished,
                                                        int thresh = 40)
{
  return moving && unfinished > thresh;
}

/// FZ2.6-P0b: stalled ticket completion prefers mesh_drain over schedule.
inline bool ShouldPrioritizeMeshDrainForTicketedConsume(
    bool consume_mode, int mark_relit_schedule_n,
    int visible_black_stalled_n, int vb_thresh = 40)
{
  return consume_mode && mark_relit_schedule_n > 0 &&
         visible_black_stalled_n > 0;
}

/// FZ2.6-P0b: schedule boost only when drain path not sufficient.
inline bool ShouldPrioritizeMeshScheduleForTicketedConsume(
    bool consume_mode, int visible_black_focus_n,
    int visible_black_stalled_n, int mark_relit_schedule_n,
    int vb_thresh = 40)
{
  if (ShouldPrioritizeMeshDrainForTicketedConsume(
          consume_mode, mark_relit_schedule_n, visible_black_stalled_n,
          vb_thresh))
  {
    return false;
  }
  return consume_mode && visible_black_focus_n > vb_thresh &&
         visible_black_stalled_n > 0;
}

/// FZ2.6-Perf0 / FZ2.7-A: classify binding. Cap-stop wins over wall≈slice.
inline ApplyBinding ClassifyApplyBinding(int applied_n, int ready_at_start,
                                         bool stopped_by_time,
                                         bool stopped_by_cap, double unit_ms,
                                         double slice_ms, int earned_cap = 0)
{
  if (applied_n <= 0 && ready_at_start <= 0)
  {
    return ApplyBinding::QueueEmpty;
  }
  if ((earned_cap >= 2 && applied_n >= earned_cap) || stopped_by_cap)
  {
    return ApplyBinding::CountCap;
  }
  if (applied_n == 1 && unit_ms > slice_ms * 0.9)
  {
    return ApplyBinding::FatUnit;
  }
  if (stopped_by_time)
  {
    return ApplyBinding::TimeSlice;
  }
  return ApplyBinding::CountCap;
}

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

/// FZ2.7: TrimFar LitDrawable=4 dropped in-view ocean (154945 fifo_drop 467).
inline int RelightFifoTrimProtectHoriz()
{
  return kVisualStageProtectHoriz;
}

inline bool ShouldProtectRelightFifoTrimVictim(
    int victim_cx, int victim_cz, bool pin_valid, int pin_cx, int pin_cz,
    bool focus_valid, int focus_cx, int focus_cz,
    int protect_horiz = RelightFifoTrimProtectHoriz())
{
  if (ShouldProtectRelightFifoPinKey(victim_cx, victim_cz, pin_valid, pin_cx,
                                     pin_cz))
  {
    return true;
  }
  if (!focus_valid)
  {
    return false;
  }
  const int dx = victim_cx < focus_cx ? focus_cx - victim_cx : victim_cx - focus_cx;
  const int dz = victim_cz < focus_cz ? focus_cz - victim_cz : victim_cz - focus_cz;
  const int dist = dx > dz ? dx : dz;
  return dist <= protect_horiz;
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

/// FZ2.5-Perf1: ticketed VB debt — all focus black faces have repair tickets.
inline bool ShouldConsumeTicketedVbDebt(int vb_no_ticket_n,
                                        int visible_black_focus_n,
                                        int visible_black_stalled_n,
                                        int vb_thresh = 40)
{
  (void)visible_black_stalled_n;
  return vb_no_ticket_n <= 0 && visible_black_focus_n > vb_thresh;
}

/// FZ2.7-B1d: cruise/defer primary_only uses slim install (no orphan/seam).
inline bool ShouldUsePrimarySlimInstallPath(bool primary_only, bool enter_gate,
                                            bool enter_quiesce)
{
  return primary_only && !enter_gate && !enter_quiesce;
}

/// FZ2.7-B3: enter primary_only also uses slim install (no orphan seamed).
inline bool ShouldUseEnterSlimInstallPath(bool enter_gate, bool enter_quiesce,
                                          bool primary_only)
{
  return enter_gate && !enter_quiesce && primary_only;
}

/// FZ2.7-B1d: orphan ground MarkTerrain is expensive — skip on slim paths.
inline bool ShouldSkipMarkRelitOrphanGround(bool primary_only,
                                            bool consume_mode)
{
  return primary_only || consume_mode;
}

/// FZ2.7-B1e: FOV lit-debt scan only while enter gate is active.
inline bool ShouldCountEnterFovLitDebtForMarkRelit(bool enter_gate)
{
  return enter_gate;
}

/// FZ2.7-B1f: primary_only / slim — do not materialize neighbor bands.
inline bool ShouldFilterMarkRelitBandsToPrimary(bool primary_only_or_slim)
{
  return primary_only_or_slim;
}

/// FZ2.7-A: EMA on cap_unit so one fat frame does not flip throughput_mode.
inline double RelightSmoothCapUnitMs(double prev_ema, double sample,
                                     double alpha = 0.3)
{
  if (sample <= 0.1)
  {
    return prev_ema;
  }
  if (prev_ema <= 0.1)
  {
    return sample;
  }
  return alpha * sample + (1.0 - alpha) * prev_ema;
}

/// FZ2.7-B5: per-column cost for earned-cap math (light+install when both known).
inline double RelightApplyCapUnitMs(double last_unit_apply_ms,
                                    double last_light_unit_ms,
                                    double last_install_unit_ms)
{
  if (last_light_unit_ms > 0.1 && last_install_unit_ms > 0.1)
  {
    return last_light_unit_ms + last_install_unit_ms;
  }
  if (last_light_unit_ms > 0.1)
  {
    return last_light_unit_ms;
  }
  return last_unit_apply_ms;
}

/// GPU-sky noop unit is ~0.07ms — still cheap. 0.0 is unknown (no prior sample).
inline bool RelightApplyUnitIsCheap(double cap_unit_ms, double slice_ms = 16.0)
{
  return cap_unit_ms > 0.0 && cap_unit_ms <= slice_ms / 3.0;
}

inline bool RelightApplyUnitIsExpensive(double cap_unit_ms,
                                        double slice_ms = 16.0)
{
  return cap_unit_ms > slice_ms / 3.0;
}

/// FZ2.7-B2c: cruise slice wide enough for ≥2 cheap applies per drain.
/// FZ2.7-B2d: widen to cap_unit×3 (≤16ms moving) so min_cap=3 fits in slice.
inline double RelightThroughputSliceMs(double miss_reserved_ms, bool consume_mode,
                                       bool moving, bool throughput_mode,
                                       double cap_unit_ms,
                                       int ready_at_start = 0)
{
  double slice_ms = RelightConsumeSliceMs(miss_reserved_ms, consume_mode, moving);
  const bool cheap_cruise = RelightApplyUnitIsCheap(cap_unit_ms);
  // FZ2.7-P2: cheap completed queue — widen consume moving 8ms so Drain can
  // take min(ready,4). Still hard-capped at 16ms moving.
  if (throughput_mode && consume_mode && cheap_cruise && ready_at_start >= 2)
  {
    const double cruise_max = moving ? 16.0 : 20.0;
    slice_ms = std::min(cruise_max, std::max(slice_ms, 16.0));
    return slice_ms;
  }
  if (!throughput_mode || consume_mode)
  {
    return slice_ms;
  }
  slice_ms = std::max(slice_ms, 12.0);
  if (cap_unit_ms > 0.1)
  {
    slice_ms = std::max(slice_ms, cap_unit_ms * 3.0);
    const double cruise_max = moving ? 16.0 : 20.0;
    slice_ms = std::min(slice_ms, cruise_max);
  }
  return slice_ms;
}

/// FZ2.7-B2d: completed queue / fifo / PL depth — consumer should batch harder.
inline bool RelightThroughputHasBacklog(int ready_at_start, int fifo_n,
                                        int fifo_soft_cap, int pending_light_n)
{
  return ready_at_start >= 2 ||
         (fifo_soft_cap > 0 && fifo_n >= fifo_soft_cap / 2) ||
         pending_light_n > 30 ||
         (ready_at_start <= 1 && fifo_n >= 24);
}

/// FZ2.7-B2d: min applies before stop (apply_util≥0.15 ⇒ med≥3 when cheap+backlog).
inline int RelightThroughputMinApplyCap(
    bool throughput_mode, double cap_unit_ms, double slice_ms,
    int visible_black_stalled_n, int ready_at_start, int fifo_n,
    int fifo_soft_cap, int pending_light_n)
{
  if (!throughput_mode)
  {
    return 1;
  }
  if (visible_black_stalled_n > 0)
  {
    return 3;
  }
  const bool cheap = RelightApplyUnitIsCheap(cap_unit_ms, slice_ms);
  if (cheap && RelightThroughputHasBacklog(ready_at_start, fifo_n, fifo_soft_cap,
                                           pending_light_n))
  {
    return 3;
  }
  return 2;
}

/// FZ2.7-B2b: throughput_mode = consume OR primary_only defer.
/// FZ2.7-B2d: backlog uses shared RelightThroughputHasBacklog helper.
inline bool ShouldUseThroughputApplyCap(
    bool consume_mode, bool defer_side, bool enter_pass, double slice_ms,
    double last_unit_apply_ms, double last_light_unit_ms,
    double last_install_unit_ms, int ready_at_start, int fifo_n,
    int fifo_soft_cap, int pending_light_focus_n, int pending_light_n)
{
  if (enter_pass)
  {
    return false;
  }
  if (consume_mode || defer_side)
  {
    return true;
  }
  const double cap_unit =
      RelightApplyCapUnitMs(last_unit_apply_ms, last_light_unit_ms,
                            last_install_unit_ms);
  if (cap_unit > 0.1 && cap_unit <= slice_ms * 0.5)
  {
    return true;
  }
  if (ready_at_start >= 2)
  {
    return true;
  }
  if (RelightThroughputHasBacklog(ready_at_start, fifo_n, fifo_soft_cap,
                                  pending_light_n))
  {
    return true;
  }
  if (pending_light_focus_n > 24)
  {
    return true;
  }
  return false;
}

/// FZ2.7-B2b: throughput_mode = consume OR primary_only defer.
/// FZ2.7-B2d: min_cap=3 when cheap unit + consumer backlog.
inline int EarnedRelightApplyCap(int drain_budget, double slice_ms,
                                 double /*elapsed_ms*/,
                                 double last_unit_apply_ms, bool throughput_mode,
                                 int visible_black_stalled_n = 0,
                                 double last_light_unit_ms = 0.0,
                                 double last_install_unit_ms = 0.0,
                                 int ready_at_start = 0, int fifo_n = 0,
                                 int fifo_soft_cap = 0, int pending_light_n = 0)
{
  if (!throughput_mode)
  {
    return drain_budget;
  }
  const double cap_unit = RelightApplyCapUnitMs(
      last_unit_apply_ms, last_light_unit_ms, last_install_unit_ms);
  const int min_cap = RelightThroughputMinApplyCap(
      throughput_mode, cap_unit, slice_ms, visible_black_stalled_n,
      ready_at_start, fifo_n, fifo_soft_cap, pending_light_n);
  const int time_cap =
      (cap_unit > 0.1)
          ? std::max(1, static_cast<int>(slice_ms / cap_unit))
          : min_cap;
  // FZ2.7-A: time_cap wins — do not force min_cap when the slice cannot fit it.
  int earned = (time_cap < min_cap) ? time_cap : std::max(min_cap, time_cap);
  // FZ2.7-P2: cheap light+install + ready queue → Drain min(ready,4) without
  // restoring min_cap=3 over a fat time_cap.
  const bool cheap_16 = cap_unit > 0.1 && cap_unit <= 16.0 / 3.0;
  if (cheap_16 && ready_at_start >= 2)
  {
    const int time_cap_16 =
        std::max(1, static_cast<int>(16.0 / cap_unit));
    const int ready_n = ready_at_start < 4 ? ready_at_start : 4;
    const int boost = time_cap_16 < ready_n ? time_cap_16 : ready_n;
    if (boost > earned)
    {
      earned = boost;
    }
  }
  return std::min(drain_budget, earned);
}

/// RateMatch R0: stop Apply slice after ≥1 column when wall ≥ MissReservedMs.
/// FZ2.5-Perf1 consume: stop at earned cap OR slice_ms (not 1@8ms when cheap).
/// Enter FOV lit pass is exempt (full Enter budget).
/// FZ2.7-B2b: throughput_mode covers primary_only defer as well as consume.
inline bool ShouldStopRelightApplySlice(double elapsed_ms, int applied_n,
                                        double slice_ms, bool enter_pass,
                                        bool throughput_mode = false,
                                        int earned_cap = 0,
                                        double last_unit_apply_ms = 0.0,
                                        int visible_black_stalled_n = 0,
                                        double last_light_unit_ms = 0.0,
                                        double last_install_unit_ms = 0.0,
                                        int ready_at_start = 0, int fifo_n = 0,
                                        int fifo_soft_cap = 0,
                                        int pending_light_n = 0)
{
  if (enter_pass || applied_n < 1)
  {
    return false;
  }
  const double cap_unit = RelightApplyCapUnitMs(
      last_unit_apply_ms, last_light_unit_ms, last_install_unit_ms);
  if (throughput_mode)
  {
    const int min_cap = RelightThroughputMinApplyCap(
        throughput_mode, cap_unit, slice_ms, visible_black_stalled_n,
        ready_at_start, fifo_n, fifo_soft_cap, pending_light_n);
    const int cap =
        earned_cap > 0
            ? earned_cap
            : EarnedRelightApplyCap(
                  applied_n + 1, slice_ms, elapsed_ms, last_unit_apply_ms, true,
                  visible_black_stalled_n, last_light_unit_ms,
                  last_install_unit_ms, ready_at_start, fifo_n, fifo_soft_cap,
                  pending_light_n);
    if (applied_n >= cap)
    {
      return true;
    }
    if (cap_unit > 0.1)
    {
      const int time_cap =
          std::max(1, static_cast<int>(slice_ms / cap_unit));
      if (time_cap >= 2 && applied_n >= time_cap)
      {
        return true;
      }
      (void)min_cap;
      return false;
    }
    return elapsed_ms >= slice_ms;
  }
  return elapsed_ms >= slice_ms;
}

// Legacy name kept for FZ2.5-Perf2 mesh_schedule (superseded by FZ2.6 split).
inline bool ShouldPrioritizeMeshScheduleForTicketedConsumeLegacy(
    bool consume_mode, int visible_black_focus_n,
    int visible_black_stalled_n, int vb_thresh = 40)
{
  return consume_mode && visible_black_focus_n > vb_thresh &&
         visible_black_stalled_n > 0;
}

/// FZ2.7-P1: GPU-sky / packed noop must still MarkRelit when repair is owed.
inline bool ShouldForceMarkRelitOnUnchangedLight(
    bool consume_mode, int visible_black_focus_n, bool has_repair_ticket,
    bool fully_dark, int horiz, int vb_thresh = 40)
{
  if (consume_mode)
  {
    return true;
  }
  if (visible_black_focus_n > vb_thresh)
  {
    return true;
  }
  if (has_repair_ticket)
  {
    return true;
  }
  if (fully_dark && horiz >= 0 && horiz <= kVisualStageLitDrawableHoriz)
  {
    return true;
  }
  return false;
}

/// FZ2.5-P0b: stalled ticket on lit ring — force MarkRelit schedule path.
inline bool ShouldForceMarkRelitForTicketedStale(
    bool consume_mode, bool has_repair_ticket, bool fully_dark,
    bool still_stale, int horiz,
    int ring = kVisualStageLitDrawableHoriz)
{
  return consume_mode && has_repair_ticket && fully_dark && still_stale &&
         horiz >= 0 && horiz <= ring;
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

/// FZ2.7-C: Capture pipeline depth from earned apply, not apply_n_prev+1.
/// Manual 141417: consume earned(budget=2) stayed 2 → completed med 0, fifo 55.
inline int RelightCapturePipelineDepthCap(
    int apply_n_prev, int max_inflight, double slice_ms, double last_unit_ms,
    double last_light_unit_ms, double last_install_unit_ms, int ready_n,
    int fifo_n, int fifo_soft_cap, int pending_light_n, bool consume_mode)
{
  const int base = std::max(1, apply_n_prev) + 1;
  const double cap_unit = RelightApplyCapUnitMs(
      last_unit_ms, last_light_unit_ms, last_install_unit_ms);
  const bool cheap = RelightApplyUnitIsCheap(cap_unit, slice_ms);
  const bool backlog = RelightThroughputHasBacklog(
      ready_n, fifo_n, fifo_soft_cap, pending_light_n);
  int cap = base;
  if (cheap && (consume_mode || backlog))
  {
    const int earned = EarnedRelightApplyCap(
        /*drain_budget=*/8, slice_ms, 0.0, last_unit_ms, true, 0,
        last_light_unit_ms, last_install_unit_ms, ready_n, fifo_n,
        fifo_soft_cap, pending_light_n);
    cap = std::max(base, std::min(6, earned));
  }
  // FZ2.7-P7: fifo piled, ready empty — refill even if prior unit unknown (0).
  if (!RelightApplyUnitIsExpensive(cap_unit, slice_ms) && fifo_n >= 50 &&
      ready_n <= 1)
  {
    cap = std::max(cap, 4);
  }
  if (max_inflight > 0)
  {
    cap = std::min(max_inflight, cap);
  }
  return std::max(1, cap);
}

/// FZ2.7: fifo starve after Apply must not clamp Capture to 1 (154945 completed=0).
/// FZ2.7-P10: completed==0 means no consumer progress — inflight alone must not
/// block the refill floor (workers busy ≠ Completed fed).
inline int RelightCaptureBgFloorForFifoStarve(int bg_cap, int fifo_n,
                                              int fifo_soft_cap, int completed_n,
                                              int inflight_n, double cap_unit_ms)
{
  const bool fifo_starve =
      fifo_n >= 50 || (fifo_soft_cap > 0 && fifo_n >= fifo_soft_cap / 2);
  if (!fifo_starve)
  {
    return bg_cap;
  }
  if (RelightApplyUnitIsExpensive(cap_unit_ms))
  {
    return bg_cap;
  }
  constexpr int kFloorN = 3;
  if (completed_n <= 0)
  {
    return bg_cap > kFloorN ? bg_cap : kFloorN;
  }
  if (completed_n + inflight_n >= 4)
  {
    return bg_cap;
  }
  return bg_cap > kFloorN ? bg_cap : kFloorN;
}

/// FZ2.7-P10: depth-full SoftDefer floor — keep refill when Completed empty.
inline int SoftDeferCaptureFloorWhenDepthFull(bool soft_defer_or_miss_hole,
                                              int bg_cap,
                                              int completed_n = -1,
                                              bool fifo_starve = false)
{
  if (fifo_starve && completed_n <= 0)
  {
    return bg_cap < 3 ? 3 : bg_cap;
  }
  if (!soft_defer_or_miss_hole)
  {
    return bg_cap;
  }
  return bg_cap < 1 ? 1 : bg_cap;
}

/// FZ2.7-P11: fifo starve + Completed empty — hot SoftDefer must not clamp to 1.
inline bool RelightFifoIsConsumerStarved(int fifo_n, int fifo_soft_cap,
                                         int completed_n)
{
  if (completed_n > 0)
  {
    return false;
  }
  return fifo_n >= 50 ||
         (fifo_soft_cap > 0 && fifo_n >= fifo_soft_cap / 2);
}

inline bool ShouldBypassCaptureHotSoftDeferClamp(int fifo_n, int fifo_soft_cap,
                                                 int completed_n)
{
  return RelightFifoIsConsumerStarved(fifo_n, fifo_soft_cap, completed_n);
}

/// P11: when consumer starved and fifo at cap, trim outside LitDrawable only.
inline int RelightFifoEffectiveTrimProtectHoriz(int fifo_n, int soft_cap,
                                                int completed_n)
{
  if (completed_n <= 0 && soft_cap > 0 && fifo_n >= soft_cap)
  {
    return kVisualStageLitDrawableHoriz;
  }
  return RelightFifoTrimProtectHoriz();
}

/// FZ2.7-P10: sim kill clamps boosts, not Completed-empty refill floor.
inline int ClampCaptureBgAfterSimKill(int bg_cap, bool sim_hot, int completed_n,
                                      int fifo_n)
{
  if (!sim_hot)
  {
    return bg_cap;
  }
  const bool refill =
      completed_n <= 0 &&
      (fifo_n >= 50);
  if (refill)
  {
    return bg_cap < 1 ? 1 : (bg_cap > 3 ? 3 : bg_cap);
  }
  return bg_cap < 1 ? bg_cap : 1;
}

/// Keep live GPU draw even if FullyDark face flag is set. CheapRemesh C5:
/// repair ticket optional — already-published LitDrawable slot stays opaque
/// (anti blink hide↔show). Horiz still gated to LitDrawable ring.
inline bool ShouldKeepLiveGpuOpaqueDespiteFullyDark(
    bool has_live_gpu_draw, int horiz, bool has_repair_progress,
    int keep_horiz = RelightFifoTrimProtectHoriz())
{
  (void)has_repair_progress;
  return has_live_gpu_draw && horiz >= 0 && horiz <= keep_horiz;
}

/// FZ2.7-P4: hide-until-lit must not toggle a live GPU slot in LitDrawable.
inline bool ShouldHideFullyDarkOverLiveGpu(
    bool has_live_gpu_draw, int horiz, bool fully_dark)
{
  if (!fully_dark)
  {
    return false;
  }
  if (ShouldKeepLiveGpuOpaqueDespiteFullyDark(has_live_gpu_draw, horiz, false))
  {
    return false;
  }
  return true;
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
  // FZ2.7-A: do not raise cruise budget to 3 when unit > slice/3 (16ms moving).
  constexpr double kCruiseSliceMs = 16.0;
  if (RelightApplyUnitIsCheap(unit_ms, kCruiseSliceMs))
  {
    cap = std::max(cap, 3);
  }
  return requested < cap ? requested : cap;
}

/// FZ2.7-P2: cruise budget may be 1 from fat FullRelight; Drain still takes
/// min(ready,4) when light+install cap_unit is cheap vs 16ms.
inline int ClampCruiseDrainToReadyCheap(int cruise_budget, int ready_n,
                                        double cap_unit_ms,
                                        double cruise_slice_ms = 16.0)
{
  if (cruise_budget < 0)
  {
    cruise_budget = 0;
  }
  if (ready_n < 2)
  {
    return cruise_budget;
  }
  if (RelightApplyUnitIsExpensive(cap_unit_ms, cruise_slice_ms))
  {
    return cruise_budget;
  }
  const int want = ready_n < 4 ? ready_n : 4;
  return cruise_budget > want ? cruise_budget : want;
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

/// ColdPL-1A: under high PL, finalize focus-ring Capture (no partial Y-band).
inline bool ShouldFinalizeRelightUnderPlPressure(int pending_light_focus_n,
                                                 int horiz_dist,
                                                 int focus_radius,
                                                 int pl_thresh = 24)
{
  return pending_light_focus_n > pl_thresh && horiz_dist >= 0 &&
         horiz_dist <= focus_radius;
}

/// FlickerZero V2: finalize Capture on lit ring when VB orphans backlog.
inline bool ShouldFinalizeRelightUnderVbPressure(
    int visible_black_no_ticket_n, int horiz_dist,
    int ring = kVisualStageLitDrawableHoriz, int no_ticket_thresh = 8)
{
  return visible_black_no_ticket_n > no_ticket_thresh && horiz_dist >= 0 &&
         horiz_dist <= ring;
}

/// FlickerZero V4: do not defer lit-ring FullyDark remesh (clears VB debt).
inline bool ShouldSkipDeferRemeshForLitRingFullyDark(
    int horiz, bool fully_dark, int ring = kVisualStageLitDrawableHoriz)
{
  return fully_dark && horiz >= 0 && horiz <= ring;
}

/// FZ2-R1: skip defer only under active VB heal pressure (enter or no_ticket).
inline bool ShouldSkipDeferRemeshUnderVbHealPressure(
    int horiz, bool fully_dark, bool enter_fov_lit, int vb_no_ticket_n,
    int ring = kVisualStageLitDrawableHoriz, int no_ticket_thresh = 8,
    int visible_black_focus_n = 0, int vb_focus_stable_frames = 0)
{
  if (!fully_dark || horiz < 0 || horiz > ring)
  {
    return false;
  }
  // FZ2.2-C3b/O3 / FZ2.3-C3a / FZ2.4-P0c: steady VB debt — force schedule.
  if (!enter_fov_lit && visible_black_focus_n > 30 &&
      vb_focus_stable_frames >= 2)
  {
    return true;
  }
  return enter_fov_lit || vb_no_ticket_n > no_ticket_thresh;
}

/// FZ2.4-P0a: tickets cleared but focus still dark+pending — stop feeding Note.
inline bool ShouldSuppressPendingLightNote(
    int vb_no_ticket_n, int pending_light_focus_n, int visible_black_focus_n,
    int pl_thresh = 15, int vb_thresh = 40)
{
  return vb_no_ticket_n <= 0 && pending_light_focus_n >= pl_thresh &&
         visible_black_focus_n > vb_thresh;
}

/// FZ2-R1: PL leave-in RemoveAt carve-out — only under VB heal pressure.
inline bool ShouldRemoveAtRemeshDespitePlPressure(
    int horiz, bool fully_dark, bool enter_fov_lit, int vb_no_ticket_n,
    int ring = kVisualStageLitDrawableHoriz, int no_ticket_thresh = 8,
    int visible_black_focus_n = 0, int vb_focus_stable_frames = 0)
{
  return ShouldSkipDeferRemeshUnderVbHealPressure(
      horiz, fully_dark, enter_fov_lit, vb_no_ticket_n, ring, no_ticket_thresh,
      visible_black_focus_n, vb_focus_stable_frames);
}

/// FZ2-R4 / FZ2.1-B4: finalize Capture on lit ring when VB steady debt.
inline bool ShouldFinalizeRelightUnderVbSteadyPressure(
    int visible_black_focus_n, int pending_light_focus_n, int horiz_dist,
    int vb_thresh = 25, int pl_thresh = 10,
    int ring = kVisualStageLitDrawableHoriz)
{
  return visible_black_focus_n > vb_thresh &&
         pending_light_focus_n > pl_thresh && horiz_dist >= 0 &&
         horiz_dist <= ring;
}

/// ColdPL-1B: remesh ownership leave-in when PL backlog is high.
inline bool ShouldLeaveInDirtyUnderPlPressure(int pending_light_focus_n,
                                              int pl_thresh = 30)
{
  return pending_light_focus_n > pl_thresh;
}

/// ColdPL-1B: do not defer MarkRelit side-effects while PL is elevated.
inline bool ShouldSkipDeferHeavyApplyUnderPl(int pending_light_focus_n,
                                             int pl_thresh = 30)
{
  return pending_light_focus_n > pl_thresh;
}

/// Hold nh≤2 witness until MarkRelit or greedy appears — pending OR missing.
/// 215411: miss was often cy=2 without pending on that key, so hold never armed.
/// FZ2.7-P12 C4: SoftDefer empty stuck extends hold to LitDrawable (nh≤4).
inline bool ShouldHoldPinnedRelightWitness(int pinned_horiz,
                                          bool pinned_still_pending,
                                          bool pinned_still_missing = false,
                                          bool empty_stuck = false)
{
  const int max_h =
      empty_stuck ? kVisualStageLitDrawableHoriz : kVisualStageNearFovHoriz;
  if (pinned_horiz < 0 || pinned_horiz > max_h)
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

/// P11: second aggressive trim while Completed empty drops needed fifo work.
inline bool ShouldCruiseRedFifoSecondTrim(int stream_pressure, int fifo_n,
                                          int soft_cap, float fifo_frac,
                                          bool holes_or_miss,
                                          int pending_light_focus,
                                          int completed_n)
{
  if (completed_n <= 0)
  {
    return false;
  }
  return ShouldCruiseRedFifoLightDrain(stream_pressure, fifo_n, soft_cap,
                                       fifo_frac, holes_or_miss,
                                       pending_light_focus);
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

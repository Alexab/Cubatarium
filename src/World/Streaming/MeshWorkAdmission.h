#pragma once

#include "Core/FrameDeadline.h"
#include "World/Streaming/ColumnTicketMap.h"
#include "World/Streaming/RelightFifoPolicy.h"
#include "World/Streaming/StreamIngressPolicy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cutum
{

/// Single authority for how much mesh work may *start* this frame given GPU
/// apply backlog and light debt. Floors may propose schedule/drain; Finalize*
/// is the only hard cap. SoftDeferHeld / Admit / neighbor Dirty read the same
/// quotas (no scattered if pending>=12).
struct MeshWorkAdmissionInput
{
  size_t pending_gpu{0};
  size_t pending_gpu_queued{0};
  size_t pending_gpu_kicked{0};
  bool visual_holes{false};
  /// R3.3: rim miss with incremental hole signals (mh≥3).
  bool rim_hole_pressure{false};
  bool missing_underfeet{false};
  bool moving{false};
  int pending_light_near{0};
  int unfinished_visual{0};
  /// Previous frame mode for HoleDrain hysteresis (SoT 100351 Normal thrash).
  uint8_t prev_mode{0};
  /// GPU readback ring depth (kReadbackRing); used for enqueue_gpu_budget.
  int ring_depth{8};
  /// Nearest focus miss Chebyshev horiz; <0 = unknown (skip K3 remesh band).
  int nearest_miss_horiz{-1};
  /// Nearest focus miss chunk Y; <0 = unknown. Era17: cy≤1 ⇒ FirstMesh class.
  /// Era20: cy≤3 OR mh≤4 (manual 214034 miss_cy=3 mh=4).
  int nearest_miss_cy{-1};
  /// Era47 P2: EnterLitGate — never Normal; force HoleDrain/WarmBacklog.
  bool enter_lit_gate{false};
  /// Closeout C: RemeshQ depth — keep remesh_schedule≥1 when nonempty.
  int remesh_queue_n{0};
  /// FZ2.7-P12 A2: Dirty FirstMeshQ depth (prior tick).
  int dirty_fm_n{0};
  /// FP-D2: prior tick mesh_dirty_schedule_ok_n for carve-out trigger.
  int mesh_schedule_ok_n{0};
  /// FZ2.7-P12 A2: no-mesh hole count (phase1: alias unfinished_visual).
  int no_mesh_n{0};
  /// FP-G1/G2: loaded columns without drawable mesh (focus ring).
  int column_loaded_no_mesh_n{0};
  /// FZ2.7-P13 R1: repairable dark faces (mesh dark, field lit).
  int dark_face_stale_near_n{0};
  /// Q2b: FullyDark pending-repair census (VB) — Remesh floor under HoleDrain.
  int visible_black_fully_dark_repair_n{0};
  /// SRBR-P1: ticketed VB stand remesh protect (no stale required).
  int visible_black_focus_n{0};
  int visible_black_no_ticket_n{0};
  int visible_black_stalled_n{0};
  /// I11-C1: miss witness age for HoleDrain exit guard.
  int miss_witness_age_frames{0};
  /// Miss Ownership SLA P2: SoftDeferEmptyOwnedN for HoleDrain exit-guard.
  int softdefer_empty_owned_n{0};
  /// MissOwn VB regress: PostLoadRingNotReady for coverage sticky (not bare miss).
  int post_load_ring_not_ready{0};
  /// Phase 5.3.3: EmptyBacklogN for HoleDrain FirstMesh drip clamp.
  int empty_backlog_n{0};
  /// Dual-lane: round-robin token when schedule_cap==1 (0=FM, 1=Remesh).
  int dual_lane_rr_token{0};
};

/// Near-focus miss that blocks view / needs urgent HoleDrain (horiz≤2 or underfeet).
/// Rim miss (horiz≥3) must not latch fog, carve stream, or DeepBacklog forever
/// (manual 152933: nh=4–5 standing → wall med 265 / fog_debt 97%).
inline bool IsNearFocusMissUrgent(bool visual_holes, bool missing_underfeet,
                                  int nearest_miss_horiz)
{
  if (missing_underfeet)
  {
    return true;
  }
  if (!visual_holes)
  {
    return false;
  }
  // FZ2.7-P17: mid-rim mh<=4 (was <=2) — sticky miss on cruise 100413/221516.
  return nearest_miss_horiz >= 0 && nearest_miss_horiz <= 4;
}

/// Era20 I-M1: FirstMesh priority class while FOV holes (manual 214034).
inline bool IsMissFirstMeshClass(bool holes, int nearest_miss_cy,
                                 int nearest_miss_horiz)
{
  if (!holes)
  {
    return false;
  }
  if (nearest_miss_cy >= 0 && nearest_miss_cy <= 3)
  {
    return true;
  }
  if (nearest_miss_horiz >= 0 && nearest_miss_horiz <= 4)
  {
    return true;
  }
  return false;
}

struct MeshWorkAdmission
{
  enum class Mode : uint8_t
  {
    Normal = 0,
    WarmBacklog = 1,
    DeepBacklog = 2,
    HoleDrain = 3,
  };

  int max_schedule{0}; // hard cap; ignored when mode==Normal (Finalize passthrough)
  int max_drain{12};
  int gpu_apply_max{4};
  double gpu_budget_frac{0.5};
  int dirty_admit_budget{8};
  int softdefer_requeue{4};
  int admit_batch{4};
  bool allow_neighbor_dirty{true};
  int promote_relight{0};
  int starve_remesh_horiz{2};
  /// Under holes: guaranteed FirstMesh slots (Pass 1); remesh uses remesh_schedule.
  int first_mesh_schedule{0};
  int remesh_schedule{0};
  /// Max new Queued GPU applies this frame (Apply enqueue throttle).
  int enqueue_gpu_budget{4};
  /// FZ2.7-P12 A2: full Remesh→FM steal applied (skip remesh floor backpressure).
  bool steal_remesh_to_fm{false};
  /// FZ2.7-P13 R1: lit-settle remesh floor armed (stale dark faces).
  bool protect_lit_settle_remesh{false};
  /// Dual-lane starve telem: 0=None, 1=Cap1YieldFm, 2=Cap1YieldRemesh, 3=NoDemand.
  int dual_lane_starve_reason{0};
  /// Dual-lane: next round-robin token for Cap1 (persist into next frame input).
  int dual_lane_rr_token_next{0};
  Mode mode{Mode::Normal};
  /// FP-A4: this frame used Warm carve-out from HoleDrain FM starvation.
  bool admission_carve_out{false};
  /// I10-A2: stop VB drain frame counter snapshot (not cumulative).
  int stop_vb_drain_frames_report{0};
  int stop_vb_budget_active{0};
};

/// FP-G2: suppress FM carve-out when hole pressure is high — carved WarmBacklog
/// starves FirstMesh vs sustained HoleDrain (manual 190202 holes_rate=1.0).
inline bool ShouldSuppressFmAdmissionCarveOut(int unfinished_visual,
                                              int column_loaded_no_mesh_n,
                                              int mesh_schedule_ok_n,
                                              int dirty_fm_n = 0,
                                              int first_mesh_floor = 4)
{
  if (dirty_fm_n == 0 && mesh_schedule_ok_n == 0)
  {
    return false;
  }
  if (unfinished_visual >= 4 || column_loaded_no_mesh_n >= 4)
  {
    return true;
  }
  return mesh_schedule_ok_n >= first_mesh_floor;
}

/// R08-lite: single HoleDrain→Warm emergency arm ledger (not dual remain
/// writers). Prefer long FM-starvation arm; else short rim consumer drip.
inline int ArmHoleDrainEmergencyCarveOutFrames(
    int current_remain, bool holes_moving, bool fm_queue_empty,
    bool schedule_starved, bool suppress_carve, bool rim_hole_pressure,
    bool fm_consumer_starved, bool moving, int nearest_miss_horiz)
{
  if (holes_moving && fm_queue_empty && schedule_starved && !suppress_carve &&
      !rim_hole_pressure)
  {
    return 90;
  }
  if (fm_consumer_starved && moving && nearest_miss_horiz >= 2 &&
      nearest_miss_horiz <= 3 && current_remain < 4)
  {
    return 4;
  }
  return current_remain;
}

/// Apply the one Warm carve-out under HoleDrain (remain / reenter / stop-VB).
inline bool TryApplyHoleDrainWarmCarveOut(bool mode_is_hole_drain,
                                          int &admission_carve_remain,
                                          int &hole_drain_reenter_cd,
                                          bool stop_vb_block_carve,
                                          bool rim_hole_pressure,
                                          bool holes_moving,
                                          bool missing_underfeet,
                                          bool stop_vb_exit_armed)
{
  if (!mode_is_hole_drain || stop_vb_block_carve)
  {
    return false;
  }
  if (admission_carve_remain > 0 && !rim_hole_pressure)
  {
    --admission_carve_remain;
    hole_drain_reenter_cd = 20;
    return true;
  }
  if (hole_drain_reenter_cd > 0)
  {
    --hole_drain_reenter_cd;
    if (holes_moving && !missing_underfeet && !rim_hole_pressure)
    {
      return true;
    }
  }
  return stop_vb_exit_armed;
}

/// FM consumer starved: dirty queue has work but schedule under floor.
inline bool IsFmConsumerStarved(int dirty_fm_n, int mesh_schedule_ok_n,
                                int first_mesh_floor = 4)
{
  if (dirty_fm_n <= 0)
  {
    return false;
  }
  return mesh_schedule_ok_n < std::min(first_mesh_floor, dirty_fm_n);
}

/// Phase 5.7R7: remesh Dirty walk scan cap (max(48, max_schedule*8)).
inline int DirtyRemeshScanCap(int max_schedule_per_frame)
{
  return std::max(48, std::max(0, max_schedule_per_frame) * 8);
}

inline size_t MeshWorkQueuedApprox(const MeshWorkAdmissionInput &in)
{
  if (in.pending_gpu_queued > 0 || in.pending_gpu_kicked > 0)
  {
    return in.pending_gpu_queued;
  }
  if (in.pending_gpu_kicked >= in.pending_gpu)
  {
    return 0;
  }
  return in.pending_gpu - in.pending_gpu_kicked;
}

/// FZ2.7-P12 A2: unfinished storm + FM starved vs no_mesh → full Remesh steal.
/// Miss Ownership SLA P3: coverage sticky steals without unfinished>30 gate.
/// protect_vb_or_lit reserved for callers that want to skip steal entirely.
inline bool ShouldStealRemeshToFirstMesh(bool holes, int unfinished, int dirty_fm,
                                         int no_mesh,
                                         bool coverage_sticky = false,
                                         bool protect_vb_or_lit = false)
{
  if (protect_vb_or_lit)
  {
    return false;
  }
  if (!holes || no_mesh <= 0)
  {
    return false;
  }
  if (coverage_sticky && dirty_fm * 2 < no_mesh)
  {
    return true;
  }
  if (unfinished <= 30)
  {
    return false;
  }
  return dirty_fm * 2 < no_mesh;
}

/// MissOwn VB P1: keep ticketed VB remesh protect even in consume_mode when
/// stalled census is high (lane fairness; not N04 Dirty).
inline bool ShouldKeepRemeshProtectUnderVbStall(bool consume_mode,
                                                int visible_black_stalled_n,
                                                int stalled_thresh = 40)
{
  if (!consume_mode)
  {
    return true;
  }
  return visible_black_stalled_n >= stalled_thresh;
}

/// FZ2.7-P13 R1 / Q2b: drawable FullyDark or stale faces need Remesh floor even
/// under FM steal (201330: repair debt high while remesh_cap starved).
/// SoT 111310: FullyDark plug debt arms protect even when SoftDefer holes=0
/// (stop-tail remesh_cap=0 with dirty_remesh≫0).
inline bool ShouldProtectLitSettleRemesh(bool holes, int dark_face_stale_near,
                                         int remesh_queue_n,
                                         int stale_thresh = 200,
                                         int fully_dark_repair_n = 0,
                                         int fully_dark_thresh = 8)
{
  if (remesh_queue_n <= 0)
  {
    return false;
  }
  if (fully_dark_repair_n >= fully_dark_thresh)
  {
    return true;
  }
  if (!holes)
  {
    return false;
  }
  return dark_face_stale_near > stale_thresh;
}

/// G1-P1 / A11: under holes + RemeshQ + StaleVertexLight/FullyDark debt, reserve
/// snapshot budget for remesh schedules before FirstMesh walk burns the slice.
inline bool ShouldReserveRemeshSnapshotSlice(bool holes, int remesh_q_n,
                                             int stale_or_fully_dark_debt,
                                             int debt_thresh = 20)
{
  return holes && remesh_q_n > 0 && stale_or_fully_dark_debt >= debt_thresh;
}

/// FullyDark FM fairness P0: when FM queue is starved, remesh snapshot must not
/// consume the whole MeshSnapshotBudget (leave residual for Pass1 FM).
/// Does NOT disable remesh slice; does NOT change admission remesh caps / N04.
inline bool ShouldStopRemeshSnapshotForFmResidual(
    bool fm_consumer_starved, int dirty_fm_n, int remesh_scheduled,
    int remesh_cap, double remesh_slice_ms, double snapshot_budget_ms,
    double remesh_budget_frac = 0.65)
{
  if (!fm_consumer_starved || dirty_fm_n <= 0 || snapshot_budget_ms <= 0.0)
  {
    return false;
  }
  if (remesh_scheduled <= 0)
  {
    return false; // allow at least one remesh attempt when cap>0
  }
  if (remesh_cap > 0 && remesh_scheduled >= remesh_cap)
  {
    return true;
  }
  return remesh_slice_ms >= snapshot_budget_ms * remesh_budget_frac;
}

/// FullyDark FM fairness P1: under protect remesh floor, never publish
/// remesh-only schedule while FM consumer is starved. Same admission owner.
inline bool ShouldYieldRemeshSlotToFmUnderProtect(bool protect_lit_settle,
                                                  int dirty_fm_n,
                                                  int prior_schedule_ok_n,
                                                  int remesh_schedule,
                                                  int first_mesh_schedule)
{
  if (!protect_lit_settle || dirty_fm_n <= 0)
  {
    return false;
  }
  if (!IsFmConsumerStarved(dirty_fm_n, prior_schedule_ok_n))
  {
    return false;
  }
  if (first_mesh_schedule <= 0 && remesh_schedule >= 2)
  {
    return true;
  }
  if (first_mesh_schedule < 1 && remesh_schedule >= 1)
  {
    return true;
  }
  return false;
}

/// Dual-lane schedule split (A10/A11): FirstMesh + RemeshLit lane quotas from one
/// admission decision. Order is fixed (remesh snapshot before FM); focus miss must
/// never flip order. Quotas ≠ Capture/Apply/MarkRelit floors.
enum class DualLaneStarveReason : uint8_t
{
  None = 0,
  Cap1YieldFm = 1,
  Cap1YieldRemesh = 2,
  NoDemand = 3,
};

struct DualLaneScheduleInput
{
  int schedule_cap{0};
  int fm_q{0};
  int remesh_q{0};
  bool focus_missing_or_holes{false};
  bool remesh_lit_demand{false};
  bool fm_demand{false};
  int protect_remesh_floor{0};
  bool steal_remesh_to_fm{false};
  int prior_first_mesh_schedule{0};
  int prior_remesh_schedule{0};
  int rr_token{0}; // 0=FM, 1=Remesh
  bool miss_pressure{false}; // prefer remainder to FM when both lanes hot
};

struct DualLaneSchedule
{
  int first_mesh_schedule{0};
  int remesh_schedule{0};
  bool remesh_snapshot_before_fm{true};
  DualLaneStarveReason starve_reason{DualLaneStarveReason::None};
  int next_rr_token{0};
};

inline DualLaneSchedule
ComputeDualLaneSchedule(const DualLaneScheduleInput &in)
{
  DualLaneSchedule out;
  out.remesh_snapshot_before_fm = true;
  out.next_rr_token = in.rr_token;

  bool fm_d = in.fm_demand;
  bool rm_d = in.remesh_lit_demand;
  if (in.protect_remesh_floor > 0 && in.remesh_q > 0)
  {
    rm_d = true;
  }
  if (in.steal_remesh_to_fm && in.protect_remesh_floor == 0)
  {
    rm_d = false;
  }

  const int cap = std::max(0, in.schedule_cap);
  if (cap == 0)
  {
    out.starve_reason = DualLaneStarveReason::NoDemand;
    return out;
  }

  if (fm_d && rm_d && cap >= 2)
  {
    int fm = std::max(1, in.prior_first_mesh_schedule);
    int rm = std::max({1, in.protect_remesh_floor, in.prior_remesh_schedule});
    if (fm + rm > cap)
    {
      // Never drop below 1/1; shrink the larger lane first.
      while (fm + rm > cap && (fm > 1 || rm > 1))
      {
        if (fm > rm && fm > 1)
        {
          --fm;
        }
        else if (rm > 1)
        {
          --rm;
        }
        else if (fm > 1)
        {
          --fm;
        }
        else
        {
          break;
        }
      }
      if (fm + rm > cap)
      {
        // cap==2 with 1+1 is the only remaining legal split.
        fm = 1;
        rm = 1;
      }
    }
    else
    {
      int rem = cap - fm - rm;
      while (rem > 0)
      {
        if (in.miss_pressure || in.focus_missing_or_holes)
        {
          ++fm;
        }
        else
        {
          ++rm;
        }
        --rem;
      }
    }
    out.first_mesh_schedule = fm;
    out.remesh_schedule = rm;
    out.starve_reason = DualLaneStarveReason::None;
    return out;
  }

  if (fm_d && rm_d && cap == 1)
  {
    if (in.rr_token == 0)
    {
      out.first_mesh_schedule = 1;
      out.remesh_schedule = 0;
      out.starve_reason = DualLaneStarveReason::Cap1YieldRemesh;
      out.next_rr_token = 1;
    }
    else
    {
      out.first_mesh_schedule = 0;
      // Cap is hard: protect_remesh_floor must not expand schedule beyond cap.
      out.remesh_schedule =
          std::min(cap, std::max(1, in.protect_remesh_floor));
      out.starve_reason = DualLaneStarveReason::Cap1YieldFm;
      out.next_rr_token = 0;
    }
    return out;
  }

  if (fm_d && !rm_d)
  {
    out.first_mesh_schedule = cap;
    out.remesh_schedule = 0;
    out.starve_reason = DualLaneStarveReason::None;
    return out;
  }
  if (rm_d && !fm_d)
  {
    out.first_mesh_schedule = 0;
    // Cap is hard: protect floor cannot expand remesh beyond schedule_cap.
    out.remesh_schedule = std::min(cap, std::max(cap, in.protect_remesh_floor));
    out.starve_reason = DualLaneStarveReason::None;
    return out;
  }
  out.starve_reason = DualLaneStarveReason::NoDemand;
  return out;
}

/// G1-P3b: after a debt-forced kick under focus miss, run a same-tick Finish
/// pass even when HoleDrain backlog <12 (proxy: pending_kicked=1–2).
inline bool ShouldRunSecondGpuFinishPass(bool hole_finish_bias,
                                         bool force_kick_debt,
                                         int kicked_this_tick,
                                         bool focus_missing_or_holes)
{
  if (hole_finish_bias)
  {
    return true;
  }
  return force_kick_debt && kicked_this_tick > 0 && focus_missing_or_holes;
}

/// G1-P2: Queued GPU work under focus/stale debt must not sit kick-starved.
inline bool ShouldForceGpuKickUnderQueuedDebt(int pending_queued_n,
                                             bool focus_missing_or_holes,
                                             int stale_or_fully_dark_debt = 0,
                                             int debt_thresh = 20)
{
  if (pending_queued_n < 1)
  {
    return false;
  }
  if (focus_missing_or_holes)
  {
    return true;
  }
  return stale_or_fully_dark_debt >= debt_thresh;
}

/// G1-N2: after DrainAsyncMeshResults, remesh work that just reached Queued
/// must get a same-frame kick under focus miss or stale/FullyDark debt.
inline bool ShouldForceGpuKickPostDrain(int pending_queued_n,
                                        bool focus_missing_mesh,
                                        int stale_vertex_light_n,
                                        int fully_dark_debt_n,
                                        int remesh_schedule_ok_n,
                                        int debt_thresh = 20)
{
  if (pending_queued_n < 1)
  {
    return false;
  }
  if (focus_missing_mesh)
  {
    return true;
  }
  const int debt = stale_vertex_light_n + fully_dark_debt_n;
  if (debt >= debt_thresh)
  {
    return true;
  }
  return remesh_schedule_ok_n > 0 && debt >= 1;
}

/// FZ2.7-P17: on long stand with sticky VB + stale plateau, keep remesh
/// protect (floor) but signal callers to prefer column-owned heal over
/// full-ring Dirty thrash. True when idle long enough that eye-black is stuck.
inline bool ShouldPreferStandVbHealOwnership(bool moving,
                                             int visible_black_focus_n,
                                             int stand_age_frames,
                                             int dark_face_stale_near)
{
  if (moving || visible_black_focus_n < 40 || dark_face_stale_near < 200)
  {
    return false;
  }
  return stand_age_frames >= 180;
}

/// I15-B1: do not exit HoleDrain while ticketed VB debt remains.
inline bool ShouldExitStopVbHoleDrain(int stop_vb_drain_frames,
                                      int vb_no_ticket_n, int vb_focus_n,
                                      bool consume_mode)
{
  constexpr int kVbClearFloor = 5;
  if (vb_focus_n <= kVbClearFloor && vb_no_ticket_n <= 0)
  {
    return true;
  }
  if (consume_mode || vb_focus_n >= 20)
  {
    (void)stop_vb_drain_frames;
    return false;
  }
  return stop_vb_drain_frames > 1800 && vb_focus_n <= kVbClearFloor;
}

/// I15-B4: stand VB plateau — keep HoleDrain even when holes telemetry cleared.
inline bool ShouldHoldHoleDrainForStopVbPlateau(bool moving, int vb_focus_n,
                                                int vb_no_ticket_n)
{
  if (moving || vb_focus_n < 15)
  {
    return false;
  }
  return vb_no_ticket_n > 0 || vb_focus_n >= 20;
}

/// MissOwn VB regress P0: HoleDrain sticky only on real coverage debt
/// (clnm / SoftDeferEmpty / PostLoadRingNotReady) — not bare focus_missing.
inline bool ShouldHoldHoleDrainForCoverageSticky(int column_loaded_no_mesh_n,
                                                 int softdefer_empty_owned_n,
                                                 int post_load_ring_not_ready = 0)
{
  return column_loaded_no_mesh_n > 0 || softdefer_empty_owned_n > 0 ||
         post_load_ring_not_ready > 0;
}

/// SRBR-P1: remesh floor under ticketed VB stand (no stale required; P17 cured).
inline bool ShouldProtectRemeshUnderTicketedVbStand(bool moving,
                                                   int visible_black_focus_n,
                                                   int vb_no_ticket_n,
                                                   int remesh_queue_n,
                                                   int vb_thresh = 50,
                                                   int no_ticket_soft = 4)
{
  if (moving || remesh_queue_n <= 0)
  {
    return false;
  }
  return visible_black_focus_n >= vb_thresh && vb_no_ticket_n >= 0 &&
         vb_no_ticket_n <= no_ticket_soft;
}

/// FP3: cruise ticketed VB — remesh floor while moving (081522 VB med 73).
inline bool ShouldProtectRemeshUnderTicketedVbCruise(bool moving,
                                                      int visible_black_focus_n,
                                                      int vb_no_ticket_n,
                                                      int remesh_queue_n,
                                                      int vb_thresh = 40,
                                                      int no_ticket_soft = 8)
{
  if (!moving || remesh_queue_n <= 0)
  {
    return false;
  }
  return visible_black_focus_n >= vb_thresh && vb_no_ticket_n >= 0 &&
         vb_no_ticket_n <= no_ticket_soft;
}

/// A25 R4: cap DirtyAdmit growth under FD repair when drops already thrashing.
inline int CapDirtyAdmitUnderThrash(int dirty_admit_budget, int fd_repair_n,
                                    int dirty_dropped_recent = 0,
                                    int dropped_soft_cap = 800)
{
  int budget = dirty_admit_budget;
  if (fd_repair_n >= 20)
  {
    budget = std::max(budget, 4);
  }
  if (dirty_dropped_recent > dropped_soft_cap)
  {
    budget = std::min(budget, 4);
  }
  return budget;
}

/// A27 S5: clamp admit when unified snapshot/GPU/retirement pools are saturated.
inline void ApplyUnifiedAdmissionPools(MeshWorkAdmission &out,
                                       const MeshWorkAdmissionInput &in,
                                       int snapshot_used = 0,
                                       int retirement_used = 0)
{
  UnifiedAdmissionPools pools{};
  const int queued =
      static_cast<int>(in.pending_gpu_queued + in.pending_gpu);
  const int gpu = static_cast<int>(in.pending_gpu_kicked);
  if (!CanAdmitUnifiedWork(pools, snapshot_used, queued, gpu, retirement_used))
  {
    out.dirty_admit_budget = std::min(out.dirty_admit_budget, 1);
    out.max_schedule = std::min(out.max_schedule, 1);
  }
}

inline void MeshWorkFillModeDefaults(MeshWorkAdmission &out,
                                     MeshWorkAdmission::Mode mode,
                                     const MeshWorkAdmissionInput &in,
                                     size_t queued, bool holes, bool light_debt)
{
  out.mode = mode;
  const int ring = std::max(1, in.ring_depth);
  switch (mode)
  {
  case MeshWorkAdmission::Mode::DeepBacklog:
    out.max_schedule = 2;
    out.max_drain = 16;
    out.gpu_apply_max = std::max(24, ring * 2);
    out.gpu_budget_frac = 0.85;
    out.dirty_admit_budget = 1;
    out.softdefer_requeue = holes ? 1 : 0;
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = holes ? 1 : 2;
    out.promote_relight = light_debt ? 4 : (holes ? 2 : 0);
    // LitDrawable drain: PL/FD repair debt → more PromoteRelight under backlog.
    if (in.pending_light_near >= 16 ||
        in.visible_black_fully_dark_repair_n >= 20)
    {
      out.promote_relight = std::max(out.promote_relight, 6);
      // A21 residual R2: keep DirtyAdmit headroom while FD repair census high.
      // A25 R4: CapDirtyAdmitUnderThrash when drops already exceed soft gate.
      out.dirty_admit_budget = CapDirtyAdmitUnderThrash(
          std::max(out.dirty_admit_budget, 4),
          in.visible_black_fully_dark_repair_n);
    }
    // G2/H: moving holes FirstMesh headroom (was 2; G2→3; H→4 for rim miss_horiz).
    out.first_mesh_schedule = holes ? 4 : 1;
    out.remesh_schedule = holes ? 0 : 1;
    break;
  case MeshWorkAdmission::Mode::HoleDrain:
    out.max_schedule = in.pending_gpu >= 16 ? 2 : 4;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(16, ring * 2);
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget =
        std::max(0, 4 - static_cast<int>(std::min<size_t>(queued, 4)));
    out.softdefer_requeue = out.dirty_admit_budget > 0 ? 1 : 0;
    // G3: Held→Dirty headroom under miss (not empty FirstMesh DirtyAdmit).
    if (holes)
    {
      out.softdefer_requeue = std::max(out.softdefer_requeue, 2);
    }
    out.admit_batch = 1;
    out.allow_neighbor_dirty = false;
    out.starve_remesh_horiz = 1;
    out.promote_relight = light_debt ? 4 : 2;
    // LitDrawable drain: raise PromoteRelight under PL≥16 / FD repair census.
    if (in.pending_light_near >= 16 ||
        in.visible_black_fully_dark_repair_n >= 20)
    {
      out.promote_relight = std::max(out.promote_relight, 6);
      // A21 residual R2: keep DirtyAdmit headroom while FD repair census high.
      // A25 R4: CapDirtyAdmitUnderThrash when drops already exceed soft gate.
      out.dirty_admit_budget = CapDirtyAdmitUnderThrash(
          std::max(out.dirty_admit_budget, 4),
          in.visible_black_fully_dark_repair_n);
    }
    // H/Era14: moving HoleDrain first_mesh 4→6 (best ARCH_D3_LAND near-GO p2c).
    out.first_mesh_schedule = 6;
    out.remesh_schedule = 1;
    if (!in.moving)
    {
      out.max_schedule = std::max(out.max_schedule, 6);
      out.admit_batch = 2;
      out.dirty_admit_budget = std::max(out.dirty_admit_budget, 2);
      out.softdefer_requeue = std::max(out.softdefer_requeue, 1);
      out.first_mesh_schedule = std::max(out.first_mesh_schedule, 6);
      out.remesh_schedule = std::max(out.remesh_schedule, 1);
    }
    else if (holes)
    {
      // FP2: moving HoleDrain must schedule FM despite pending_gpu DeepBacklog cap.
      out.first_mesh_schedule = std::max(out.first_mesh_schedule, 4);
      out.max_schedule = std::max(out.max_schedule, 4);
    }
    else if (in.moving)
    {
      const int rim_fm =
          RimIngressFmScheduleFloor(true, in.nearest_miss_horiz, in.dirty_fm_n);
      if (rim_fm > 0)
      {
        out.first_mesh_schedule = std::max(out.first_mesh_schedule, rim_fm);
        out.max_schedule = std::max(out.max_schedule, rim_fm);
      }
    }
    // I11-C2: rim miss — guarantee remesh, don't starve FM below floor.
    if (holes && in.nearest_miss_horiz >= 0 && in.nearest_miss_horiz <= 4)
    {
      out.remesh_schedule = std::max(out.remesh_schedule, 1);
      const int fm_floor = std::min(4, std::max(1, in.dirty_fm_n));
      if (out.first_mesh_schedule < fm_floor)
      {
        out.first_mesh_schedule = fm_floor;
      }
    }
    break;
  case MeshWorkAdmission::Mode::WarmBacklog:
    out.max_schedule = 6;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(16, ring * 2);
    out.gpu_budget_frac = 0.75;
    out.dirty_admit_budget = 4;
    out.softdefer_requeue = 2;
    out.admit_batch = 2;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = 2;
    out.promote_relight = in.pending_light_near >= 16 ? 4 : 0;
    out.first_mesh_schedule = holes ? 2 : 3;
    out.remesh_schedule = 3;
    break;
  case MeshWorkAdmission::Mode::Normal:
  default:
    out.max_schedule = 0;
    out.max_drain = 12;
    out.gpu_apply_max = std::max(4, ring);
    out.gpu_budget_frac = holes ? 0.6 : 0.5;
    out.dirty_admit_budget = 8;
    out.softdefer_requeue = 4;
    out.admit_batch = holes ? (in.moving ? 3 : 4) : 4;
    out.allow_neighbor_dirty = true;
    out.starve_remesh_horiz = holes ? 2 : 3;
    out.promote_relight = 0;
    out.first_mesh_schedule = 0; // unused when Normal (full schedule)
    out.remesh_schedule = 0;
    break;
  }
  const int kicked = static_cast<int>(std::min<size_t>(in.pending_gpu_kicked, 64));
  out.enqueue_gpu_budget =
      std::max(0, ring - kicked + (mode == MeshWorkAdmission::Mode::Normal ? 2 : 0));
  if (mode == MeshWorkAdmission::Mode::HoleDrain ||
      mode == MeshWorkAdmission::Mode::DeepBacklog)
  {
    // Prefer Finish: do not refill Queued beyond one ring of headroom.
    out.enqueue_gpu_budget = std::max(0, ring - kicked);
  }
  if (out.first_mesh_schedule > 0 && out.max_schedule > 0)
  {
    out.max_schedule =
        std::max(out.max_schedule, out.first_mesh_schedule + out.remesh_schedule);
  }
  ApplyUnifiedAdmissionPools(out, in);
}

inline MeshWorkAdmission::Mode
MeshWorkPickRawMode(const MeshWorkAdmissionInput &in, bool holes)
{
  const bool near_miss_urgent =
      IsNearFocusMissUrgent(in.visual_holes, in.missing_underfeet,
                            in.nearest_miss_horiz);
  if (in.pending_gpu >= 24)
  {
    // Near witness must not sit in DeepBacklog max_schedule=2 (163559 sch≈5).
    if (near_miss_urgent && holes)
    {
      return MeshWorkAdmission::Mode::HoleDrain;
    }
    return MeshWorkAdmission::Mode::DeepBacklog;
  }
  if (in.pending_gpu >= 12 && holes)
  {
    return MeshWorkAdmission::Mode::HoleDrain;
  }
  if (in.pending_gpu >= 12)
  {
    return MeshWorkAdmission::Mode::WarmBacklog;
  }
  return MeshWorkAdmission::Mode::Normal;
}

inline MeshWorkAdmission
ComputeMeshWorkAdmission(const MeshWorkAdmissionInput &in)
{
  MeshWorkAdmission out;
  // Rim-only miss (nh≥5): UV debt alone must not trap HoleDrain; visual_holes still
  // drives admission (K3 remesh band). Crisis carve/fog use IsNearFocusMissUrgent.
  const bool rim_only_miss =
      in.visual_holes && !in.missing_underfeet &&
      in.nearest_miss_horiz >= 5;
  const bool holes =
      in.visual_holes || in.missing_underfeet || in.rim_hole_pressure ||
      (in.unfinished_visual >= 8 && !rim_only_miss);
  const size_t queued = MeshWorkQueuedApprox(in);
  const bool light_debt =
      holes && (in.pending_light_near >= 16 || in.unfinished_visual >= 8);

  MeshWorkAdmission::Mode mode = MeshWorkPickRawMode(in, holes);
  const int ring = std::max(1, in.ring_depth);
  const size_t queued_exit_cap =
      static_cast<size_t>(std::max(2, ring / 2));
  // J0: never Normal under holes/UV — even with cooled pending/queued FOV floor
  // sch=12 refill thrash (manual 170330 mid i=2: pend=1,mode=0,miss=1,uv=9).
  // Without holes: Warm when Queued refill risk (pending≥8 + queued>half-ring).
  if (mode == MeshWorkAdmission::Mode::Normal)
  {
    if (holes)
    {
      mode = MeshWorkAdmission::Mode::HoleDrain;
    }
    else if (queued > queued_exit_cap && in.pending_gpu >= 8)
    {
      mode = MeshWorkAdmission::Mode::WarmBacklog;
    }
  }
  // Era47 P2: enter gate must not sit in Normal (drain_cap=4 / schedule=1
  // throttles stall gpu_pending plateau while MarkRelit still feeds).
  if (in.enter_lit_gate && mode == MeshWorkAdmission::Mode::Normal)
  {
    mode = (holes || in.pending_gpu >= 8)
               ? MeshWorkAdmission::Mode::HoleDrain
               : MeshWorkAdmission::Mode::WarmBacklog;
  }
  const auto prev = static_cast<MeshWorkAdmission::Mode>(in.prev_mode);
  const bool was_hole_backlog =
      prev == MeshWorkAdmission::Mode::HoleDrain ||
      prev == MeshWorkAdmission::Mode::DeepBacklog;
  // FP-D2: cruise FM starvation carve-out — WarmBacklog when FM queue empty.
  static int admission_carve_remain = 0;
  static int hole_drain_reenter_cd = 0;
  const bool holes_moving = holes && in.moving;
  const bool fm_starved = in.dirty_fm_n == 0;
  const bool fm_consumer_starved =
      IsFmConsumerStarved(in.dirty_fm_n, in.mesh_schedule_ok_n);
  const bool schedule_starved = in.mesh_schedule_ok_n == 0;
  const bool suppress_carve = ShouldSuppressFmAdmissionCarveOut(
      in.unfinished_visual, in.column_loaded_no_mesh_n,
      in.mesh_schedule_ok_n, in.dirty_fm_n);
  // R08-lite: one emergency arm ledger (FM starve OR rim consumer drip).
  admission_carve_remain = ArmHoleDrainEmergencyCarveOutFrames(
      admission_carve_remain, holes_moving, fm_starved, schedule_starved,
      suppress_carve, in.rim_hole_pressure, fm_consumer_starved, in.moving,
      in.nearest_miss_horiz);
  // HoleDrain exit: FM queue fed and schedule at floor — leave HoleDrain.
  static int hole_drain_fm_fed_frames = 0;
  if (in.dirty_fm_n > 0 &&
      in.mesh_schedule_ok_n >= std::min(4, in.dirty_fm_n))
  {
    ++hole_drain_fm_fed_frames;
  }
  else
  {
    hole_drain_fm_fed_frames = 0;
  }
  const bool stop_vb_hold = ShouldHoldHoleDrainForStopVbPlateau(
      in.moving, in.visible_black_focus_n, in.visible_black_no_ticket_n);
  const bool coverage_sticky = ShouldHoldHoleDrainForCoverageSticky(
      in.column_loaded_no_mesh_n, in.softdefer_empty_owned_n,
      in.post_load_ring_not_ready);
  if (hole_drain_fm_fed_frames >= 8 &&
      mode == MeshWorkAdmission::Mode::HoleDrain && !holes &&
      in.unfinished_visual == 0 && !stop_vb_hold && !coverage_sticky)
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    hole_drain_fm_fed_frames = 0;
  }
  // I12-C2: HoleDrain with empty FM queue — soften to WarmBacklog.
  static int hole_drain_empty_fm_frames = 0;
  static int hole_drain_schedule_starved_frames = 0;
  if (mode == MeshWorkAdmission::Mode::HoleDrain && in.dirty_fm_n == 0)
  {
    ++hole_drain_empty_fm_frames;
  }
  else
  {
    hole_drain_empty_fm_frames = 0;
  }
  if (mode == MeshWorkAdmission::Mode::HoleDrain && in.dirty_fm_n == 0 &&
      in.mesh_schedule_ok_n == 0)
  {
    ++hole_drain_schedule_starved_frames;
  }
  else
  {
    hole_drain_schedule_starved_frames = 0;
  }
  if (hole_drain_empty_fm_frames >= 4 &&
      mode == MeshWorkAdmission::Mode::HoleDrain &&
      (hole_drain_schedule_starved_frames >= 8 ||
       !(in.nearest_miss_horiz <= 2 && in.missing_underfeet)) &&
      !stop_vb_hold && !coverage_sticky)
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    hole_drain_empty_fm_frames = 0;
  }
  // I10-E1: rim-only HoleDrain exit when FM fed but rim holes persist.
  static int hole_drain_rim_fed_frames = 0;
  if (in.dirty_fm_n > 0 &&
      in.mesh_schedule_ok_n >= std::min(4, in.dirty_fm_n) &&
      in.nearest_miss_horiz > 2)
  {
    ++hole_drain_rim_fed_frames;
  }
  else
  {
    hole_drain_rim_fed_frames = 0;
  }
  if (hole_drain_rim_fed_frames >= 8 &&
      mode == MeshWorkAdmission::Mode::HoleDrain && !stop_vb_hold &&
      !coverage_sticky &&
      (!in.rim_hole_pressure || in.unfinished_visual <= 0))
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    hole_drain_rim_fed_frames = 0;
  }
  // R3.7: exit HoleDrain when pressure latched without unfinished progress.
  static int hole_drain_pressure_stale_frames = 0;
  if (mode == MeshWorkAdmission::Mode::HoleDrain && in.rim_hole_pressure &&
      in.unfinished_visual <= 0 && in.column_loaded_no_mesh_n <= 0)
  {
    ++hole_drain_pressure_stale_frames;
  }
  else
  {
    hole_drain_pressure_stale_frames = 0;
  }
  if (hole_drain_pressure_stale_frames >= 12 &&
      mode == MeshWorkAdmission::Mode::HoleDrain && !stop_vb_hold &&
      !coverage_sticky)
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    hole_drain_pressure_stale_frames = 0;
  }
  // I11-C1: exit HoleDrain when column_loaded_no_mesh drains (clnm falling).
  static int prev_column_loaded_no_mesh = 0;
  static int hole_drain_clnm_drain_frames = 0;
  static int prev_miss_witness_age = 0;
  static int miss_witness_age_rising = 0;
  if (in.dirty_fm_n > 0 &&
      in.mesh_schedule_ok_n >= std::min(4, in.dirty_fm_n) &&
      in.column_loaded_no_mesh_n < prev_column_loaded_no_mesh)
  {
    ++hole_drain_clnm_drain_frames;
  }
  else if (in.column_loaded_no_mesh_n >= prev_column_loaded_no_mesh)
  {
    hole_drain_clnm_drain_frames = 0;
  }
  prev_column_loaded_no_mesh = in.column_loaded_no_mesh_n;
  if (in.miss_witness_age_frames > prev_miss_witness_age)
  {
    ++miss_witness_age_rising;
  }
  else
  {
    miss_witness_age_rising = 0;
  }
  prev_miss_witness_age = in.miss_witness_age_frames;
  if (hole_drain_clnm_drain_frames >= 6 &&
      mode == MeshWorkAdmission::Mode::HoleDrain &&
      in.nearest_miss_horiz > 2 && miss_witness_age_rising < 4 &&
      !stop_vb_hold && !coverage_sticky)
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    hole_drain_clnm_drain_frames = 0;
  }
  const bool consume_mode = IsTicketedVbConsumeMode(
      in.visible_black_no_ticket_n, in.visible_black_focus_n,
      /*vb_stalled_n=*/in.visible_black_stalled_n, in.moving,
      in.pending_light_near);
  const bool stop_vb_block_carve =
      !consume_mode && !in.moving && in.visible_black_focus_n > 20 &&
      in.visible_black_no_ticket_n > 0;
  // FP-E2: stop-phase VB drain — after 30s stand with VB orphans, exit HoleDrain.
  static int stop_vb_drain_frames = 0;
  if (!in.moving &&
      (in.visible_black_no_ticket_n > 0 || in.visible_black_focus_n >= 20))
  {
    ++stop_vb_drain_frames;
  }
  else
  {
    stop_vb_drain_frames = 0;
  }
  const bool stop_vb_exit_armed =
      !in.moving && stop_vb_drain_frames > 1800 &&
      in.visible_black_focus_n > 0 &&
      mode == MeshWorkAdmission::Mode::HoleDrain &&
      ShouldExitStopVbHoleDrain(stop_vb_drain_frames,
                                in.visible_black_no_ticket_n,
                                in.visible_black_focus_n, consume_mode);
  if (TryApplyHoleDrainWarmCarveOut(
          mode == MeshWorkAdmission::Mode::HoleDrain, admission_carve_remain,
          hole_drain_reenter_cd, stop_vb_block_carve, in.rim_hole_pressure,
          holes_moving, in.missing_underfeet, stop_vb_exit_armed))
  {
    mode = MeshWorkAdmission::Mode::WarmBacklog;
    out.admission_carve_out = true;
  }
  // Exit HoleDrain/Deep only when holes cleared, pending cooled (≤8), and
  // Queued drained below half-ring (avoid Immediate Normal refill).
  if (was_hole_backlog)
  {
    const bool can_exit =
        !holes && in.pending_gpu <= 8 && queued <= queued_exit_cap &&
        !stop_vb_hold;
    const bool near_miss_urgent =
        IsNearFocusMissUrgent(in.visual_holes, in.missing_underfeet,
                              in.nearest_miss_horiz);
    if (!can_exit)
    {
      if (in.pending_gpu >= 24 && !(near_miss_urgent && holes))
      {
        mode = MeshWorkAdmission::Mode::DeepBacklog;
      }
      else if (holes || in.pending_gpu > 8)
      {
        mode = holes ? MeshWorkAdmission::Mode::HoleDrain
                     : MeshWorkAdmission::Mode::WarmBacklog;
      }
      else
      {
        // Pending cooled but Queued still high — stay Warm, not Normal.
        mode = MeshWorkAdmission::Mode::WarmBacklog;
      }
    }
    if (stop_vb_hold && holes == false)
    {
      mode = MeshWorkAdmission::Mode::HoleDrain;
    }
  }

  MeshWorkFillModeDefaults(out, mode, in, queued, holes, light_debt);

  // FP-D2 / FP-G2: carved WarmBacklog FM floor only when holes are mild.
  if (out.admission_carve_out &&
      out.mode == MeshWorkAdmission::Mode::WarmBacklog &&
      in.unfinished_visual <= 2 && in.column_loaded_no_mesh_n < 4)
  {
    out.first_mesh_schedule = std::max(out.first_mesh_schedule, 6);
    if (out.max_schedule > 0)
    {
      out.max_schedule = std::max(out.max_schedule, 6);
    }
  }

  // Phase 3: fixed pools — HoleDrain redistributes remesh → FirstMesh.
  // Warm/Normal keep mode defaults; pool fm floor on Warm blew max_schedule (9 vs 6).
  {
    WorkPoolBudget pools = DefaultCruisePools();
    if (mode == MeshWorkAdmission::Mode::HoleDrain ||
        mode == MeshWorkAdmission::Mode::DeepBacklog)
    {
      pools = HoleDrainPools(pools);
    }
    int fm = 0;
    int remesh = 0;
    int gpu = 0;
    ApplyPoolsToAdmissionCaps(pools, fm, remesh, gpu);
    if (mode == MeshWorkAdmission::Mode::HoleDrain ||
        mode == MeshWorkAdmission::Mode::DeepBacklog)
    {
      out.remesh_schedule = remesh;
      if (out.first_mesh_schedule > 0 && out.max_schedule > 0)
      {
        out.max_schedule = std::max(
            out.max_schedule,
            out.first_mesh_schedule + std::max(0, out.remesh_schedule));
      }
    }
    out.gpu_apply_max = std::max(out.gpu_apply_max, gpu);
    (void)fm;
  }

  // Era17/20: FirstMesh priority class while FOV holes — remesh_schedule=0.
  // Era20: cy≤3 OR mh≤4 (manual 214034 miss_cy=3 mh=4 outside old cy≤1).
  // Era18 P3: unfinished climb under dirty storm — same class when UV>0.
  const bool miss_tops =
      IsMissFirstMeshClass(holes, in.nearest_miss_cy, in.nearest_miss_horiz);
  const bool unfinished_storm =
      holes && in.unfinished_visual > 0 &&
      IsMissFirstMeshClass(true, in.nearest_miss_cy, in.nearest_miss_horiz) &&
      in.pending_gpu >= 12;
  const int no_mesh =
      in.no_mesh_n > 0 ? in.no_mesh_n : in.unfinished_visual;
  const bool coverage_sticky_steal = ShouldHoldHoleDrainForCoverageSticky(
      in.column_loaded_no_mesh_n, in.softdefer_empty_owned_n,
      in.post_load_ring_not_ready);
  const bool steal_fm =
      ShouldStealRemeshToFirstMesh(holes, in.unfinished_visual, in.dirty_fm_n,
                                   no_mesh, coverage_sticky_steal);
  if ((miss_tops || unfinished_storm || steal_fm) &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    // Closeout C: steal remesh→FM only when RemeshQ empty (or far-only later).
    // Keep remesh_schedule≥1 when RemeshQ has work (pool reservation, not FloorMs).
    // FZ2.7-P12 A2: unfinished storm + fm/no_mesh<0.5 → full steal even if RemeshQ.
    const int remesh_was = std::max(0, out.remesh_schedule);
    if (steal_fm || in.remesh_queue_n <= 0)
    {
      out.remesh_schedule = 0;
      out.first_mesh_schedule =
          std::max(out.first_mesh_schedule, 6) + remesh_was;
      out.steal_remesh_to_fm = steal_fm;
    }
    else
    {
      // Deep RemeshQ (manual 100319 late): one remesh slot cannot drain med~60.
      if (in.remesh_queue_n >= 32)
      {
        out.remesh_schedule = std::max(2, std::min(remesh_was, 3));
      }
      else
      {
        out.remesh_schedule = std::max(1, std::min(remesh_was, 2));
      }
      const int steal = std::max(0, remesh_was - out.remesh_schedule);
      out.first_mesh_schedule =
          std::max(out.first_mesh_schedule, 6) + steal;
    }
    out.max_schedule =
        std::max(out.max_schedule, out.first_mesh_schedule + out.remesh_schedule);
  }

  // FZ2.7-P13 R2: keep Remesh floor under stale lit-settle even if A2 stole.
  // FM schedule boost from steal is retained; only remesh is restored.
  // MissOwn VB P1: keep ticketed VB protect even in consume_mode when stalled high.
  const bool protect_lit = ShouldProtectLitSettleRemesh(
      holes, in.dark_face_stale_near_n, in.remesh_queue_n, 200,
      in.visible_black_fully_dark_repair_n, 8);
  const bool keep_vb_protect = ShouldKeepRemeshProtectUnderVbStall(
      consume_mode, in.visible_black_stalled_n);
  const bool protect_ticketed_vb =
      keep_vb_protect &&
      ShouldProtectRemeshUnderTicketedVbStand(
          in.moving, in.visible_black_focus_n, in.visible_black_no_ticket_n,
          in.remesh_queue_n);
  const bool protect_ticketed_vb_cruise =
      keep_vb_protect &&
      ShouldProtectRemeshUnderTicketedVbCruise(
          in.moving, in.visible_black_focus_n, in.visible_black_no_ticket_n,
          in.remesh_queue_n);
  if ((protect_lit || protect_ticketed_vb || protect_ticketed_vb_cruise) &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    out.protect_lit_settle_remesh = true;
    // SoT 111310: FullyDark plug debt — remesh floor scales with repair census
    // (cap=2 left VB plateau with ok_remesh=2 but age climbing).
    const int fd_repair = in.visible_black_fully_dark_repair_n;
    const int fd_floor =
        fd_repair >= 8 ? std::min(8, 2 + fd_repair / 2) : 2;
    out.remesh_schedule = std::max(out.remesh_schedule, fd_floor);
    out.max_schedule =
        std::max(out.max_schedule, out.first_mesh_schedule + out.remesh_schedule);
  }
  // FullyDark FM fairness P1: under protect, keep FM residual when prior ok_fm
  // was starved (same admission owner; remesh floor stays ≥1 if remesh_q>0).
  if (ShouldYieldRemeshSlotToFmUnderProtect(
          out.protect_lit_settle_remesh, in.dirty_fm_n, in.mesh_schedule_ok_n,
          out.remesh_schedule, out.first_mesh_schedule))
  {
    if (in.remesh_queue_n > 0)
    {
      out.remesh_schedule = std::max(1, out.remesh_schedule - 1);
    }
    else
    {
      out.remesh_schedule = 0;
    }
    out.first_mesh_schedule = std::max(out.first_mesh_schedule, 1);
    out.max_schedule =
        std::max(out.max_schedule, out.first_mesh_schedule + out.remesh_schedule);
  }

  // J1/K2/M2: under HoleDrain/Deep miss backlog, give Finish more wall budget
  // (Kick bias is in ChunkMeshCache kick_cut/finish_cap — keep enqueue capped).
  // M2: pending≥12 already gets Finish wall share 0.85 (was 0.82 at 12 / 0.85 at 16).
  if (holes &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    if (in.pending_gpu >= 12)
    {
      out.gpu_budget_frac = std::max(out.gpu_budget_frac, 0.85);
    }
  }

  if (light_debt && out.mode != MeshWorkAdmission::Mode::Normal)
  {
    // Manual 153832: UV≥8 crushed first_mesh; 152933: deep RemeshQ~37 + cap=1
    // starved remesh drain (relight+mesh_emerge wall). Keep FM headroom; drain Q.
    // FZ2.7-P13: stale protect → ceil≥2 even when RemeshQ collapsed (<32).
    if (!out.steal_remesh_to_fm || out.protect_lit_settle_remesh)
    {
      const int remesh_floor =
          out.protect_lit_settle_remesh
              ? 2
              : (in.remesh_queue_n >= 32 ? 2
                                         : (in.remesh_queue_n >= 16 ? 1 : 0));
      const int remesh_ceil =
          out.protect_lit_settle_remesh
              ? (in.remesh_queue_n >= 32 ? 3 : 2)
              : (in.remesh_queue_n >= 32 ? 3 : 1);
      if (miss_tops && in.remesh_queue_n <= 0 && !out.protect_lit_settle_remesh)
      {
        out.remesh_schedule = 0;
      }
      else
      {
        out.remesh_schedule =
            std::min(std::max(out.remesh_schedule, remesh_floor), remesh_ceil);
      }
    }
    out.starve_remesh_horiz = std::max(out.starve_remesh_horiz, 2);
    const int fm = std::max(0, out.first_mesh_schedule);
    const int need = fm + std::max(0, out.remesh_schedule);
    out.max_schedule = std::max(out.max_schedule, need);
    const int max_cap = in.remesh_queue_n >= 32 ? 7 : 5;
    out.max_schedule = std::min(out.max_schedule, std::max(need, max_cap));
  }

  // Near-focus miss must keep HoleDrain schedule (FM6+remesh), not DeepBacklog=2.
  // 163559: pending_gpu≥24 → Deep max=5 while miss_horiz=1 / schedule_ok med=5.
  if (IsNearFocusMissUrgent(in.visual_holes, in.missing_underfeet,
                            in.nearest_miss_horiz) &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    out.first_mesh_schedule = std::max(out.first_mesh_schedule, 6);
    const int need =
        out.first_mesh_schedule + std::max(0, out.remesh_schedule);
    out.max_schedule = std::max(out.max_schedule, std::max(need, 8));
  }

  // K3/M3: rim miss outside FirstMesh class with cooled-ish GPU pending — +1
  // remesh for stale/UV without stealing FirstMesh slots.
  // Era20: FirstMesh class owns cy≤3 OR mh≤4; remesh band is mh 5–6.
  // M3: cooled band pending≤12 (was ≤8; med pending~11 skipped the band).
  // Demand contract (audit N07): +1 only when RemeshQ has real demand — otherwise
  // steal already zeroed remesh and dual-lane must not invent remesh slots.
  if (!miss_tops && holes && in.pending_gpu <= 12 &&
      in.remesh_queue_n > 0 && in.nearest_miss_horiz >= 5 &&
      in.nearest_miss_horiz <= 6 &&
      (out.mode == MeshWorkAdmission::Mode::HoleDrain ||
       out.mode == MeshWorkAdmission::Mode::DeepBacklog))
  {
    out.remesh_schedule = std::max(0, out.remesh_schedule) + 1;
    out.starve_remesh_horiz = std::max(out.starve_remesh_horiz, 2);
    const int fm = std::max(0, out.first_mesh_schedule);
    out.max_schedule =
        std::max(out.max_schedule, fm + std::max(0, out.remesh_schedule));
  }
  // I10-E2: HoleDrain schedule floor when FM queue has work but schedule starved.
  if (out.mode == MeshWorkAdmission::Mode::HoleDrain && in.dirty_fm_n > 0 &&
      in.mesh_schedule_ok_n < 2)
  {
    const int fm_floor = std::min(4, in.dirty_fm_n);
    out.max_schedule = std::max(out.max_schedule, std::max(2, fm_floor));
    if (out.first_mesh_schedule > 0)
    {
      out.first_mesh_schedule = std::max(out.first_mesh_schedule, fm_floor);
    }
  }
  // Phase 5.3.3 FirstMeshDripCap: prefer continuous 3–4 over burst 6 after starve.
  if ((mode == MeshWorkAdmission::Mode::HoleDrain ||
       mode == MeshWorkAdmission::Mode::DeepBacklog) &&
      in.empty_backlog_n > 8)
  {
    out.first_mesh_schedule = std::min(out.first_mesh_schedule, 4);
  }

  // Dual-lane: enforce FM+RemeshLit minima after mode/debt floors; never flip
  // remesh-snapshot-before-FM order (focus miss must not reorder).
  {
    const int stale_fd_debt =
        std::max(in.dark_face_stale_near_n, in.visible_black_fully_dark_repair_n);
    const bool holes_or_miss =
        holes || in.missing_underfeet ||
        IsNearFocusMissUrgent(in.visual_holes, in.missing_underfeet,
                              in.nearest_miss_horiz);
    DualLaneScheduleInput din{};
    din.schedule_cap = std::max(out.max_schedule,
                                out.first_mesh_schedule + out.remesh_schedule);
    din.fm_q = in.dirty_fm_n;
    din.remesh_q = in.remesh_queue_n;
    din.focus_missing_or_holes = holes_or_miss;
    din.fm_demand = in.dirty_fm_n > 0 && holes_or_miss;
    din.remesh_lit_demand =
        in.remesh_queue_n > 0 &&
        (out.protect_lit_settle_remesh || stale_fd_debt >= 20);
    din.protect_remesh_floor = out.protect_lit_settle_remesh ? 2 : 0;
    din.steal_remesh_to_fm = out.steal_remesh_to_fm;
    din.prior_first_mesh_schedule = out.first_mesh_schedule;
    din.prior_remesh_schedule = out.remesh_schedule;
    din.rr_token = in.dual_lane_rr_token;
    din.miss_pressure = holes_or_miss;
    // Only reshape when at least one lane has demand under non-Normal pressure.
    if ((din.fm_demand || din.remesh_lit_demand) &&
        out.mode != MeshWorkAdmission::Mode::Normal)
    {
      const DualLaneSchedule lane = ComputeDualLaneSchedule(din);
      out.first_mesh_schedule = lane.first_mesh_schedule;
      out.remesh_schedule = lane.remesh_schedule;
      out.dual_lane_starve_reason =
          static_cast<int>(lane.starve_reason);
      out.dual_lane_rr_token_next = lane.next_rr_token;
      const int need =
          out.first_mesh_schedule + out.remesh_schedule;
      out.max_schedule = std::max(out.max_schedule, need);
    }
    else
    {
      out.dual_lane_rr_token_next = in.dual_lane_rr_token;
      out.dual_lane_starve_reason =
          static_cast<int>(DualLaneStarveReason::None);
    }
  }

  out.stop_vb_drain_frames_report = stop_vb_drain_frames;
  out.stop_vb_budget_active =
      (!in.moving &&
       (in.visible_black_no_ticket_n > 0 || in.visible_black_focus_n >= 20))
          ? 1
          : 0;
  return out;
}

/// Cruise wall P0: remesh DirtyAdmit backpressure when Pressure/fifo/Dirty thrash.
/// FirstMesh / !Drawable paths do not consume DirtyAdmit (enforced in Cache).
struct RemeshAdmitBackpressureInput
{
  int stream_pressure{0}; // 0 Green / 1 Yellow / 2 Red
  int fifo_n{0};
  int dirty_n{0};
  int relight_fifo_soft_cap{96};
  int dirty_thrash_soft_cap{320};
  float fifo_admit_frac{0.75f};
  int admit_cap_red{0};
  int admit_cap_yellow{1};
  bool miss_active{false};
  int remesh_queue_n{0};
  /// FZ2.7-P13: do not clamp remesh below lit-settle floor.
  bool protect_lit_settle_remesh{false};
};

inline bool ShouldApplyRemeshAdmitBackpressure(
    const RemeshAdmitBackpressureInput &in)
{
  if (in.stream_pressure >= 2)
  {
    return true;
  }
  const int fifo_thresh = static_cast<int>(
      static_cast<float>(std::max(1, in.relight_fifo_soft_cap)) *
      std::max(0.1f, in.fifo_admit_frac));
  if (in.fifo_n >= fifo_thresh)
  {
    return true;
  }
  return in.dirty_n > std::max(0, in.dirty_thrash_soft_cap);
}

inline void ApplyRemeshAdmitBackpressure(MeshWorkAdmission &adm,
                                         const RemeshAdmitBackpressureInput &in)
{
  if (!ShouldApplyRemeshAdmitBackpressure(in))
  {
    return;
  }
  const int dirty_over_soft =
      in.dirty_n - static_cast<int>(static_cast<float>(
                       std::max(0, in.dirty_thrash_soft_cap)) *
                   1.1f);
  const bool hard_dirty_pressure =
      in.stream_pressure >= 2 && dirty_over_soft > 0;
  const int admit_cap =
      in.stream_pressure >= 2 ? in.admit_cap_red : in.admit_cap_yellow;
  adm.dirty_admit_budget =
      std::min(adm.dirty_admit_budget, std::max(0, admit_cap));
  if (hard_dirty_pressure)
  {
    // F4a-v2: keep minimal progress (1 admit) to avoid full relight/mesh stall
    // observed in cruise while still reducing dirty flood on red pressure.
    adm.dirty_admit_budget = std::min(adm.dirty_admit_budget, 1);
  }
  // Keep at least 1 remesh slot under miss: dual-Q already prefers FirstMesh;
  // remesh_schedule=0 starved lit settle and caused flicker / late light updates.
  // FZ2.7-P12 A2: unfinished FM-starve steal keeps remesh at 0.
  // FZ2.7-P13: lit-settle protect restores floor≥2 even under steal.
  if (adm.protect_lit_settle_remesh || in.protect_lit_settle_remesh)
  {
    adm.protect_lit_settle_remesh = true;
    adm.remesh_schedule = std::max(adm.remesh_schedule, 2);
    const int remesh_cap = in.remesh_queue_n >= 32 ? 3 : 2;
    adm.remesh_schedule = std::min(adm.remesh_schedule, remesh_cap);
  }
  else if (!adm.steal_remesh_to_fm)
  {
    const int remesh_cap = in.remesh_queue_n >= 32
                               ? (in.miss_active ? 3 : 2)
                               : (in.miss_active && in.remesh_queue_n >= 16 ? 2
                                                                            : 1);
    adm.remesh_schedule = std::min(adm.remesh_schedule, remesh_cap);
  }
  adm.allow_neighbor_dirty = false;
  if (adm.first_mesh_schedule > 0 && adm.max_schedule > 0)
  {
    adm.max_schedule =
        std::max(adm.max_schedule, adm.first_mesh_schedule + adm.remesh_schedule);
  }
}

inline int FinalizeSchedule(int proposed_schedule, const MeshWorkAdmission &adm)
{
  proposed_schedule = std::max(0, proposed_schedule);
  if (adm.mode == MeshWorkAdmission::Mode::Normal)
  {
    return proposed_schedule;
  }
  return std::min(proposed_schedule, std::max(0, adm.max_schedule));
}

inline int FinalizeDrain(int proposed_drain, const MeshWorkAdmission &adm)
{
  proposed_drain = std::max(0, proposed_drain);
  if (adm.mode == MeshWorkAdmission::Mode::Normal)
  {
    return proposed_drain;
  }
  // Backlog modes: never starve Apply below admission floor.
  return std::max(proposed_drain, adm.max_drain);
}

} // namespace cutum

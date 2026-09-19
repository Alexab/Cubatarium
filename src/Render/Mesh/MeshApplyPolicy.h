#pragma once

#include <atomic>
#include <cstdint>

namespace cutum
{

/// Decision for async/GPU mesh apply vs ActiveMeshSourceRevision + Current.
/// Fixes thrash where an older result erased Active tracking for a newer build
/// (manual_1957: mesh_apply_stale≈392, dirty plateau).
enum class MeshApplyRevDecision : uint8_t
{
  Commit = 0,
  /// Active tracks a different (newer) rev — drop this result, keep Active.
  DiscardOlderKeepActive,
  /// Active matches result but Current moved — remesh without bump.
  RemeshObsoleteTracked,
  /// No Active entry — drop; do not Dirty (CancelOutside / cancelled).
  DropNoActive,
};

inline MeshApplyRevDecision ClassifyMeshApplyRevision(bool has_active,
                                                      uint64_t active_rev,
                                                      uint64_t result_rev,
                                                      uint64_t current_rev)
{
  if (!has_active)
  {
    return MeshApplyRevDecision::DropNoActive;
  }
  if (active_rev != result_rev)
  {
    return MeshApplyRevDecision::DiscardOlderKeepActive;
  }
  if (result_rev != current_rev)
  {
    return MeshApplyRevDecision::RemeshObsoleteTracked;
  }
  return MeshApplyRevDecision::Commit;
}

/// Era15 TD-ARCH-049 MeshResidency: when replacing a live GPU-resident mesh with
/// CPU batches, publish the CPU drawable *before* FreeChunk. Free-first opens a
/// one-frame hole whenever GPU was the sole drawable source (batches cleared on
/// GPU commit). Industry: keep old mesh until replacement is ready.
inline bool ShouldPublishCpuBatchesBeforeFreeGpu()
{
  return true;
}

/// True when FreeChunk-before-write would drop HasDrawable (GPU-only drawable).
inline bool CpuReplaceFreeFirstWouldHole(bool gpu_drawable,
                                         bool new_cpu_batches_drawable)
{
  return gpu_drawable && !new_cpu_batches_drawable;
}

/// Era20 I-M3: skip FreeChunk / keep prior GPU when replacement CPU is empty
/// SoftDefer placeholder (HasGreedy && !Drawable flicker).
inline bool ShouldKeepPriorGpuOnEmptyCpuReplace(bool gpu_drawable,
                                                bool new_cpu_batches_drawable)
{
  return CpuReplaceFreeFirstWouldHole(gpu_drawable, new_cpu_batches_drawable);
}

/// Era21 I-R1 PendingReplace: already-GPU-drawable remesh must not FreeChunk
/// until BindCommittedSlot of the new packed mesh (or explicit unload).
/// Empty SoftDefer keep-prior (I-M3) remains a separate early-out.
/// new_cpu_drawable does not authorize FreeChunk while live SSBO draws.
inline bool ShouldDeferFreeChunkUntilPackedReplace(bool had_gpu_drawable,
                                                   bool /*new_cpu_drawable*/)
{
  return had_gpu_drawable;
}

/// Underfeet lease: never FreeChunk a live GPU drawable on intentional empty
/// CPU replace (one-frame NotLoaded/hole under camera). Hinterland still
/// FreeChunks intentional occluded empty → 0-quad ready.
inline bool ShouldRetainUnderfeetGpuOnEmptyReplace(bool underfeet_lease,
                                                   bool had_gpu_drawable,
                                                   bool intentional_empty)
{
  return underfeet_lease && had_gpu_drawable && intentional_empty;
}

/// P4: keep live GPU in visual/keep ring until BindCommitted replacement.
/// Hinterland (horiz > keep) still evicts. Hide-until-lit covers FullyDark —
/// RemoveChunk dark GPU «ради дыр» collapses packed/pool on cruise (195810).
inline bool ShouldKeepGpuSlotUntilBindInRing(bool had_gpu_drawable, int horiz,
                                            int keep_horiz,
                                            bool has_replacement_bound)
{
  if (!had_gpu_drawable || has_replacement_bound || keep_horiz < 0)
  {
    return false;
  }
  return horiz >= 0 && horiz <= keep_horiz;
}

/// P4: keep packed/slice-ready draw (not just the SSBO) until BindCommitted.
/// Pool can survive a cruise step while GpuPacked refs dump — same keep ring.
inline bool ShouldKeepPackedDrawUntilBind(bool had_live_gpu_draw, int horiz,
                                         int keep_horiz,
                                         bool has_replacement_bound)
{
  return ShouldKeepGpuSlotUntilBindInRing(had_live_gpu_draw, horiz, keep_horiz,
                                          has_replacement_bound);
}

/// LitRing: lit GpuPacked stays until BindCommitted (never free into dark plug).
inline bool ShouldKeepLitPackedUntilBind(bool had_lit_drawable, int horiz,
                                         int keep_horiz,
                                         bool has_replacement_bound)
{
  return ShouldKeepGpuSlotUntilBindInRing(had_lit_drawable, horiz, keep_horiz,
                                          has_replacement_bound);
}

/// Q2b/G1 dual-Q: FirstMeshQ = MissingResident only. Published FullyDark is
/// StaleVertexLight remesh (RemeshQ). Closeout C routed FullyDark→FM and mixed
/// classes under HoleDrain (201330: dirty_fm~129 vs remesh~32, schedule_ok=1).
inline bool ShouldRouteRemeshToFirstMeshQueue(bool has_drawable,
                                             bool fully_dark_drawable)
{
  (void)fully_dark_drawable;
  return !has_drawable;
}

/// A10 RelightReplace Dirty sole-owner for published FullyDark / StaleVertexLight.
/// When ON: secondary enter/RAA/seam Dirty pumps must not MarkDirty FullyDark —
/// MarkRelit → RemeshQ is the sole producer. Rollback: Set(false).
inline std::atomic<bool> &RelightReplaceDirtyOwnerFlag()
{
  static std::atomic<bool> enabled{true};
  return enabled;
}
inline bool IsRelightReplaceDirtyOwnerEnabled()
{
  return RelightReplaceDirtyOwnerFlag().load(std::memory_order_relaxed);
}
inline void SetRelightReplaceDirtyOwnerEnabled(bool on)
{
  RelightReplaceDirtyOwnerFlag().store(on, std::memory_order_relaxed);
}
/// Skip secondary FullyDark Dirty when RelightReplace owner is ON.
inline bool ShouldSkipSecondaryFullyDarkDirty(bool fully_dark_drawable)
{
  return IsRelightReplaceDirtyOwnerEnabled() && fully_dark_drawable;
}

/// N04 autopsy I3t: accepted light/geom-stale must not become sole live image
/// while prior drawable exists — hold prior draw and Dirty-refresh instead.
inline bool ShouldHoldPriorDrawOnAcceptedStale(bool accepted_input_stale,
                                              bool had_prior_drawable)
{
  return accepted_input_stale && had_prior_drawable;
}

/// E1/111235: visual prior for I3t — allocator/HasGpuMesh counts even when
/// GpuQuadCount was spoofed to 0 (intentional-empty keep-until-bind).
inline bool HadVisualPriorForI3tHold(bool has_drawable_greedy,
                                    bool gpu_resident_flag,
                                    bool pipeline_has_gpu_mesh)
{
  if (has_drawable_greedy)
  {
    return true;
  }
  return gpu_resident_flag && pipeline_has_gpu_mesh;
}

/// Era21 I-M6: under FOV miss, SoftDefer Capture is blocked only by a live
/// FirstMesh ticket — Relight/Remesh alone must not starve rim FirstMesh.
inline bool SoftDeferCaptureBlockedByRepairTicket(bool missing_visible_mesh,
                                                  bool has_first_mesh_ticket,
                                                  bool has_any_repair_ticket)
{
  if (missing_visible_mesh)
  {
    return has_first_mesh_ticket;
  }
  return has_any_repair_ticket;
}

/// Era27 I-A4: under SoftDefer/miss undrawn, live Inflight or PendingReplace
/// must not be Forget/superseded into a hole frame (PendingReplace is the
/// residency layer — no second drawable cache).
inline bool ShouldHoldInflightSupersedeUnderMissUndrawn(
    bool soft_or_miss_undrawn, bool has_inflight_or_pending, bool has_drawable)
{
  return soft_or_miss_undrawn && has_inflight_or_pending && !has_drawable;
}

/// FP-A2 / FP-D1 / I8-A1: HoleDrain cruise nh≤4 — do not park undrawn miss in RAA.
/// Bypass only under hole pressure or empty FM queue — never blanket on low
/// schedule_ok (that flooded Dirty without mesh completion, holes_rate 0.72).
/// fm_consumer_starved: dirty_fm>0 but schedule under floor — must bypass RAA.
inline bool ShouldBypassRaAParkForCruiseFirstMesh(bool hole_drain_mode,
                                                    int horiz,
                                                    bool fm_starvation = false,
                                                    bool column_loaded_no_mesh =
                                                        false,
                                                    int schedule_ok_n = 999,
                                                    int first_mesh_floor = 4,
                                                    bool fm_consumer_starved =
                                                        false)
{
  if (horiz < 0 || horiz > 4)
  {
    return false;
  }
  if (hole_drain_mode)
  {
    return true;
  }
  if (column_loaded_no_mesh && schedule_ok_n < first_mesh_floor)
  {
    return true;
  }
  if (fm_starvation && schedule_ok_n < first_mesh_floor)
  {
    return true;
  }
  if (fm_consumer_starved && schedule_ok_n < first_mesh_floor)
  {
    return true;
  }
  return false;
}

/// FP-G1 arch: prior-frame enqueue minus prior-frame schedule drain (not same-frame).
inline int ComputeFmDirtyEnqueueReserve(int enqueue_prior, int schedule_ok_prior)
{
  int reserve = std::max(0, enqueue_prior - schedule_ok_prior);
  // M1-4: empty_fm_queue guard — keep FM enqueue path alive when both zero.
  if (enqueue_prior <= 0 && schedule_ok_prior <= 0)
  {
    reserve = std::max(reserve, 2);
  }
  return reserve;
}

/// Phase 5.6.2: keep-ring FirstMesh reserve floor outside AbortDripN.
/// Survives abort clamp so frontier empties / miss / SoftDefer / latch can enqueue.
inline int ComputeKeepRingFmDirtyEnqueueReserve(int base_reserve,
                                                bool focus_missing,
                                                int empty_backlog_n, bool latch,
                                                int soft_stuck_n,
                                                int keep_ring_empty_n)
{
  int reserve = std::max(0, base_reserve);
  if (focus_missing || empty_backlog_n > 0 || latch || soft_stuck_n > 0 ||
      keep_ring_empty_n > 0)
  {
    reserve = std::max(reserve, 2);
  }
  return reserve;
}

/// Phase 5.6.2: presentable-band empty pressure (r<=work_r), not full RD inflate.
inline int PresentableBandEmptyPressure(int unfinished_visual,
                                        int column_loaded_no_mesh,
                                        int chunk_not_ready, int work_radius,
                                        int stuck_horiz, int miss_horiz)
{
  if (work_radius < 0)
  {
    return 0;
  }
  const bool in_band =
      (miss_horiz >= 0 && miss_horiz <= work_radius) ||
      (stuck_horiz >= 0 && stuck_horiz <= work_radius);
  if (!in_band && unfinished_visual <= 0 && column_loaded_no_mesh <= 0)
  {
    return 0;
  }
  // Prefer near-band signals; still count backlog when miss/stuck in band.
  const int backlog =
      std::max(unfinished_visual,
               std::max(column_loaded_no_mesh, std::max(0, chunk_not_ready)));
  if (in_band)
  {
    return backlog;
  }
  // No miss/stuck pin in band: only count if backlog is small (keep-ring sized).
  constexpr int kKeepRingBacklogCap = 9; // ~(2*r+1)^2/4 for r=4-ish
  return backlog > 0 && backlog <= kKeepRingBacklogCap ? backlog : 0;
}

/// Effective FirstMesh schedule cap after drain-aware reserve.
inline int ComputeFirstMeshScheduleEffectiveCap(int first_mesh_cap_base,
                                                  int fm_q, int reserve_n,
                                                  int first_mesh_floor = 4,
                                                  bool fm_consumer_starved = false,
                                                  int schedule_ok_n = 0)
{
  if (reserve_n <= 0)
  {
    return first_mesh_cap_base;
  }
  int reserved_cap = std::max(first_mesh_floor, fm_q - reserve_n);
  // I10-D3: soften consumer-starved clamp — keep schedule_ok+1 throughput.
  if (fm_consumer_starved && schedule_ok_n >= 0)
  {
    reserved_cap =
        std::max(reserved_cap, std::min(schedule_ok_n + 1, fm_q));
  }
  return std::min(first_mesh_cap_base, reserved_cap);
}

/// FP-G1.1: FM enqueue reserve caps first_mesh schedule on cruise only.
/// During enter lit / quiesce it zeros first_mesh_cap and stalls spawn ring.
inline bool ShouldDeferFmDirtyEnqueueReserve(bool enter_lit_gate,
                                             bool enter_lit_quiesce,
                                             bool enter_fov_lit)
{
  return enter_lit_gate || enter_lit_quiesce || enter_fov_lit;
}

} // namespace cutum

#pragma once

#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>
#include <cstdint>

namespace cutum
{

/// Era20: SoftDefer empty placeholder → FirstMesh ticket (pure policy).
/// HasGreedy && !Drawable && solid && !in-flight work.
inline bool IsSoftDeferEmptyPlaceholder(bool has_greedy, bool has_drawable,
                                        bool is_dirty, bool pending_gpu,
                                        bool inflight, bool any_solid)
{
  if (has_drawable || pending_gpu || inflight || is_dirty)
  {
    return false;
  }
  if (!has_greedy || !any_solid)
  {
    return false;
  }
  return true;
}

/// Stuck SoftDefer empty → ColumnFlow FirstMesh while FOV miss OR in-focus
/// SoftDefer-held undrawn (Era32 P3: black-as-hole after LitDrawable ring).
inline bool ShouldEnqueueSoftDeferEmptyFirstMesh(bool empty_placeholder,
                                                 int horiz,
                                                 bool missing_visible_mesh,
                                                 bool in_focus = false)
{
  (void)horiz;
  if (!empty_placeholder)
  {
    return false;
  }
  return missing_visible_mesh || in_focus;
}

/// Era20 I-M2: Imm/Force Dirty escape when miss and async dead (ignore wall).
inline bool ShouldColdAsyncImmEscape(bool missing_visible_mesh, int mesh_async)
{
  return missing_visible_mesh && mesh_async < 2;
}

/// Era22 I-S1: SoftDefer must not drop/park !Drawable FirstMesh without a
/// schedule under miss-class or in-focus (place-to-reveal / SoftDeferHeld SLA).
inline bool ShouldScheduleFirstMeshUnderSoftDefer(bool has_drawable,
                                                  bool miss_or_in_focus,
                                                  int horiz = -1,
                                                  int protect_horiz = 8)
{
  if (has_drawable)
  {
    return false;
  }
  if (miss_or_in_focus)
  {
    return true;
  }
  return horiz >= 0 && horiz <= protect_horiz;
}

/// Era22 I-S2: SoftDeferHeld side-set counts as repair progress for no_ticket
/// honesty (Hide⇒Ticket).
inline bool SoftDeferHeldCountsAsRepairProgress(bool soft_defer_held)
{
  return soft_defer_held;
}

/// Era22 I-V3: VB ticket collect radius. Count uses full focus; under miss
/// collect must not clamp to r≤2 while no_ticket orphans exist.
inline int VisibleBlackTicketCollectRadius(int focus_radius,
                                           bool missing_visible_mesh,
                                           bool visible_black_no_ticket)
{
  const int r = std::max(0, focus_radius);
  if (visible_black_no_ticket || !missing_visible_mesh)
  {
    return r;
  }
  return std::min(2, r);
}

/// Era22 I-V3: enqueue nearest no_ticket VB repair while async has headroom.
inline bool ShouldEnqueueNearestVbNoTicket(bool visible_black_no_ticket,
                                           bool async_ok)
{
  return async_ok && visible_black_no_ticket;
}

/// Era22 I-M8: miss witness age (periods ≈2s) ⇒ PreferKick + admit bump.
inline bool ShouldMissTimeSlaKick(bool missing_visible_mesh,
                                  int miss_age_periods,
                                  int sla_periods = 2)
{
  return missing_visible_mesh && miss_age_periods >= sla_periods;
}

/// Closeout F: schedule floor folded into pools — always 0.
inline int AsyncScheduleFloorUnderMiss(bool /*miss_or_unfinished_visual*/)
{
  return 0;
}

/// Era23 I-V6: SoftDeferHeld must not count as void-heal progress while the
/// column still has fully-dark faces (Collect skip masked void on 172232).
inline bool SoftDeferHeldCountsAsVoidProgress(bool soft_defer_held,
                                              bool has_fully_dark_face)
{
  if (has_fully_dark_face)
  {
    return false;
  }
  return soft_defer_held;
}

/// Era23 I-V4 / Era31 I-T1: reserve Relight slots for void>T, miss+void, or VB.
inline bool ShouldReserveVoidRelightSlots(int dark_face_void_near_n,
                                          int visible_black_n,
                                          bool missing_visible_mesh,
                                          int void_threshold = 200)
{
  if (dark_face_void_near_n > void_threshold)
  {
    return true;
  }
  if (visible_black_n > 0)
  {
    return true;
  }
  return missing_visible_mesh && dark_face_void_near_n > 0;
}

/// Era23 I-V4: void collect cap independent of no_ticket nearest-N=1–2.
inline int VoidRelightCollectCap(int repair_cap, bool void_pressure)
{
  const int base = std::max(1, repair_cap);
  if (void_pressure)
  {
    return std::max(base, std::min(4, std::max(2, repair_cap)));
  }
  return base;
}

/// Era23 I-V5: NotePendingLight when enqueueing void Relight (not only Dispatch).
inline bool ShouldNotePendingLightOnVoidEnqueue(bool fully_dark_or_no_sky)
{
  return fully_dark_or_no_sky;
}

/// Era23 I-M9: PreferKick miss witness every miss-frame in FirstMesh class
/// (do not wait age≥2 periods).
inline bool ShouldPreferKickMissWitnessEarly(bool missing_visible_mesh,
                                             bool miss_first_mesh_class)
{
  return missing_visible_mesh && miss_first_mesh_class;
}

/// FZ2.7-P16 U1 / SRBR-P0.2: underfeet/near nh≤2 FirstMesh pin — cruise and
/// idle (no !moving gate). miss_horiz is Chebyshev to focus ground.
inline bool ShouldPinIsolatedMissUnderfeet(bool found_nearest_missing,
                                           int miss_horiz)
{
  return found_nearest_missing && miss_horiz >= 0 && miss_horiz <= 2;
}

/// SRBR-P0.2: spawn-ring catch-up pins nh≤enter work radius (fz-cold-enter).
inline bool ShouldPinIsolatedMissSpawnRing(bool spawn_catch_up,
                                           bool found_nearest_missing,
                                           int miss_horiz,
                                           bool enter_session_active = false)
{
  if (enter_session_active)
  {
    return false;
  }
  return spawn_catch_up && found_nearest_missing && miss_horiz >= 0 &&
         miss_horiz <= kVisualStageLitDrawableHoriz;
}

/// Hold nh≤2 miss witness while the pinned slice is still greedy-missing.
inline bool ShouldHoldNearMissWitness(int pinned_horiz, bool still_missing)
{
  return still_missing && pinned_horiz >= 0 &&
         pinned_horiz <= kVisualStageNearFovHoriz;
}

/// Enter burst: heal pinned underfeet/near miss every frame (miss_stuck SLA).
inline bool ShouldBurstHealPinnedMiss(bool focus_missing_mesh, int miss_horiz,
                                      bool enter_burst, bool spawn_catch_up)
{
  return focus_missing_mesh && miss_horiz >= 0 && miss_horiz <= 1 &&
         (enter_burst || spawn_catch_up);
}

/// TD-ARCH-021: spawn-ring catch-up must run during enter fly (fz-cold-enter).
inline bool ShouldRunSpawnRingCatchUpHeal(bool spawn_catch_up, bool moving_fast,
                                          bool underfeet_miss,
                                          bool enter_session_active = false)
{
  if (enter_session_active)
  {
    return false;
  }
  if (!spawn_catch_up && !underfeet_miss)
  {
    return false;
  }
  return !moving_fast || spawn_catch_up || underfeet_miss;
}

/// I8-D1: moving nh≤1 miss — renew ColumnFlow FirstMesh every frame (budget axis).
inline bool ShouldRenewMovingNearMissFirstMesh(bool moving,
                                               bool focus_missing_mesh,
                                               int miss_horiz,
                                               bool no_drawable_on_witness)
{
  return moving && focus_missing_mesh && no_drawable_on_witness &&
         miss_horiz >= 0 && miss_horiz <= 1;
}

/// FZ2.7-P16 U2: column-owned FirstMesh on miss witness (nh≤2, !drawable).
inline bool ShouldEnqueueWitnessOwnedFirstMesh(bool focus_missing_mesh,
                                               int miss_horiz,
                                               bool no_drawable_on_witness)
{
  if (!focus_missing_mesh || !no_drawable_on_witness)
  {
    return false;
  }
  return miss_horiz >= 0 && miss_horiz <= 4;
}

/// SRBR-P0: Dirty only for resident chunks (!HasChunk = ghost thrash, 112418).
inline bool ShouldAdmitResidentDirty(bool has_chunk) { return has_chunk; }

inline bool ShouldPruneGhostDirtyCoord(bool has_chunk) { return !has_chunk; }

/// Bulk prune per emerge/rebuild tick (schedule RemoveAt×54/frame is the tax).
inline int GhostDirtyPruneCapPerTick(bool visual_holes)
{
  return visual_holes ? 64 : 24;
}

/// SRBR-P0: sticky underfeet/near miss FirstMesh only when the slice is loaded.
inline bool ShouldGuaranteeResidentWitnessFirstMesh(bool has_chunk,
                                                    bool no_drawable,
                                                    int miss_horiz)
{
  return has_chunk && no_drawable && miss_horiz >= 0 && miss_horiz <= 1;
}

/// SRBR-P0.2 / ColPipe P4: one miss owner — Dirty, RAA, Inflight, SoftDeferHeld,
/// PendingGpu, or FirstMesh ticket. Dual MarkDirty+Enqueue refeed forever.
/// has_drawable_greedy_mesh=false: RAA/Dirty without drawable is not owned.
inline bool MissSliceAlreadyOwned(bool dirty, bool remesh_after_apply,
                                  bool inflight, bool soft_defer_held,
                                  bool pending_gpu, bool first_mesh_ticket,
                                  bool has_drawable_greedy_mesh = true)
{
  if (!has_drawable_greedy_mesh)
  {
    return first_mesh_ticket || inflight || pending_gpu;
  }
  return dirty || remesh_after_apply || inflight || soft_defer_held ||
         pending_gpu || first_mesh_ticket;
}

/// SoftDeferHeld owns Hide⇒Ticket — refresh ticket only, never MarkDirty.
inline bool MissSliceSoftDeferOwns(bool soft_defer_held)
{
  return soft_defer_held;
}

/// SoftDeferHeld ticket-only while SoftDefer still owns publication.
/// SoftDefer lifted → transfer to one Dirty (never Held+ticket orphan).
inline bool ShouldTransferSoftDeferHeldToDirty(bool soft_defer_held,
                                               bool soft_defer_still_active)
{
  return soft_defer_held && !soft_defer_still_active;
}

/// Intentional occluded empty: GpuResident 0-quad after SoftDefer lift publish.
/// SoftDefer sticky empty (Held / defer still ON) must stay !ready (I-M3).
inline bool IsIntentionalOccludedEmptyReady(bool has_greedy, bool has_drawable,
                                            bool gpu_resident, int gpu_quad_count,
                                            bool soft_defer_held,
                                            bool defer_until_lit)
{
  if (!has_greedy || has_drawable || soft_defer_held || defer_until_lit)
  {
    return false;
  }
  return gpu_resident && gpu_quad_count == 0;
}

/// CPU-published occluded empty while I-R1 keeps live SSBO (!GpuResident or
/// stale GpuQuadCount). SoftDefer sticky empty must stay !ready (I-M3).
inline bool IsCpuPublishedOccludedEmptyReady(bool has_greedy, bool has_drawable,
                                             bool cpu_batches_empty,
                                             bool soft_defer_held,
                                             bool defer_until_lit)
{
  if (!has_greedy || has_drawable || soft_defer_held || defer_until_lit ||
      !cpu_batches_empty)
  {
    return false;
  }
  return true;
}

/// EnterLitQuiesce + SoftDefer still active: one Dirty owner (not Held park).
inline bool ShouldEnterSoftDeferEmptyTransferDirty(bool enter_lit_quiesce,
                                                   bool defer_until_lit)
{
  return enter_lit_quiesce && defer_until_lit;
}

/// Mesh pipeline owns the slice — no second Enqueue/MarkDirty.
inline bool MissSlicePipelineOwns(bool dirty, bool remesh_after_apply,
                                  bool inflight, bool pending_gpu)
{
  return dirty || remesh_after_apply || inflight || pending_gpu;
}

/// Pin may MarkDirty only when nothing owns the slice yet.
inline bool ShouldPinIsolatedMissMarkDirty(bool resident, bool already_owned,
                                           bool column_ready)
{
  return resident && !already_owned && !column_ready;
}

/// SRBR-P0.2: under enter gate/quiesce, miss_undrawn must not RAA-park holes
/// (dirty=0 orphan). Force one Dirty owner instead.
inline bool ShouldForceEnterHoleDirty(bool enter_lit_quiesce,
                                      bool enter_gpu_quiesce_drain)
{
  return enter_lit_quiesce || enter_gpu_quiesce_drain;
}

/// SRBR-P0.2: under enter, sticky Inflight on !drawable must not RemoveAt Dirty.
inline bool ShouldKeepEnterHoleDirtyDespiteInflight(bool enter_hole_force,
                                                    bool has_drawable)
{
  return enter_hole_force && !has_drawable;
}

/// SRBR-P0.2: phantom enter Dirty is terminal-held or unloaded only —
/// never strip FirstMesh hole Dirty (!HasGreedy).
inline bool ShouldPruneEnterPhantomDirtyCoord(bool enter_terminal_held,
                                             bool has_chunk)
{
  return enter_terminal_held || !has_chunk;
}

/// Era23 P2: SoftDefer empty PreferKick only when GPU queue is stuck on the
/// same coord (not empty placeholder apply storm).
inline bool ShouldPreferKickSoftDeferEmptyStuck(bool soft_defer_empty,
                                                bool missing_visible_mesh,
                                                bool queued_or_kicked_stuck,
                                                int age_frames = 0,
                                                bool has_drawable = false)
{
  if (has_drawable)
  {
    return false;
  }
  if (soft_defer_empty && missing_visible_mesh && queued_or_kicked_stuck)
  {
    return true;
  }
  return soft_defer_empty && missing_visible_mesh && age_frames >= 15;
}

/// Era23 I-P1: SoftDefer empty / !Drawable place column needs FirstMesh SLA.
inline bool ShouldForceFirstMeshOnPlaceHole(bool soft_defer_empty_or_undrawn,
                                            bool near_or_underfeet)
{
  return soft_defer_empty_or_undrawn && near_or_underfeet;
}

/// Era24 I-E2: SoftDefer empty under FOV miss/focus needs FirstMesh ownership
/// until Drawable (Hide⇒Ticket).
inline bool SoftDeferEmptyNeedsFirstMeshOwnership(bool empty_placeholder,
                                                  bool miss_or_in_focus)
{
  return empty_placeholder && miss_or_in_focus;
}

/// Era24 I-E4: SoftDefer empty age (frames) ⇒ escalate PreferKick/Capture.
/// Era32/33: tighter SLA under FOV miss — 15 frames (~0.25s @60) vs 30
/// (miss_stuck 28–54s / holes≈1.0 when PreferKick waited too long).
inline bool ShouldEscalateSoftDeferEmptyAge(int age_frames,
                                            int sla_frames = 15)
{
  return age_frames >= sla_frames;
}

/// Era24 I-E3: SoftDefer empty heal is FirstMesh-class only (not Remesh/Relight).
enum class SoftDeferEmptyHealKind : uint8_t
{
  FirstMesh = 0
};

inline SoftDeferEmptyHealKind SoftDeferEmptyHealKindOf()
{
  return SoftDeferEmptyHealKind::FirstMesh;
}

/// Era39: SoftDefer-hidden neighbor (loaded solid, no drawable / SoftDefer
/// empty or Held) must not occlude faces of a ready chunk.
inline bool IsSoftDeferHiddenNeighbor(bool neighbor_chunk_loaded,
                                      bool neighbor_visually_drawable,
                                      bool soft_defer_empty_or_held)
{
  if (!neighbor_chunk_loaded || neighbor_visually_drawable)
  {
    return false;
  }
  return soft_defer_empty_or_held;
}

} // namespace cutum

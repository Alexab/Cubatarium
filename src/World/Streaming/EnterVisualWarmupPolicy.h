#pragma once

#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>
#include <string>
#include <utility>

namespace cutum
{

/// Era20/33: spawn mesh ring for enter warmup (HasMissing / gate SoT).
inline constexpr int kEnterSpawnMeshRingHoriz = 2;

/// (Era29 was underfeet r=1; cold create entered InGame with empty FOV.)
inline int EnterVisualWarmupRadiusChunks()
{
  return kVisualStageLitDrawableHoriz;
}

/// Enter worklist / hide / snapshot lit debt share this ring (not RD).
inline int EnterVisualWorkRadiusChunks()
{
  return kVisualStageLitDrawableHoriz;
}

/// SRBR-P0.2: miss probe + pending_light share enter ring (not full RD).
inline bool ShouldUseEnterSpawnMissProbe(bool enter_lit_gate,
                                         bool enter_mesh_warmup,
                                         bool spawn_catch_up, bool moving_fast)
{
  if (enter_lit_gate || enter_mesh_warmup)
  {
    return true;
  }
  // Idle/cooperative enter only — cruise fly must keep full-RD SoT.
  return spawn_catch_up && !moving_fast;
}

/// Do not park spawn-ring Dirty while catch-up / near miss heal is active.
inline bool ShouldSkipParkSpawnRingForMissHeal(bool spawn_catch_up,
                                               bool focus_missing_mesh,
                                               int miss_horiz,
                                               bool enter_session_active = false)
{
  if (enter_session_active)
  {
    return false;
  }
  if (spawn_catch_up)
  {
    return true;
  }
  return focus_missing_mesh && miss_horiz >= 0 &&
         miss_horiz <= kVisualStageLitDrawableHoriz;
}

/// Remesh only when lightmap or voxels actually changed.
inline bool ShouldRemeshAfterLightApply(bool light_or_voxel_delta)
{
  return light_or_voxel_delta;
}

/// No-op FullyDark spin: field already 0 and this tick has no light delta.
inline bool ShouldSpinFullyDarkRemesh(bool fully_dark, bool stale_field,
                                      bool light_delta)
{
  return fully_dark && !stale_field && !light_delta;
}

/// Underfeet slice may exit when lit-bound or true-dark (field 0, not pending).
inline bool EnterUnderfeetSliceReady(bool has_lit_drawable, bool pending_light,
                                     bool true_dark_field_zero)
{
  if (pending_light)
  {
    return false;
  }
  return has_lit_drawable || true_dark_field_zero;
}

/// Underfeet present: slice ready AND focus column is in opaque draw.
/// Do not treat underfeet_need==0 as "no hole".
inline bool EnterUnderfeetPresentReady(bool slice_ready, bool opaque_present)
{
  return slice_ready && opaque_present;
}

/// FullyDark column is enter-resolved only after bake outcome (lit or true-dark).
/// OpenSky / RelightThenMesh alone is never settled.
inline bool EnterFullyDarkColumnSettled(bool open_sky_applied, bool pending,
                                        bool lit_ready, bool stale_field,
                                        bool has_lit_drawable)
{
  if (pending || !lit_ready)
  {
    return false;
  }
  if (has_lit_drawable)
  {
    return true;
  }
  // true-dark: OpenSky/relight owned, light field 0 (not stale).
  return open_sky_applied && !stale_field;
}

/// Worklist Done is enter SoT — snapshot debt must not outlive it (manual
/// 173849: Remaining=0 while snapshot_debt stuck 3–10 blocked enter_ready).
inline bool EnterLitSnapshotResolvedByWorklistDone(bool gate_captured,
                                                   bool col_in_worklist,
                                                   bool col_done)
{
  return gate_captured && col_in_worklist && col_done;
}

/// One Sticky remesh attempt under enter gate exhausts snapshot remesh debt
/// for that column (still FullyDark void-edge is accepted after the attempt).
inline bool EnterLitSnapshotResolvedByStickyRemesh(bool enter_gate_active,
                                                   bool sticky_owned,
                                                   bool pending,
                                                   bool lit_ready)
{
  return enter_gate_active && sticky_owned && !pending && lit_ready;
}

/// Underfeet solid-without-drawable force: only when no owner already holds
/// FirstMesh/remesh (else every-frame MarkDirtyPriority livelocks Dirty).
inline bool ShouldForceUnderfeetSolidFirstMeshDirty(
    bool has_drawable, bool has_solid, bool already_dirty, bool soft_held,
    bool inflight, bool pending_gpu, bool raa_pending)
{
  if (has_drawable || !has_solid)
  {
    return false;
  }
  if (already_dirty || soft_held || inflight || pending_gpu || raa_pending)
  {
    return false;
  }
  return true;
}

/// EnterLitQuiesce: SoftDefer empty / undrawn in spawn r≤2 must stay Dirty so
/// FirstMesh can schedule. Parking to SoftDeferHeld then Requeue→prune cancelled
/// Dirty every frame (manual 180247: dirty=0 gpu=0 missing=1 forever).
inline bool EnterLitQuiesceKeepSpawnUndrawnDirty(bool enter_lit_quiesce,
                                                bool has_drawable, int horiz,
                                                int spawn_radius = 2)
{
  return enter_lit_quiesce && !has_drawable && horiz >= 0 &&
         horiz <= spawn_radius;
}

/// EnterLitQuiesce + spawn ring: candidate SoftDefer lift (r≤2).
/// Call sites must also require !PendingLight — lift while pending published
/// SoftDefer-empty as intentional empty → sticky miss dirty=0 (102235/094710).
inline bool EnterLitQuiesceLiftSpawnSoftDefer(bool enter_lit_quiesce, int horiz,
                                             int spawn_radius = 2)
{
  return enter_lit_quiesce && horiz >= 0 && horiz <= spawn_radius;
}

/// SoftDefer lift under enter only when spawn column light is not pending.
/// underfeet_exit_blocked: nh≤1 may lift despite pending_light — InGame exit
/// SoT (manual 202127: missing=1 dirty=0 gpu=0 at 99% progress).
inline bool EnterLitQuiesceMayLiftSpawnSoftDefer(bool enter_lit_quiesce,
                                                 int horiz,
                                                 bool pending_light,
                                                 int spawn_radius = 2,
                                                 bool underfeet_exit_blocked =
                                                     false)
{
  if (underfeet_exit_blocked && horiz >= 0 && horiz <= 1)
  {
    return EnterLitQuiesceLiftSpawnSoftDefer(enter_lit_quiesce, horiz,
                                             spawn_radius);
  }
  return EnterLitQuiesceLiftSpawnSoftDefer(enter_lit_quiesce, horiz,
                                           spawn_radius) &&
         !pending_light;
}

/// Presentable cy band for enter spawn-ring SoT (not bedrock cy=0 alone).
/// Matches underfeet/enter visual band — manual 182802: missing stuck on cy=0
/// SoftDefer empty while underfeet_present_ready=1 at y≈96.
inline void EnterSpawnPresentableCyRange(int player_cy, int sea_cy,
                                         bool fill_water, int max_cy,
                                         int &out_cy0, int &out_cy1)
{
  out_cy0 = std::max(0, player_cy - 1);
  out_cy1 = std::min(max_cy, std::max(player_cy + 1,
                                      fill_water ? sea_cy + 1 : player_cy + 1));
  if (fill_water)
  {
    out_cy0 = std::min(out_cy0, std::max(0, sea_cy - 1));
  }
}

/// Dirty/GPU/async outside the presentable band must not block enter exit
/// after worklist Done + underfeet present (Era49 comment / manual 182802).
inline bool EnterSpawnRingIgnoresHinterlandMeshDebt(bool enter_gate_active,
                                                    int visibility_debt,
                                                    bool underfeet_present)
{
  return enter_gate_active && visibility_debt <= 0 && underfeet_present;
}

/// Hide FullyDark in enter ring — ColPipe P7: keep-until-replace; never blank.
/// LitRing: enter FullyDark → hole until lit drawable or settled true-dark.
inline bool ShouldHideEnterFullyDark(bool fully_dark, bool /*pending*/,
                                     bool /*stale_field*/,
                                     bool has_lit_drawable, bool true_dark)
{
  if (!fully_dark || has_lit_drawable || true_dark)
  {
    return false;
  }
  return true;
}

/// Load MeshWarmup must not FirstMesh/Dirty spawn ring while relight is deferred.
inline bool ShouldSkipSpawnMeshWhileRelightDeferred(bool lighting_relight_deferred,
                                                    int horiz)
{
  return lighting_relight_deferred && horiz <= kVisualStageLitDrawableHoriz;
}

/// Era34 P0: create-bar SoftDefer settle radius (near FOV); ring 3–4 InGame.
inline int CreateNearFovSoftDeferRadiusChunks()
{
  return 2;
}

/// Era34 P0: soft leave PrepareView after underfeet lit (ms wall).
inline int CreateSpawnWarmupSoftWallMs()
{
  return 12000;
}

/// Era34 P0: hard safety leave PrepareView (ms wall).
inline int CreateSpawnWarmupHardWallMs()
{
  return 20000;
}

/// Era34 P0: tick safety ceiling (≪ Era33 1800).
inline int CreateSpawnWarmupMaxTicks()
{
  return 360;
}

/// Era34 P0: debt_frac = clamp(debt / max(1, denom), 0, 1).
inline float CreateBarDebtFraction(int debt, int denom)
{
  const int d = std::max(1, denom);
  const float frac =
      static_cast<float>(std::max(0, debt)) / static_cast<float>(d);
  return std::min(1.0f, std::max(0.0f, frac));
}

/// Era34 P0: soft leave only after underfeet LitDrawable ready.
inline bool ShouldSoftLeaveCreateSpawnWarmup(bool underfeet_lit_ready,
                                             double elapsed_ms)
{
  return underfeet_lit_ready &&
         elapsed_ms >= static_cast<double>(CreateSpawnWarmupSoftWallMs());
}

/// Era34 P0: hard leave on wall or tick ceiling.
inline bool ShouldHardLeaveCreateSpawnWarmup(double elapsed_ms, int ticks)
{
  return elapsed_ms >= static_cast<double>(CreateSpawnWarmupHardWallMs()) ||
         ticks >= CreateSpawnWarmupMaxTicks();
}

/// Era29 I-E1: underfeet still needs visual warmup when neither lit drawable
/// nor keep-prior GPU is available.
inline bool EnterUnderfeetNeedsLitDrawable(bool has_lit_drawable,
                                           bool keep_prior_gpu)
{
  return !has_lit_drawable && !keep_prior_gpu;
}

/// Era22 / ColPipe: underfeet_need = feet column only (priority/lease).
/// Neighbor hole or PendingLight in r≤1 must NOT latch need when the feet
/// column itself is OK — that remeshed player+nearest forever (manual 175310).
inline bool UnderfeetNeedUrgent(bool missing_feet_column, bool pending_feet,
                                bool underfeet_undrawn)
{
  return missing_feet_column || pending_feet || underfeet_undrawn;
}

/// Streaming pressure / load caps: feet column only (Chebyshev r=0 mesh +
/// pending on focus xz). incomplete_camera_column is the feet ground column.
inline bool FeetColumnUnderfeetNeed(bool incomplete_camera_column,
                                    bool missing_feet_mesh,
                                    bool pending_feet_light)
{
  return incomplete_camera_column || missing_feet_mesh || pending_feet_light;
}

/// Era29 I-E4: SoftDefer empty underfeet needs FirstMesh ownership on bar.
inline bool EnterSoftDeferEmptyNeedsFirstMesh(bool empty_or_held, bool underfeet)
{
  return empty_or_held && underfeet;
}

/// Era29 I-E2: always run budgeted TickEnterStreamingWarmup on progress bar
/// even after cooperative spawn prepare (no MarkAllDirty).
inline bool ShouldRunEnterStreamingWarmupDespiteSpawnPrepared(
    bool /*spawn_prepared*/)
{
  return true;
}

/// Era29 I-E5: soft enter_app budget ms (KEEP Era20 ~100; allow ≤200).
inline int EnterVisualWarmupAppUpdateSoftMs()
{
  return 200;
}

/// Era29 P4: opaque cmd swing above this on stop/enter ⇒ churn regress soft.
inline int EnterOpaqueChurnSoftMax()
{
  return 200;
}

/// Era29 P3: Capture witness pin frames after PrepareEnterGameSession (vs Era27 T=8).
inline int EnterSpawnCapturePinFrames()
{
  return 16;
}

/// Era29 P3: near FOV CollectFullyDark must not skip on PendingLight alone —
/// report VB honesty so RelightThenMesh dual-queue stays visible (091332).
inline bool CollectFullyDarkShouldSkipForOwnership(int horiz,
                                                   bool has_relight_or_pending,
                                                   int near_honesty_r = 1)
{
  if (horiz <= near_honesty_r)
  {
    return false;
  }
  return has_relight_or_pending;
}

/// Era29 P3: far Unlit (horiz>near_r) with drawable already uploaded — after
/// lit prefer RemeshAfterApply, not MarkDirty remesh-on-lit storm.
inline bool ShouldDampFarUnlitRemeshOnLit(bool has_drawable, int horiz,
                                         int near_r = 2)
{
  return has_drawable && horiz > near_r;
}

/// Era29 P4: idle/stop with live drawable → RemeshAfterApply only (opaque churn).
inline bool ShouldRemeshAfterApplyOnlyOnIdleDrawable(bool idle_or_suppress,
                                                     bool has_drawable)
{
  return idle_or_suppress && has_drawable;
}

/// Era36 B1: visible surface cy-band (surface_cy-1 .. surface_cy+3).
inline std::pair<int, int> RelightSurfaceBandCy(int surface_block_y,
                                                int chunk_size, int max_cy)
{
  const int surface_cy = std::max(0, surface_block_y / chunk_size);
  return {std::max(0, surface_cy - 1), std::min(max_cy, surface_cy + 3)};
}

/// Era36 B1: relight Y-band surface clamp — skip underground chunks that
/// are invisible from the surface. Returns min_y clamped to surface band.
inline int RelightSurfaceBandMinY(int surface_block_y, int chunk_size,
                                  int original_min_y)
{
  const int surface_min = std::max(0, surface_block_y - chunk_size);
  return std::max(original_min_y, surface_min);
}

/// Era36 B1: clamp max_y to surface band top (surface_cy+3).
inline int RelightSurfaceBandMaxY(int surface_block_y, int chunk_size,
                                  int max_height, int original_max_y)
{
  const int max_cy = std::max(0, max_height / chunk_size);
  const int cy1 = RelightSurfaceBandCy(surface_block_y, chunk_size, max_cy).second;
  const int surface_max = std::min(max_height, (cy1 + 1) * chunk_size - 1);
  return std::min(original_max_y, surface_max);
}

/// Cruise SOTA / Minetest: trust disk light when had_disk_light && complete.
inline bool ShouldTrustDiskLightmap(bool had_disk_light, bool light_complete,
                                    bool lighting_deferred)
{
  return !lighting_deferred && had_disk_light && light_complete;
}

/// Skip full EnqueueTerrainColumnRelight when disk light is trusted.
inline bool ShouldSkipRelightOnTrustedDiskLight(bool trust_disk_light)
{
  return trust_disk_light;
}

/// Bake-before-present: LitReady on trusted disk only after lit (non-FullyDark)
/// drawable settle — light_complete ≠ mesh ready / Minetest lighting_complete.
inline bool ShouldSetLitReadyOnTrustedDisk(bool has_lit_drawable,
                                           bool remesh_in_flight)
{
  return has_lit_drawable && !remesh_in_flight;
}

/// P5: dynamic moving Capture cap can rise from 1 to 2 only after FIFO/apply
/// stability gates pass (checked by ShouldAllowDynamicCaptureMovingBgCap).
inline int DynamicCaptureMovingBgCap(int pending_light_focus,
                                     int base_cap = 1)
{
  if (pending_light_focus <= 15)
  {
    return base_cap;
  }
  return std::max(base_cap, 2);
}

/// P5 / ColdSupply / RateMatch R1: raise CaptureMovingBgCap above 1 when Apply
/// unit is cheap. High PL (>30): only if apply_n_prev ≥ 2 (Capture≤Apply), not
/// PL alone (manual 190534 Capture=2 Apply=1 → PL plateau).
inline bool ShouldAllowDynamicCaptureMovingBgCap(int fifo_drop_delta,
                                                 int fifo_pin_drop_n,
                                                 double apply_ms,
                                                 int apply_n = 0,
                                                 int pending_light_focus = 0)
{
  if (fifo_pin_drop_n > 0)
  {
    return false;
  }
  const double unit_ms =
      (apply_n > 0) ? (apply_ms / static_cast<double>(apply_n)) : apply_ms;
  if (unit_ms > 8.0)
  {
    return false;
  }
  if (pending_light_focus > 30)
  {
    return apply_n >= 2;
  }
  if (fifo_drop_delta > 0)
  {
    return false;
  }
  return true;
}

/// Cruise SOTA: moving + visual holes — never raise bg above dynamic cap.
inline int ClampCaptureMovingBgCapWithHoles(int bg_cap, bool moving,
                                            bool visual_holes,
                                            int dynamic_cap)
{
  if (!moving)
  {
    return bg_cap;
  }
  const int capped = std::min(bg_cap, std::max(1, dynamic_cap));
  if (visual_holes)
  {
    return std::min(capped, std::max(1, dynamic_cap));
  }
  return capped;
}

/// Cruise SOTA: moving + holes — narrow Capture Y-band (hot surface only).
inline int EffectiveRelightCaptureBandCy(int band_cy, bool moving,
                                         bool visual_holes)
{
  const int base = std::max(0, band_cy);
  if (moving && visual_holes && base > 1)
  {
    return std::max(1, base - 1);
  }
  return base;
}

/// Era36 B3 / Era40: land moving drain when pendf>15 (was 30).
inline bool ShouldDrainPendingLightLandMoving(int pending_light_focus,
                                              int threshold = 15)
{
  return pending_light_focus > threshold;
}

/// Closeout F: floors folded into WorkPoolBudget — always 0 (no *FloorMs).
inline int LandMovingRelightDrainFloor(bool /*moving*/, int /*pending_light_focus*/,
                                       int /*threshold*/ = 15)
{
  return 0;
}

/// Era37 P5: per-column surface Y for relight band (fallback to focus Y).
inline int RelightColumnSurfaceBlockY(int focus_block_y, int column_top_block_y)
{
  if (column_top_block_y >= 0)
  {
    return column_top_block_y;
  }
  return focus_block_y;
}

/// Era37 P5: clamp relight block-Y range to visible surface for one column.
inline std::pair<int, int> RelightSurfaceBandForColumn(int focus_block_y,
                                                      int column_top_block_y,
                                                      int chunk_size,
                                                      int max_height,
                                                      int orig_min_y,
                                                      int orig_max_y)
{
  const int surface_y =
      RelightColumnSurfaceBlockY(focus_block_y, column_top_block_y);
  const int band_min = RelightSurfaceBandMinY(surface_y, chunk_size, orig_min_y);
  const int band_max =
      RelightSurfaceBandMaxY(surface_y, chunk_size, max_height, orig_max_y);
  return {band_min, band_max};
}

/// Era37 P0: allow unlit drawable in LitDrawable ring under light debt.
inline bool AllowUnlitDrawableUnderLightDebt(int pending_focus, int unlit_near,
                                             int horiz, bool fully_dark,
                                             bool has_greedy_mesh,
                                             bool underfeet,
                                             int pending_threshold = 15,
                                             int unlit_threshold = 10,
                                             int lit_ring =
                                                 kVisualStageLitDrawableHoriz)
{
  if (fully_dark || !has_greedy_mesh)
  {
    return false;
  }
  if (underfeet)
  {
    return false;
  }
  if (horiz > lit_ring)
  {
    return false;
  }
  return pending_focus > pending_threshold || unlit_near > unlit_threshold;
}

/// Era37 P1b / Era39 A4 / Era40: boost GPU relight apply when FIFO saturated.
/// Pending threshold 15 so cruise pendf≈14–16 still arms the floor.
inline int LandRelightGpuApplyFloor(int relight_fifo_n, int pending_focus,
                                    int base_gpu_apply, int fifo_threshold = 60,
                                    int pending_threshold = 15)
{
  if (relight_fifo_n <= fifo_threshold || pending_focus <= pending_threshold)
  {
    return base_gpu_apply;
  }
  return std::max(base_gpu_apply, 12);
}

/// B2: near underfeet nh≤1 miss — guarantee at least one GPU apply per frame.
inline int NearUnderfeetGpuApplyFloor(bool missing_underfeet, int nh,
                                      int pending_light, int base_gpu_apply)
{
  if (missing_underfeet && nh <= 1 && pending_light <= 0)
  {
    return std::max(base_gpu_apply, 1);
  }
  return base_gpu_apply;
}

/// Era37 P4: enter warmup SoftDefer empty ownership boost.
inline int EnterWarmupSoftDeferOwnershipCap(int base_cap, int softdefer_empty_n,
                                            bool enter_warmup_active)
{
  if (!enter_warmup_active || softdefer_empty_n <= 5)
  {
    return base_cap;
  }
  return std::max(base_cap, 18);
}

/// Era41/Era42: warn threshold ms for Enter lit pass (not force-abort when
/// EnterLitRequireZero). Runtime: streaming_tune.json `enter_fov_lit_hard_wall_ms`.
inline int EnterFovLitHardWallMs()
{
  return 120000;
}

/// Era41/Era42: Captures per enter lit tick.
/// Override: `enter_fov_lit_capture_budget`.
inline int EnterFovRelightCaptureBudget()
{
  return 16;
}

/// Era41/Era42: async Relight apply budget on enter lit pass.
/// Override: `enter_fov_lit_apply_budget`.
inline int EnterFovRelightApplyBudget()
{
  return 64;
}

/// Era41: progress fraction for Lighting bar (1 - debt/peak).
inline float EnterFovLitProgressFraction(int debt, int peak_debt)
{
  return 1.0f - CreateBarDebtFraction(debt, std::max(1, peak_debt));
}

/// Era41/Era42: hold enter bar while lit debt remains.
/// When require_zero (default), hard-wall does not release the bar.
inline bool ShouldHoldEnterBarForFovLit(int fov_lit_debt, double elapsed_ms,
                                        int hard_wall_ms = EnterFovLitHardWallMs(),
                                        bool require_zero = true,
                                        bool progress_stalled = false)
{
  if (fov_lit_debt <= 0)
  {
    return false;
  }
  // LitRing C: progress stall exits RequireZero (holes OK, no MarkAllDirty).
  if (progress_stalled)
  {
    return false;
  }
  if (require_zero)
  {
    return true;
  }
  return elapsed_ms < static_cast<double>(hard_wall_ms);
}

/// LitRing C: no FOV-lit progress for stall_ms while underfeet already lit
/// (or soft wall elapsed) → abort drain / force InGame with holes OK.
inline int EnterLitProgressStallMs()
{
  return 20000;
}

inline bool EnterLitDebtProgressStalled(int fov_debt, int best_fov_debt,
                                        bool underfeet_present_or_lit,
                                        double ms_since_progress,
                                        double stall_limit_ms,
                                        double elapsed_ms,
                                        double soft_wall_ms)
{
  if (fov_debt <= 0)
  {
    return false;
  }
  if (ms_since_progress < stall_limit_ms)
  {
    return false;
  }
  // Require underfeet present OR soft wall so we never abort mid first paint.
  if (!underfeet_present_or_lit && elapsed_ms < soft_wall_ms)
  {
    return false;
  }
  return fov_debt >= best_fov_debt;
}

/// Era43: skip streaming warmup while enter lit gate drains snapshot.
inline bool ShouldSkipEnterStreamingWarmup(bool enter_lit_gate_active)
{
  return enter_lit_gate_active;
}

/// Era43: block NotePending outside frozen snapshot.
inline bool ShouldBlockNotePendingOutsideSnapshot(bool gate_active,
                                                  bool in_snapshot)
{
  return gate_active && !in_snapshot;
}

/// Era43f: DrainEnterGameMeshWarmup must continue while GPU uploads remain.
inline bool ShouldContinueEnterMeshWarmupDrain(bool spawn_meshes_pending,
                                               bool async_mesh_pending,
                                               int gpu_pending_near)
{
  return spawn_meshes_pending || async_mesh_pending || gpu_pending_near > 0;
}

/// Era46: default mesh budget for enter warmup drain (matches Application).
constexpr int EnterWarmupMeshBudgetDefault()
{
  return 8;
}

/// Era46: coop/gpu_warmup shared drain must call explicit GPU path when blockers.
inline bool EnterWarmupDrainUsesGpuExplicitPath(bool needs_mesh_warmup)
{
  return needs_mesh_warmup;
}

/// Era46 B: after RAA erase on commit — PreferKick if GPU still pending.
inline bool ShouldPreferKickAfterRemeshAfterApplyCommit(bool gpu_pending)
{
  return gpu_pending;
}

/// Era46 B / Era47 P3 / Era49b / sky-fix / 123647: MarkDirty after RAA commit
/// when not already dirty and not GPU. Under enter lit gate — PreferKick only
/// for *lit* drawable remesh; !Drawable FirstMesh AND FullyDark drawable must
/// still MarkDirty or hide-until-lit sticks forever (opaque=0, RAA stuck).
inline bool ShouldMarkDirtyAfterRemeshAfterApplyCommit(
    bool already_dirty, bool gpu_pending, bool enter_lit_gate = false,
    bool needs_first_mesh = false, bool fully_dark_drawable = false)
{
  if (already_dirty || gpu_pending)
  {
    return false;
  }
  if (enter_lit_gate)
  {
    return needs_first_mesh || fully_dark_drawable;
  }
  return true;
}

/// Era49b: enter gate active ⇒ treat as enter_lit_gate for RAA commit policy.
inline bool EnterGateBlocksRaaMarkDirty(bool enter_lit_quiesce,
                                        bool enter_gpu_quiesce_drain)
{
  return enter_lit_quiesce || enter_gpu_quiesce_drain;
}

/// Suppress MarkRelit remesh only when the column is already enter-settled
/// (lit drawable or true-dark). remaining==0 / OpenSky alone must not suppress.
inline bool ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(
    bool enter_lit_gate_active, bool column_enter_settled,
    int /*fifo_n*/ = 0)
{
  return enter_lit_gate_active && column_enter_settled;
}

/// Era48: cruise/ocean void bias threshold (heal budgets — not enter exit).
inline int EnterVisibilityVoidNearMax()
{
  return 200;
}

/// Era51: enter exit unfinished void must be exactly 0.
inline int EnterVoidExitMax()
{
  return 0;
}

/// Era52: void telem excludes terminal / LitReady void-edge faces under enter.
inline bool EnterVoidTelemFaceExcluded(bool chunk_terminal, bool col_done,
                                       bool void_edge, bool enter_gate,
                                       bool col_lit_ready)
{
  if (chunk_terminal || col_done)
  {
    return true;
  }
  return enter_gate && void_edge && col_lit_ready;
}

/// Era53: stale FullyDark GPU commit under enter gate — latch terminal, no re-Dirty.
inline bool ShouldLatchStaleFullyDarkAfterEnterGpuCommit(
    bool enter_gpu_quiesce_drain, bool has_fully_dark_face,
    bool stale_lit_field, bool already_terminal)
{
  return enter_gpu_quiesce_drain && has_fully_dark_face && stale_lit_field &&
         !already_terminal;
}

/// Skip MarkRelit only after the column already remeshed and is no longer stale.
inline bool ShouldSkipMarkRelitAfterEnterStaleAttempt(bool enter_gate_active,
                                                      bool stale_attempt_owned,
                                                      bool still_stale)
{
  return enter_gate_active && stale_attempt_owned && !still_stale;
}

/// Era54: gate-accepted FullyDark drawable (terminal / column Done) satisfies
/// visual warmup — Relight cannot invent rim light on void-edge (Era50).
inline bool EnterFullyDarkDrawableAcceptedForWarmupExit(
    bool fully_dark_drawable, bool enter_terminal_held, bool gate_column_done)
{
  if (!fully_dark_drawable)
  {
    return false;
  }
  return enter_terminal_held || gate_column_done;
}

/// Era55: HoldEnterTerminal also HoldSoftDeferFirstMesh — SoftDefer underfeet
/// must not reopen Era29 FirstMesh while the slice is already terminal.
inline bool EnterSoftDeferBlocksWarmupExit(bool empty_or_held, bool underfeet,
                                           bool enter_terminal_held)
{
  return EnterSoftDeferEmptyNeedsFirstMesh(empty_or_held, underfeet) &&
         !enter_terminal_held;
}

/// Yield visual warmup only when gate remaining==0 AND underfeet present
/// (slice lit/true-dark + opaque draw). remaining alone is not SoT.
/// Underfeet GPU pending must not be ignored — CPU drawable ≠ first paint.
inline bool EnterVisualWarmupYieldsToGateRemaining(bool enter_gate_active,
                                                   int visibility_debt,
                                                   bool underfeet_present_ready,
                                                   int underfeet_gpu_pending = 0)
{
  return enter_gate_active && visibility_debt <= 0 &&
         underfeet_present_ready && underfeet_gpu_pending <= 0;
}

/// Alias kept for ocean/cruise policy readers.
inline int OceanHealVoidBias()
{
  return EnterVisibilityVoidNearMax();
}

/// Era48 DoD radius for visibility-ready = render distance (caller supplies RD).
inline int EnterVisibilityReadyRadiusChunks(int render_distance_chunks)
{
  return std::max(0, render_distance_chunks);
}

/// Era48/Era51: void telem gate — ignore until first dark-face sample exists.
/// Default void_max is EnterVoidExitMax (0). Cruise callers pass OceanHealVoidBias.
inline bool EnterVisibilityVoidReady(int dark_face_near_n,
                                     int dark_face_void_near_n,
                                     int void_max = EnterVoidExitMax())
{
  if (dark_face_near_n <= 0)
  {
    return true;
  }
  return dark_face_void_near_n <= void_max;
}

/// Era46 C: escalate GPU drain after abort_drain wall (ms).
inline bool ShouldEscalateEnterWarmupGpuDrain(bool abort_drain, double elapsed_ms,
                                              int escalate_ms = 180000)
{
  return abort_drain && escalate_ms > 0 &&
         elapsed_ms >= static_cast<double>(escalate_ms);
}

/// Era43f/Era44: safety valve when mesh blockers persist after lighting done.
/// Era44: triggers abort-drain mode only — does not permit InGame with holes.
inline bool ShouldForceEnterMeshAbort(int fov_debt, bool ring_ready,
                                      double elapsed_ms, int abort_ms)
{
  return fov_debt <= 0 && !ring_ready && abort_ms > 0 &&
         elapsed_ms >= static_cast<double>(abort_ms);
}

/// Era44: combined gpu_warmup debt (fifo/gpu/ring/lit) for honest 93→100% bar.
inline float EnterGpuWarmupDebtFraction(int fifo_n, int fifo_peak, int gpu_n,
                                        int gpu_peak, int ring_n, int ring_peak,
                                        int fov_debt, int fov_peak)
{
  float sum = 0.0f;
  int n = 0;
  if (fifo_peak > 0)
  {
    sum += CreateBarDebtFraction(fifo_n, fifo_peak);
    ++n;
  }
  if (gpu_peak > 0)
  {
    sum += CreateBarDebtFraction(gpu_n, gpu_peak);
    ++n;
  }
  if (ring_peak > 0)
  {
    sum += CreateBarDebtFraction(ring_n, ring_peak);
    ++n;
  }
  if (fov_peak > 0)
  {
    sum += CreateBarDebtFraction(fov_debt, fov_peak);
    ++n;
  }
  if (n == 0)
  {
    return 0.0f;
  }
  return sum / static_cast<float>(n);
}

/// Era44: 1 - weighted debt fraction (monotonic when peaks fixed).
inline float EnterGpuWarmupProgressFraction(int fifo_n, int fifo_peak, int gpu_n,
                                            int gpu_peak, int ring_n,
                                            int ring_peak, int fov_debt,
                                            int fov_peak)
{
  return 1.0f - EnterGpuWarmupDebtFraction(fifo_n, fifo_peak, gpu_n, gpu_peak,
                                           ring_n, ring_peak, fov_debt, fov_peak);
}

/// Era44: clamp display progress so bar never regresses (Era34 pattern).
inline float EnterGpuWarmupMonotonicProgress(float raw_prog, float &display_prog)
{
  display_prog = std::max(display_prog, raw_prog);
  return display_prog;
}

/// Era51: mesh warmup bar — resolved/total from queue depth (not cumulative
/// rebuild ops).
inline float MeshWarmupResolvedFraction(size_t start_pending, size_t pending_now)
{
  const size_t total = std::max<size_t>(1, start_pending);
  const size_t resolved =
      pending_now >= total ? total : total - pending_now;
  return static_cast<float>(resolved) / static_cast<float>(total);
}

inline std::string FormatMeshWarmupProgress(size_t start_pending,
                                            size_t pending_now)
{
  const size_t total = std::max<size_t>(1, start_pending);
  const size_t resolved =
      pending_now >= total ? total : total - pending_now;
  return "Building meshes... " + std::to_string(resolved) + "/" +
         std::to_string(total) + " (" + std::to_string(pending_now) +
         " pending)";
}

/// Diagnostic: mesh/light holes remaining. Not an enter InGame gate (SOTA:
/// IsEnterGpuWarmupReady = spawn-ring Presentable + lit + vis).
inline bool NeedsCruiseStabilize(const EnterLitSample &sample,
                                 int pending_light_focus,
                                 bool visual_holes, int focus_not_render_ready)
{
  if (sample.mesh_dirty || sample.mesh_gpu_pending_near > 0 ||
      sample.mesh_async_pending || sample.ring_not_ready > 0 ||
      sample.mesh_visual_warmup)
  {
    return true;
  }
  if (visual_holes || focus_not_render_ready > 0 || pending_light_focus > 0)
  {
    return true;
  }
  return false;
}

/// InGame when spawn ring + mesh blockers + lit snapshot debt + visibility
/// ready. Visibility is the r=4 worklist (remaining==0 and no stale dark), not
/// void telem / fifo / terminal-held. No extra cruise-stabilize gate.
inline bool IsEnterGpuWarmupReady(bool ring_ready, int fov_debt,
                                  bool mesh_blockers_clear, bool min_frames_done,
                                  bool visibility_ready = true)
{
  return min_frames_done && ring_ready && fov_debt <= 0 && mesh_blockers_clear &&
         visibility_ready;
}

/// Era51: skip GPU destroy when coop PrepareView already settled spawn ring.
inline bool ShouldResetRenderStateForGpuWarmup(bool spawn_prepared_by_coop)
{
  return !spawn_prepared_by_coop;
}

/// Era51: coop load can exit GpuWarmup after min frames — upload meshes before
/// InGame, not only on the last warmup frame (ResetWorldRenderState otherwise
/// leaves an empty world).
inline bool ShouldWarmupGreedyGpuDuringEnter(int gpu_warmup_frames_remaining,
                                             bool spawn_prepared_by_coop,
                                             int frame_index,
                                             int min_warmup_frames)
{
  if (gpu_warmup_frames_remaining == 1)
  {
    return true;
  }
  return spawn_prepared_by_coop && frame_index >= min_warmup_frames - 1;
}

/// Era44: documented last-resort after abort-drain (default ≥150s post-stabilize).
inline bool ShouldForceEnterInGameAfterAbortDrain(double elapsed_ms,
                                                  int force_ingame_ms)
{
  return force_ingame_ms > 0 &&
         elapsed_ms >= static_cast<double>(force_ingame_ms);
}

/// Stabilize P0: release enter after abort-drain wall when underfeet is lit/true-dark
/// present — does not bypass full visibility (residual FOV debt may remain).
inline bool ShouldReleaseEnterAfterAbortUnderfeetCap(
    bool abort_drain, double elapsed_ms, int force_ingame_ms,
    bool underfeet_present_ready, int underfeet_gpu_pending)
{
  if (!abort_drain || !underfeet_present_ready || underfeet_gpu_pending > 0)
  {
    return false;
  }
  return ShouldForceEnterInGameAfterAbortDrain(elapsed_ms, force_ingame_ms);
}

/// Era35 P1: SoftDefer empty scan cy-window for near-FOV columns (horiz<=2)
/// covers full column (0..max_cy) so air chunks with trees/leaves above
/// preferred_cy+2 are not permanently stuck as SoftDefer empty.
inline int SoftDeferCyWindowNearTop(int max_cy, int preferred_cy, int horiz,
                                    int near_r = 2)
{
  if (horiz <= near_r)
  {
    return max_cy;
  }
  return std::min(max_cy, preferred_cy + 2);
}

/// Era35 P2: dynamic SoftDefer empty ownership cap based on current empty count.
inline int SoftDeferOwnershipCap(int softdefer_empty_n)
{
  return std::min(24, softdefer_empty_n / 4 + 12);
}

/// Era35 P4: cruise catch-up emerge budget boost when SoftDefer empty lag.
inline double CruiseCatchUpEmergeBudgetMs(double base_ms,
                                          int softdefer_empty_n,
                                          bool moving)
{
  if (!moving || softdefer_empty_n <= 5)
  {
    return base_ms;
  }
  return base_ms * 1.5;
}

/// Era35 P4: cruise catch-up ownership cap boost.
inline int CruiseCatchUpOwnershipCap(int base_cap, int softdefer_empty_n,
                                     bool moving)
{
  if (!moving || softdefer_empty_n <= 5)
  {
    return base_cap;
  }
  return std::max(base_cap, 18);
}

/// Era44b: elapsed suffix for enter warmup status strings.
inline std::string FormatEnterWarmupElapsed(double elapsed_ms)
{
  const int sec = static_cast<int>(elapsed_ms / 1000.0);
  return " (" + std::to_string(sec) + "s)";
}

/// Era44b/48: shared enter warmup status — mesh/gpu blockers beat fifo/lit queue.
inline std::string BuildEnterWarmupStatus(const EnterLitSample &sample,
                                          int fov_debt, bool ring_ready,
                                          bool abort_drain, double elapsed_ms,
                                          int hard_wall_ms,
                                          int visibility_debt = 0)
{
  const bool slow = elapsed_ms >= static_cast<double>(hard_wall_ms);
  const std::string elapsed_suffix = FormatEnterWarmupElapsed(elapsed_ms);
  if (abort_drain && !ring_ready)
  {
    std::string status =
        "Finishing terrain (slow)… fifo=" + std::to_string(sample.fifo_n) +
        " gpu=" + std::to_string(sample.mesh_gpu_pending_near) + " ring=" +
        std::to_string(sample.ring_not_ready);
    if (sample.mesh_async_pending)
    {
      status += " async=1";
    }
    status += elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  if (sample.mesh_dirty || sample.mesh_missing_greedy ||
      sample.mesh_gpu_pending_near > 0 || sample.mesh_async_pending ||
      !ring_ready)
  {
    std::string status =
        "Building terrain… gpu=" +
        std::to_string(sample.mesh_gpu_pending_near);
    if (sample.mesh_async_pending)
    {
      status += " async=1";
    }
    status += " dirty=" + std::to_string(sample.mesh_dirty ? 1 : 0);
    status += elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  if (visibility_debt > 0)
  {
    std::string status =
        "Finishing view… " + std::to_string(visibility_debt) + " left";
    status += elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  if (fov_debt > 0 || sample.fifo_n > 0 || sample.inflight > 0)
  {
    std::string status = "Lighting queue… fifo=" + std::to_string(sample.fifo_n) +
                         " inflight=" + std::to_string(sample.inflight);
    if (fov_debt > 0)
    {
      status += " debt=" + std::to_string(fov_debt);
    }
    status += elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  if (sample.ring_not_ready > 0)
  {
    std::string status =
        "Finishing view… ring=" + std::to_string(sample.ring_not_ready) +
        " left" + elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  return "Preparing view..." + elapsed_suffix;
}

/// Era44b/48: combined debt for coop/gpu_warmup progress (mesh_dirty weighted).
inline int EnterWarmupCombinedDebt(const EnterLitSample &sample, int fov_debt)
{
  return sample.fifo_n + sample.mesh_gpu_pending_near + sample.ring_not_ready +
         fov_debt + sample.visibility_debt + (sample.mesh_dirty ? 8 : 0);
}

/// Era44b: status prefers mesh/gpu over fifo when both are active.
inline bool EnterWarmupStatusPrefersMeshOverFifo(const EnterLitSample &sample,
                                                 int fov_debt, bool ring_ready,
                                                 bool abort_drain)
{
  const std::string status =
      BuildEnterWarmupStatus(sample, fov_debt, ring_ready, abort_drain, 0.0, 0);
  return status.rfind("Building terrain", 0) == 0 ||
         status.rfind("Finishing terrain", 0) == 0;
}

/// Era45 B5: allow seam MarkDirty on enter until spawn ring ready.
inline bool ShouldSuppressRelightSeamDirtyForEnterGate(
    bool enter_lit_gate_active, bool spawn_mesh_ring_ready, bool base_suppress)
{
  if (enter_lit_gate_active && !spawn_mesh_ring_ready)
  {
    return false;
  }
  return base_suppress;
}

/// Era45 B2: ownership policy for MarkRelit → RemeshAfterApply.
enum class RemeshAfterLitApplyDecision
{
  Schedule,
  SkipAlreadyDirty,
  SkipAlreadyRaa,
  PreferKickGpu,
  SkipInflight,
  SkipEnterLitQuiesce,
};

/// FullyDark under quiesce remeshes only when this apply had a light/voxel delta.
inline RemeshAfterLitApplyDecision ClassifyRemeshAfterLitApply(
    bool is_dirty, bool raa_pending, bool gpu_pending, bool inflight,
    bool enter_lit_quiesce = false, bool fully_dark_drawable = false,
    bool column_visual_ready = false, bool light_or_voxel_delta = false,
    bool stale_field = false)
{
  if (is_dirty)
  {
    return RemeshAfterLitApplyDecision::SkipAlreadyDirty;
  }
  if (raa_pending)
  {
    return RemeshAfterLitApplyDecision::SkipAlreadyRaa;
  }
  if (gpu_pending)
  {
    // Stale FullyDark under enter quiesce must MarkDirty/RAA — PreferKick alone
    // left black_sticky flicker with pending≈45 (manual 20260820).
    if (enter_lit_quiesce && fully_dark_drawable && stale_field)
    {
      return RemeshAfterLitApplyDecision::Schedule;
    }
    return RemeshAfterLitApplyDecision::PreferKickGpu;
  }
  if (inflight)
  {
    return RemeshAfterLitApplyDecision::SkipInflight;
  }
  if (enter_lit_quiesce && fully_dark_drawable)
  {
    if (ShouldSpinFullyDarkRemesh(true, stale_field, light_or_voxel_delta))
    {
      return RemeshAfterLitApplyDecision::SkipEnterLitQuiesce;
    }
    if (ShouldRemeshAfterLightApply(light_or_voxel_delta) || stale_field)
    {
      return RemeshAfterLitApplyDecision::Schedule;
    }
    return RemeshAfterLitApplyDecision::SkipEnterLitQuiesce;
  }
  if (enter_lit_quiesce && column_visual_ready && !stale_field)
  {
    return RemeshAfterLitApplyDecision::SkipEnterLitQuiesce;
  }
  if (enter_lit_quiesce)
  {
    return RemeshAfterLitApplyDecision::Schedule;
  }
  return RemeshAfterLitApplyDecision::Schedule;
}

inline bool ShouldScheduleRemeshAfterLitApply(bool is_dirty, bool raa_pending,
                                              bool gpu_pending, bool inflight,
                                              bool enter_lit_quiesce = false,
                                              bool fully_dark_drawable = false,
                                              bool column_visual_ready = false,
                                              bool light_or_voxel_delta = false)
{
  return ClassifyRemeshAfterLitApply(is_dirty, raa_pending, gpu_pending,
                                     inflight, enter_lit_quiesce,
                                     fully_dark_drawable, column_visual_ready,
                                     light_or_voxel_delta) ==
         RemeshAfterLitApplyDecision::Schedule;
}

/// When Dirty/GPU/Inflight already owns the chunk, do not latch a second
/// RemeshAfterApply (ColPipe P2: dual Dirty+RAA refeed dirty_revisit forever).
inline bool ShouldLatchRemeshAfterApplyWhileOwned(
    RemeshAfterLitApplyDecision /*decision*/,
    bool /*fully_dark_or_stale*/ = true)
{
  return false;
}

/// Idle stand: chronic DarkFaceNearN must not reopen recover every 2 frames
/// (manual 160656: dark≈4000 forever → RelightThenMesh+RemeshSeam flicker).
inline int RecoverWatchdogFramesForDarkNear(bool moving)
{
  return moving ? 2 : 45;
}

/// Idle RemeshSeam storm: raw dark_n stays high in caves; only sticky debt
/// or moving dark pressure should refeed Seam (manual 160656 stand flicker).
inline bool ShouldEnqueueRecoverRemeshSeamStorm(bool moving, int dark_n,
                                               int black_sticky)
{
  if (black_sticky > 0)
  {
    return true;
  }
  if (!moving)
  {
    return false;
  }
  return dark_n > 200;
}

/// Do not re-Enqueue RelightThenMesh when the column already owns light work.
inline bool ShouldEnqueueUrgentDarkRelight(bool pending_dark_preview,
                                           bool urgent_dark_pending,
                                           bool already_owns_light_work)
{
  if (already_owns_light_work)
  {
    return false;
  }
  return pending_dark_preview || urgent_dark_pending;
}

/// True when Dirty/RAA/GPU/inflight already owns remesh for this column.
inline bool ColumnHasRemeshOwner(bool is_dirty, bool raa_pending,
                                 bool gpu_pending, bool inflight)
{
  return is_dirty || raa_pending || gpu_pending || inflight;
}

/// After lit apply: RemeshSeam only when no RAA/GPU owner (Sodium PreferKick).
inline bool ShouldEnqueueRemeshSeamAfterLit(bool had_mesh, bool enter_quiesce,
                                           bool any_drawable,
                                           bool column_has_remesh_owner)
{
  if (!had_mesh || enter_quiesce || column_has_remesh_owner)
  {
    return false;
  }
  // Drawable remesh is owned by RemeshAfterApply / PreferKick — not RemeshSeam.
  return !any_drawable;
}

/// Era46 C: ring blocker label for heartbeat (mesh gate honesty).
inline const char *EnterWarmupRingBlockerLabel(bool mesh_dirty,
                                               int gpu_pending_near,
                                               bool async_pending,
                                               bool missing_greedy)
{
  if (mesh_dirty)
  {
    return "dirty";
  }
  if (gpu_pending_near > 0)
  {
    return "gpu";
  }
  if (async_pending)
  {
    return "async";
  }
  if (missing_greedy)
  {
    return "missing";
  }
  return "none";
}

} // namespace cutum

#pragma once

#include "World/Diagnostics/EnterLitDiagnostics.h"
#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>
#include <string>
#include <utility>

namespace cutum
{

/// Era33 P0: initial-area visual gate on progress bar = LitDrawable FOV ring.
/// (Era29 was underfeet r=1; cold create entered InGame with empty FOV.)
inline int EnterVisualWarmupRadiusChunks()
{
  return kVisualStageLitDrawableHoriz;
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

/// Era36 B2 / Era40: dynamic CaptureMovingBgCap — fire at pendf>15 (was 20).
inline int DynamicCaptureMovingBgCap(int pending_light_focus,
                                     int base_cap = 1)
{
  if (pending_light_focus <= 15)
  {
    return base_cap;
  }
  return std::min(4, pending_light_focus / 10 + 1);
}

/// Era36 B3 / Era40: land moving drain when pendf>15 (was 30).
inline bool ShouldDrainPendingLightLandMoving(int pending_light_focus,
                                              int threshold = 15)
{
  return pending_light_focus > threshold;
}

/// Era36 B3 / Era40: moving drain floor for land cruise light debt.
inline int LandMovingRelightDrainFloor(bool moving, int pending_light_focus,
                                       int threshold = 15)
{
  if (!moving || !ShouldDrainPendingLightLandMoving(pending_light_focus,
                                                    threshold))
  {
    return 0;
  }
  return 1;
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
                                        bool require_zero = true)
{
  if (fov_lit_debt <= 0)
  {
    return false;
  }
  if (require_zero)
  {
    return true;
  }
  return elapsed_ms < static_cast<double>(hard_wall_ms);
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

/// Era46 B / Era47 P3 / Era49b: MarkDirty after RAA commit only when not already
/// dirty and not GPU. Under enter lit gate / GPU quiesce drain — never MarkDirty
/// (PreferKick-only; RepairEnterLit owns FullyDark reschedule).
inline bool ShouldMarkDirtyAfterRemeshAfterApplyCommit(bool already_dirty,
                                                      bool gpu_pending,
                                                      bool enter_lit_gate = false)
{
  if (enter_lit_gate)
  {
    return false;
  }
  return !already_dirty && !gpu_pending;
}

/// Era49b: enter gate active ⇒ treat as enter_lit_gate for RAA commit policy.
inline bool EnterGateBlocksRaaMarkDirty(bool enter_lit_quiesce,
                                        bool enter_gpu_quiesce_drain)
{
  return enter_lit_quiesce || enter_gpu_quiesce_drain;
}

/// Era47 P1 / Era49: after enter unready==0 under gate, MarkRelit must not
/// feed new Dirty/RAA/RemeshSeam — PreferKick/Skip only.
/// Era49: suppress uses enter VisualReady unready count (not snapshot debt alone).
inline bool ShouldSuppressMarkRelitRemeshOnEnterLitQuiesce(
    bool enter_lit_gate_active, int enter_unready, int /*fifo_n*/ = 0)
{
  return enter_lit_gate_active && enter_unready <= 0;
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

/// Era53: skip MarkRelit remesh churn after one stale attempt owns the column.
inline bool ShouldSkipMarkRelitAfterEnterStaleAttempt(bool enter_gate_active,
                                                      bool stale_attempt_owned)
{
  return enter_gate_active && stale_attempt_owned;
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

/// Era55: Gate remaining==0 is the enter visual SoT. Era29 underfeet
/// lit-drawable / SoftDefer must not keep mesh_visual_warmup after that.
inline bool EnterVisualWarmupYieldsToGateRemaining(bool enter_gate_active,
                                                   int visibility_debt)
{
  return enter_gate_active && visibility_debt <= 0;
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

/// Era44/48/49: InGame only when ring + mesh blockers + lit debt + visibility
/// ready (visibility includes unready==0, void≤200, stale==0 when Strict).
inline bool IsEnterGpuWarmupReady(bool ring_ready, int fov_debt,
                                  bool mesh_blockers_clear, bool min_frames_done,
                                  bool visibility_ready = true)
{
  return min_frames_done && ring_ready && fov_debt <= 0 && mesh_blockers_clear &&
         visibility_ready;
}

/// Era44: documented last-resort after abort-drain (default ≥300s).
inline bool ShouldForceEnterInGameAfterAbortDrain(double elapsed_ms,
                                                  int force_ingame_ms)
{
  return force_ingame_ms > 0 &&
         elapsed_ms >= static_cast<double>(force_ingame_ms);
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
    if (sample.dark_face_void_near_n > EnterVisibilityVoidNearMax())
    {
      status += " void=" + std::to_string(sample.dark_face_void_near_n);
    }
    status += elapsed_suffix;
    if (slow)
    {
      status += " (slow)";
    }
    return status;
  }
  if (sample.mesh_visual_warmup)
  {
    std::string status = "Finishing view… visual" + elapsed_suffix;
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

/// Era48/49: fully_dark_drawable carves out quiesce — remesh-after-lit required.
/// Era49: under quiesce, Skip only when column already VisualReady; otherwise
/// Schedule so remesh can finish (no Sticky-as-ready).
inline RemeshAfterLitApplyDecision ClassifyRemeshAfterLitApply(
    bool is_dirty, bool raa_pending, bool gpu_pending, bool inflight,
    bool enter_lit_quiesce = false, bool fully_dark_drawable = false,
    bool column_visual_ready = false)
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
    return RemeshAfterLitApplyDecision::PreferKickGpu;
  }
  if (inflight)
  {
    return RemeshAfterLitApplyDecision::SkipInflight;
  }
  if (enter_lit_quiesce && fully_dark_drawable)
  {
    return RemeshAfterLitApplyDecision::Schedule;
  }
  if (enter_lit_quiesce && column_visual_ready)
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
                                              bool column_visual_ready = false)
{
  return ClassifyRemeshAfterLitApply(is_dirty, raa_pending, gpu_pending,
                                     inflight, enter_lit_quiesce,
                                     fully_dark_drawable,
                                     column_visual_ready) ==
         RemeshAfterLitApplyDecision::Schedule;
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

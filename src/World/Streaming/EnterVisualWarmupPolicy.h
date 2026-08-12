#pragma once

#include "World/Streaming/VisualStagePolicy.h"

#include <algorithm>
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

} // namespace cutum

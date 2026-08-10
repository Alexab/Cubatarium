#pragma once

#include "World/Streaming/VisualStagePolicy.h"

namespace cutum
{

/// Era33 P0: initial-area visual gate on progress bar = LitDrawable FOV ring.
/// (Era29 was underfeet r=1; cold create entered InGame with empty FOV.)
inline int EnterVisualWarmupRadiusChunks()
{
  return kVisualStageLitDrawableHoriz;
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

} // namespace cutum

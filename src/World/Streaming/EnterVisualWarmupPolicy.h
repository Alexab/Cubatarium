#pragma once

namespace cutum
{

/// Era29 I-E1: underfeet visual gate radius (LitDrawable); greedy floor stays 2.
inline int EnterVisualWarmupRadiusChunks()
{
  return 1;
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

} // namespace cutum

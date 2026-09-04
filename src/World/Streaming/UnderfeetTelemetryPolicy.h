#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

#include "World/Core/World.h"

namespace cutum
{

/// Align underfeet telem with draw SoT (HasDrawable / GpuPacked / opaque present).
inline bool UnderfeetColumnHasDrawable(bool slice_drawable, bool pending_gpu,
                                       bool draw_ok, bool opaque_in_pass)
{
  return slice_drawable || pending_gpu || opaque_in_pass ||
         (draw_ok && slice_drawable);
}

/// ColdPL F4: telem reconcile — draw_ok + GPU/inflight ⇒ visually present.
inline bool UnderfeetOpaquePresentPredicted(bool opaque_in_pass, bool draw_ok,
                                            bool pending_gpu, bool inflight)
{
  if (opaque_in_pass)
  {
    return true;
  }
  return draw_ok && (pending_gpu || inflight);
}

/// FZ2-R3 / FZ2.1-B6 / FZ2.2-C6b: perf spike SoT — monotonic present when drawable.
inline int UnderfeetOpaquePresentForPerf(bool draw_ok, bool latched,
                                         bool predicted)
{
  if (!draw_ok)
  {
    return latched ? 1 : 0;
  }
  return (latched || predicted) ? 1 : 0;
}

/// FZ2.2-C6b: hold predicted present 2–3 frames at draw_ok to damp telem flips.
inline bool UnderfeetOpaquePresentPredictedHeld(
    bool opaque_in_pass, bool draw_ok, bool pending_gpu, bool inflight,
    bool &latched_predicted, int &hold_frames)
{
  const bool raw =
      UnderfeetOpaquePresentPredicted(opaque_in_pass, draw_ok, pending_gpu,
                                      inflight);
  if (raw)
  {
    latched_predicted = true;
    hold_frames = 3;
    return true;
  }
  if (draw_ok && latched_predicted && hold_frames > 0)
  {
    --hold_frames;
    return true;
  }
  latched_predicted = false;
  hold_frames = 0;
  return false;
}

inline ColumnRenderableState::BlockReason ReconcileUnderfeetBlockReason(
    ColumnRenderableState::BlockReason reason, bool has_drawable, bool draw_ok,
    bool pending_gpu)
{
  if (!has_drawable && !draw_ok && !pending_gpu)
  {
    return reason;
  }
  if (reason == ColumnRenderableState::BlockReason::NotReadyState ||
      reason == ColumnRenderableState::BlockReason::NotLoaded)
  {
    if (pending_gpu)
    {
      return ColumnRenderableState::BlockReason::GpuInFlight;
    }
    if (has_drawable || draw_ok)
    {
      return ColumnRenderableState::BlockReason::None;
    }
  }
  return reason;
}

} // namespace cutum

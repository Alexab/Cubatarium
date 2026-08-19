#pragma once

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

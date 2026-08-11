#pragma once

#include "World/Streaming/VisualStagePolicy.h"

namespace cutum
{

/// Era40: miss / SoftDefer-empty Relight pin covers LitDrawable ring (not only
/// near horiz<=2). Matches hide-until-lit publication radius.
inline int RelightMissPinMaxHoriz(int ring = kVisualStageLitDrawableHoriz)
{
  return ring;
}

/// Era40: force FIFO Enqueue for FOV miss even when Keys/FIFO ghost-empty.
inline bool ShouldForceMissColumnFifoEnqueue(bool miss_or_visual_hole,
                                             bool pending_or_void_or_undrawn,
                                             bool already_in_fifo)
{
  if (!miss_or_visual_hole || !pending_or_void_or_undrawn)
  {
    return false;
  }
  return !already_in_fifo;
}

/// Era40: prefer full finalize band on miss rim pin (horiz<=ring).
inline bool ShouldPreferMissFinalizeBand(int miss_horiz,
                                         int ring = kVisualStageLitDrawableHoriz)
{
  return miss_horiz >= 0 && miss_horiz <= ring;
}

/// Era40: soft-cap FIFO stuck with empty Completed under FOV miss ⇒ raise drain.
inline bool ShouldBoostRelightDrainUnderFifoMissStarve(int fifo_n, int soft_cap,
                                                       int completed_n,
                                                       bool miss_or_visual_hole)
{
  if (!miss_or_visual_hole || soft_cap <= 0)
  {
    return false;
  }
  return fifo_n >= soft_cap && completed_n <= 0;
}

/// Era40 P3: analyze soft-fail when FIFO stuck + dropped churn + no completed.
inline bool RelightFifoStuckSoftFail(int fifo_med, int soft_cap,
                                     int completed_med, int fifo_dropped_delta,
                                     bool miss_end_or_stuck)
{
  if (!miss_end_or_stuck || soft_cap <= 0)
  {
    return false;
  }
  return fifo_med >= soft_cap - 1 && completed_med <= 0 &&
         fifo_dropped_delta > 0;
}

} // namespace cutum

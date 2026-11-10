#pragma once
// BUDGET_MS: 0.0  // perf-root P4: measure via Tracy; kill-switch required for new heuristics

namespace cutum
{

/// Era49: shared **outcome** meaning for column VisualReady.
/// EnterVisualGate (Load/Create) is blocking over RD; CruiseStreaming is
/// budgeted/progressive — different orchestrators, same outcome predicate.
/// NEVER treat schedule / StickyRemesh / PreferKick / EnterLitQuiesce latch /
/// void plateau as ready by themselves.

/// Runtime overlay (URuntimeTuning::StrictEnterVisualReady). Default on.
inline bool StrictEnterVisualReadyDefault()
{
  return true;
}

/// Pure composition used by unit tests and World::IsColumnVisualReady.
/// SoftDefer empty (!drawable) is never enter VisualReady — hole until bake.
/// Sticky / Dirty queued / quiesce alone must not flip ready.
inline bool ColumnVisualReadyFromFlags(bool terrain_in_band, bool pending_light,
                                       bool lit_ready,
                                       bool fully_dark_solid_drawable,
                                       bool solid_missing_greedy,
                                       bool soft_defer_empty,
                                       bool /*sticky_scheduled*/ = false,
                                       bool /*quiesce_latched*/ = false)
{
  // No terrain in band: N/A for enter visibility (do not treat as unready).
  if (!terrain_in_band)
  {
    return true;
  }
  if (pending_light || !lit_ready)
  {
    return false;
  }
  if (fully_dark_solid_drawable)
  {
    return false;
  }
  if (solid_missing_greedy)
  {
    return false;
  }
  if (soft_defer_empty)
  {
    return false;
  }
  return true;
}

/// Schedule / Sticky / quiesce are never sufficient for ready (Era49 invariants).
inline bool ColumnScheduleAloneIsVisualReady(bool sticky_or_dirty_or_kick)
{
  (void)sticky_or_dirty_or_kick;
  return false;
}

inline bool ColumnQuiesceLatchAloneIsVisualReady(bool quiesce_latched)
{
  (void)quiesce_latched;
  return false;
}

} // namespace cutum

#ifndef SCHEDULESHEDPOLICY_H
#define SCHEDULESHEDPOLICY_H

namespace cutum
{

/// Q3 / 162400: never idle-shed schedule_policy under holes, EnterLitGate,
/// unfinished_hard, near miss, underfeet, or apply-stale storm. Phase5.1 had
/// dropped visual_holes from the guard; UV floor=1 + ScheduleShedUv1 then
/// allowed stream_loads==0 shed while holes were red.
inline bool AllowScheduleShed(bool missing_underfeet, bool enter_warmup_active,
                              bool enter_lit_gate_active, bool visual_holes,
                              bool unfinished_protect, bool near_miss_protect,
                              bool stale_storm_protect)
{
  return !missing_underfeet && !enter_warmup_active && !enter_lit_gate_active &&
         !visual_holes && !unfinished_protect && !near_miss_protect &&
         !stale_storm_protect;
}

/// Soft-exit mid full-schedule must honor the same correctness guards, or
/// prep_deadline break skips DropRemesh/CancelAsync under holes.
inline bool AllowScheduleSoftExit(bool missing_underfeet,
                                  bool enter_warmup_active,
                                  bool enter_lit_gate_active, bool visual_holes,
                                  bool unfinished_protect,
                                  bool near_miss_protect,
                                  bool stale_storm_protect)
{
  return AllowScheduleShed(missing_underfeet, enter_warmup_active,
                           enter_lit_gate_active, visual_holes,
                           unfinished_protect, near_miss_protect,
                           stale_storm_protect);
}

} // namespace cutum

#endif

#pragma once

#include "World/Core/RuntimeTuning.h"

namespace cutum
{

struct MemoryBudgetSample
{
  double private_mb{0.0};
  int stream_pressure{0}; // 0/1/2
  double last_wall_ms{0.0};
  int visual_holes{0};
  int pending_light_focus{0};
  int dirty_chunks{0};
  int baseline_keep_margin{2};
  int visual_rd{4};
  /// FZ2.7-P10: Capture hard-cap must not starve Completed refill.
  int relight_fifo_n{0};
  int relight_completed_n{0};
  int unfinished_visual{0};
};

struct MemoryBudgetDecision
{
  int keep_margin{2};
  int max_effective_rd{4};
  bool allow_keep_prewarm{true};
  bool emergency_cancel_outside{false};
  int capture_hard_cap{-1}; // <0 = no override
  int memory_pressure{0};   // 0 under expand / 1 soft / 2 hard
};

class UMemoryBudgetController
{
public:
  static MemoryBudgetDecision Evaluate(const MemoryBudgetSample &sample,
                                       const URuntimeTuning &tuning);

  /// Call at most every N frames from streaming; returns true if Evaluate ran.
  bool MaybeEvaluate(int frame_counter, const MemoryBudgetSample &sample,
                     const URuntimeTuning &tuning, MemoryBudgetDecision &out);

  const MemoryBudgetDecision &Last() const { return LastDecision; }

private:
  MemoryBudgetDecision LastDecision{};
  int LastEvalFrame{-1000};
  static constexpr int kEvalIntervalFrames = 20;
};

} // namespace cutum
